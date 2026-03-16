#include "calculations.h"
#include "data/electricity_structs.h"
#include "data/weather_structs.h"
#include "electricity_cache_handler.h"
#include "weather_cache_handler.h"
#include "maestroutils/file_utils.h"
#include <maestroutils/error.h>
#include <maestroutils/file_logging.h>
#include <maestromodules/thread_pool.h>
#include <maestroutils/math_utils.h>
#include <pthread.h>
#include "maestroutils/json_utils.h"
#define MAESTROUTILS_WITH_CJSON 1 // get rid of stupid lsp error
#include <stdio.h>
#include <time.h>
// #define BASE_CACHE_PATH "/var/lib/maestro/spots"
// #define AVG_CACHE_PATH "/var/lib/maestro/"

/* --------------------------- Internal --------------------------- */

pthread_mutex_t mutex_global = PTHREAD_MUTEX_INITIALIZER;

typedef enum
{
  DAILY = 24,
  HOURLY = 4,
  QUARTERLY = 1, // Highest accuracy

} ResultsInterval;

typedef struct
{
  const Calc_Args*         calc_args;
  const Electricity_Spots* spots;
  const Weather*           weather; // pass as null before taskwork for no solar
  ResultsInterval          interval;

} Calc_Thread_Args;

int calc_init_ech(ECH* ech);

void calc_daily_averages_threadtask(void* _context);

static inline int calc_results_create(Calc_Results* _Res, 
  const Calc_Args* _Args,
  const Electricity_Spots* _S, 
  const Weather* _W, // optional
  unsigned int _count, // amount of spots
  ResultsInterval _interval, // output accuracy from quarterly data
  float _avg_thresh);

static inline int calc_results_json_create(Calc_Results* _Res, const char* _filename);

void calc_results_dispose(Calc_Results* _Res);

/* ---------------------------------------------------------------- */

void calc_daily_averages_threadtask(void* _context)
{
  if (!_context)
    return;

  Calc_Thread_Args* T_Args = (Calc_Thread_Args*)_context;
  const Calc_Args* C_Args = T_Args->calc_args;
  const Electricity_Spots* S = T_Args->spots;
  const Weather* W = T_Args->weather;

  if (!S || !C_Args || !C_Args->calcs_dir) {
    LOG_ERROR("One or more args are missing");
    return;
  }

  if (W != NULL && (S->price_count > W->count)) {
    LOG_ERROR("Weather count must be higher than spot count");
    return;
  }

  int epd = 0; // entries-per-day
  if (T_Args->interval == QUARTERLY)
    epd = 96;
  else if (T_Args->interval == HOURLY)
    epd = 24;
  else if (T_Args->interval == DAILY) 
    epd = 1;
  else {
    LOG_ERROR("Unexpected interval value");
    return;
  }

  /* Define filename */
  int name_len = 0;
  char filename[128];
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  char today[11]; // YYYY-MM-DD
  strftime(today, sizeof(today), "%Y-%m-%d", &tm);

  if (W != NULL) { // With solar
    name_len = snprintf(filename, sizeof(filename), 
        "%s/Daily_%s_SP%i_SE%i_Solar.json", 
        C_Args->calcs_dir, today, epd, C_Args->price_class+1);
  }
  else {
    name_len = snprintf(filename, sizeof(filename), 
        "%s/Daily_%s_SP%i_SE%i.json", 
        C_Args->calcs_dir, today, epd, C_Args->price_class+1);
  }

  if (name_len < 0 || (size_t)name_len >= sizeof(filename)) {
    LOG_ERROR("Failed to create filename");
    return;
  }


  Calc_Results Res = {0};
  if (calc_results_create(&Res, C_Args, S, W, epd, T_Args->interval, 0.25) != SUCCESS) {
    LOG_ERROR("calc_results_create");
    return;
  }

  if (calc_results_json_create(&Res, filename) != SUCCESS) {
    LOG_ERROR("calc_create_json");
    calc_results_dispose(&Res);
    return;
  }
  calc_results_dispose(&Res);

}

// TODO: Consider highest/lowest prices more than arbitrary deviance from average
static inline int calc_results_create(Calc_Results* _Res, 
    const Calc_Args* _Args,
    const Electricity_Spots* _S, 
    const Weather* _W, // optional
    unsigned int _count, // amount of expected output spots (e.g 24 or 96)
    ResultsInterval _interval, // output accuracy from quarterly data (e.g 4 or 1)
    float _avg_thresh)
{
  if (!_S 
      || _interval == 0 // don't divide by zero
      || _count < 1
      || _count > _S->price_count // count must not be more than available spots
      || _interval > _count // update interval not more than count
      // || (_W && _W->count != _S->price_count) // spots and weather must have same count
      || _avg_thresh < 0.0 || _avg_thresh > 1.0) {
    LOG_ERROR("One or more conditions not met for calculations");
    return ERR_INVALID_ARG;
  }

  float solars_avg;
  float solars_gain_avg;
  _Res->cheapness_thresh = _avg_thresh;
  _Res->count = _count;

  /* Sync spot and weather indexes (TODO: remove this when db implemented and just make sure count and time_start is the same for them) */
  int weather_index_start = 0;
  int spot_index_start = 0;
  if (_W) {
    weather_index_start = -1;
    for (int i = 0; i < (int)_W->count; i++)
      if (_W->values[i].timestamp == _S->prices[0].time_start)
        weather_index_start = i;
    
    printf("weather index start: %i\n", weather_index_start);
    printf("weather timestamp start: %li\n", _W->values[weather_index_start].timestamp);
    if (weather_index_start < 0) {
      LOG_ERROR("No overlapping weather+spots data as input");
      return ERR_INVALID_ARG;
    }

    int synced_weather_count = _W->count - weather_index_start + 1;
    if (synced_weather_count < (int)_S->price_count) {
      _Res->count = synced_weather_count / _interval;
      spot_index_start = (int)_S->price_count - synced_weather_count - 1;
    }
    printf("synced_weather_count: %i\n",  synced_weather_count);
    printf("spot index start: %i\n", spot_index_start);
    printf("spot timestamp start: %li\n", _S->prices[spot_index_start].time_start);
  }

  /* Result allocations */
  _Res->timestamps = calloc(1, sizeof(time_t) * _Res->count);
  if (!_Res->timestamps) {
    LOG_ERROR("calloc");
    calc_results_dispose(_Res);
    return ERR_NO_MEMORY;
  }
  _Res->spot_prices = calloc(1, sizeof(float) * _Res->count);
  if (!_Res->spot_prices) {
    LOG_ERROR("calloc");
    calc_results_dispose(_Res);
    return ERR_NO_MEMORY;
  }

  _Res->spot_prices_cheapness = calloc(1, sizeof(PriceCheapness) * _Res->count);
  if (!_Res->spot_prices_cheapness) {
    LOG_ERROR("calloc");
    calc_results_dispose(_Res);
    return ERR_NO_MEMORY;
  }
  _Res->spot_prices_deviation = calloc(1, sizeof(float) * _Res->count);
  if (!_Res->spot_prices_deviation) {
    LOG_ERROR("calloc");
    calc_results_dispose(_Res);
    return ERR_NO_MEMORY;
  }

  float price_sum = 0.0;
  for (int i = 0; i < (int)(_Res->count * _interval); i++)
    price_sum += _S->prices[i+spot_index_start].spot_price;

  _Res->spot_prices_avg = price_sum / (_Res->count * _interval);

  printf("spot prices average: %f\n", _Res->spot_prices_avg);

  /* Solar specific */
  if (_W) {
    _Res->solar_gains = calloc(1, sizeof(float) * _Res->count);
    if (!_Res->solar_gains) {
      LOG_ERROR("calloc");
      calc_results_dispose(_Res);
      return ERR_NO_MEMORY;
    }
    _Res->solar_gains_deviation = calloc(1, sizeof(float) * _Res->count);
    if (!_Res->solar_gains_deviation) {
      LOG_ERROR("calloc");
      calc_results_dispose(_Res);
      return ERR_NO_MEMORY;
    }

    // We use GTI
    // solars_avg = math_get_avg_float(&_W->values[96].radiation_tilted, _W->count-96);
    float solars_sum = 0.0;
    for (int i = 0; i < (int)(_Res->count * _interval); i++)
      solars_sum += _W->values[i+weather_index_start].radiation_tilted;

    solars_avg = solars_sum / (float)(_Res->count * _interval);
    solars_gain_avg = (solars_avg / 1000) * _Res->spot_prices_avg;
    _Res->solar_gains_avg = solars_gain_avg;
  }

  unsigned int i;
  for (i = 0; i < _Res->count; i++) {
    unsigned int base_index = (i+spot_index_start) * _interval;

    _Res->timestamps[i] = _S->prices[base_index].time_start;

    float spot_sum = 0.0;
    float solar_sum = 0.0;

    /* Calculate sums within interval */
    unsigned int j;
    for (j = 0; j < _interval && (base_index + j) < _S->price_count; j++) {
      spot_sum += _S->prices[base_index + j].spot_price;
      if (_W) solar_sum += _W->values[base_index + j + weather_index_start].radiation_tilted;
    }
    float spot_price = spot_sum / j;

    /* Get how much money we gain from solar panels */
    if (_W) {
      if (solar_sum > 0) {
        float solar_watt = (solar_sum * _Args->panel_size) / _interval;
        float solar_gain = (solar_watt / 1000) * spot_price;
        double solar_gain_deviation = ((solar_gain - solars_gain_avg) / solars_gain_avg);
        _Res->solar_gains[i] = solar_gain;
        _Res->solar_gains_deviation[i] = solar_gain_deviation;
        
        spot_price -= solar_gain; // deduct solar gain from price
      }
      else {
        _Res->solar_gains[i] = 0.0;
        _Res->solar_gains_deviation[i] = 0.0;
      }
    }

    _Res->spot_prices[i] = spot_price;
    float spots_deviation = ((spot_price - _Res->spot_prices_avg) / _Res->spot_prices_avg);
    _Res->spot_prices_deviation[i] = spots_deviation * 100;

    if (spot_price > (_Res->spot_prices_avg * (1.0f + _Res->cheapness_thresh)))
      _Res->spot_prices_cheapness[i] = EXPENSIVE;
    else if (spot_price < (_Res->spot_prices_avg * (1.0f - _Res->cheapness_thresh)))
      _Res->spot_prices_cheapness[i] = CHEAP;
    else
      _Res->spot_prices_cheapness[i] = AVERAGE;
  }

  return SUCCESS;
}

void calc_results_dispose(Calc_Results* _Res)
{
  if (!_Res)
    return;

  if (_Res->timestamps)
    free(_Res->timestamps);
  if (_Res->spot_prices_cheapness)
    free(_Res->spot_prices_cheapness);
  if (_Res->spot_prices_deviation)
    free(_Res->spot_prices_deviation);
  if (_Res->spot_prices)
    free(_Res->spot_prices);
  if (_Res->solar_gains)
    free(_Res->solar_gains);
  if (_Res->solar_gains_deviation)
    free(_Res->solar_gains_deviation);

  _Res = NULL;
}

int calc_results_json_create(Calc_Results* _Res, const char* _filename)
{
  if (!_Res || !_Res->timestamps || !_Res->spot_prices || !_Res->spot_prices_cheapness || !_Res->spot_prices_deviation)
    return ERR_INVALID_ARG;

  cJSON* Json_Root = cJSON_CreateObject();
  if (Json_Root == NULL) {
    const char* err = cJSON_GetErrorPtr();
    if (err != NULL)
      LOG_ERROR("cJSON: %s\n", err);
    return ERR_JSON_PARSE;
  }

  /* Meta root objects */
  cJSON* Json_Meta = cJSON_CreateObject();

  cJSON_AddStringToObject(Json_Meta, "deviation_unit", "%");
  cJSON_AddStringToObject(Json_Meta, "spot_unit", "kW");
  cJSON_AddStringToObject(Json_Meta, "currency", "SEK");
  cJSON_AddNumberToObject(Json_Meta, "cheapness_threshold_percent", _Res->cheapness_thresh * 100);
  cJSON_AddNumberToObject(Json_Meta, "spot_price_average", _Res->spot_prices_avg);

  if (_Res->solar_gains && _Res->solar_gains_deviation) {
    cJSON_AddNumberToObject(Json_Meta, "solar_gains_average", _Res->solar_gains_avg);
  }
  cJSON_AddNumberToObject(Json_Meta, "count", _Res->count);

  cJSON_AddItemToObject(Json_Root, "meta", Json_Meta);

  /* Array of main data */
  cJSON* Json_Results = cJSON_AddArrayToObject(Json_Root, "results");
  for (unsigned int i = 0; i < _Res->count; i++) {
    cJSON* Json_Result = cJSON_CreateObject();
    
    /* Timestamp */
    const char* time_start = parse_epoch_to_iso_full_datetime_string(&_Res->timestamps[i], 0);
    cJSON_AddStringToObject(Json_Result, "timestamp", time_start);
    free((void*)time_start);

    /* Spots */
    cJSON_AddNumberToObject(Json_Result, "spot_price", _Res->spot_prices[i]); 
    cJSON_AddNumberToObject(Json_Result, "spot_price_deviation", _Res->spot_prices_deviation[i]); 

    if (_Res->spot_prices_cheapness[i] == CHEAP)
      cJSON_AddStringToObject(Json_Result, "spot_cheapness", "CHEAP");
    else if (_Res->spot_prices_cheapness[i] == AVERAGE)
      cJSON_AddStringToObject(Json_Result, "spot_cheapness", "AVERAGE");
    else if (_Res->spot_prices_cheapness[i] == EXPENSIVE)
      cJSON_AddStringToObject(Json_Result, "spot_cheapness", "EXPENSIVE");

    /* Solar */
    if (_Res->solar_gains && _Res->solar_gains_deviation) {
      cJSON_AddNumberToObject(Json_Result, "solar_gains", _Res->solar_gains[i]); 
      cJSON_AddNumberToObject(Json_Result, "solar_gains_deviation", _Res->solar_gains_deviation[i]); 
    }

    cJSON_AddItemToArray(Json_Results, Json_Result);
  }

  char* json_str = cJSON_Print(Json_Root);

  /* Write to file */
  pthread_mutex_lock(&mutex_global);
  if (write_string_to_file(json_str, _filename) != 0)
    LOG_ERROR("Failed to write string \"%p\" to cache \"%p\"\n", json_str, _filename); 
  pthread_mutex_unlock(&mutex_global);

  free(json_str);

  cJSON_Delete(Json_Root);
  
  return SUCCESS;
}

// int calc_summary_create(Calc_Results* _Res, Calc_Args* _Args, const char* _filename)
// {
//   if (!_Res || !_Args || !_filename) {
//     return ERR_INVALID_ARG;
//   }
//
//   int res;
//   FILE* f;
//   time_t t = time(NULL);
//   struct tm tm = *localtime(&t);
//
//   char today[11]; // YYYY-MM-DD
//   strftime(today, sizeof(today), "%Y%m%d", &tm);
//
//   char filename[128];
//
//   res = snprintf(filename, sizeof(filename), "%s/%s-SP24-SE%i.txt", _Args->calcs_dir, today, _Args->price_class+1);
//
//   if (res < 0 || (size_t)res >= sizeof(filename)) {
//     LOG_ERROR("Failed to create filename");
//     return ERR_INVALID_ARG;
//   }
//
//   pthread_mutex_lock(&mutex_global);
//   f = fopen(filename, "w");
//   enum
//   {
//     TIME_W = 19,
//     AVG_W = 12,
//     DEV_W = 8
//   };
//
//   fprintf(f, "================ DAILY PRICE SUMMARY =================\n");
//   fprintf(f, "Daily Price Average: %.4f\n", _Res->spot_prices_avg);
//
//   fprintf(f, "------------------------------------------------------\n");
//   fprintf(f, "%-*s | %*s | %*s\n", TIME_W, "Hour starting", AVG_W, "Hour Avg", DEV_W, "Dev %");
//   fprintf(f, "------------------------------------------------------\n");
//
//   char start_buf[32];
//   char end_buf[32];
//   for (unsigned int i = 0; i < _Res->count; i++) {
//
//     struct tm tm_start;
//     struct tm* tmp;
//
//     tmp = localtime(&_Res->timestamps[i]);
//     if (tmp)
//       tm_start = *tmp;
//
//     strftime(start_buf, sizeof(start_buf), "%Y-%m-%d %H:%M:%S", &tm_start);
//
//     double price = (double)s->prices[i].spot_price;
//     double deviation = ((price - daily_avg) / daily_avg) * 100.0;
//
//     const char* status;
//
//     if (_Res->spot_prices_cheapness[i] == CHEAP)
//       status = "CHEAP";
//     else if (_Res->spot_prices_cheapness[i] == EXPENSIVE)
//       status = "EXPENSIVE";
//     else
//       status = "AVERAGE";
//
//     /* Solar */
//     if (_Res->solar_gains && _Res->solar_gains_deviation) {
//
//     }
//     else {
//     
//       fprintf(f, "%-*s | %*.3f SEK | %+*.2f\n", TIME_W, start_buf, AVG_W - 4,
//               hour_avg, /* -4 för " SEK" */
//               DEV_W, deviation);
//     }
//
//     tmp = localtime(&s->prices[i].time_start);
//     if (tmp)
//       tm_start = *tmp;
//
//   }
//
//   fprintf(f, "------------------------------------------------------\n\n");
//
//   fclose(f);
//   pthread_mutex_unlock(&mutex_global);
//
//   return SUCCESS;
// }

//TODO: Improve this shit, write proper cache fetching interfaces 
// just based on date range, caller caring only about initing data struct
int calc_fetch_input_data(Electricity_Spots* _S, Weather* _W, Calc_Args* _Args)
{
  if (!_S || !_W || !_Args)
    return ERR_INVALID_ARG;

  int res;
  memset(_S, 0, sizeof(Electricity_Spots));
  memset(_W, 0, sizeof(Weather));

  /* Spots cache */
  time_t today = epoch_now_day();
  const char* spots_cache_path =
      ech_get_cache_filepath(_Args->spots_dir, today, _Args->price_class, _Args->currency);

  res = ech_read_cache(_S, spots_cache_path);
  if (res != 0) {
    LOG_ERROR("ech_read_cache (%i)", res);
    free((void*)spots_cache_path);
    return res;
  }
  free((void*)spots_cache_path);

  /* Weather cache */
  // THIS IS STUPID, THERE'S A CHANCE ENOUGH TIME HAS PASSED AFTER wch_update_cache() THAT NO FILENAME WITH THIS NAME EXISTS! ALSO NOT THE SAME STARTING INDEX AS SPOTS
  // TODO: SWITCH TO MORE DYNAMIC CACHE FETCHING, i.e hdf5/db
  time_t now = time(NULL);
  const char* weather_cache_path =
      wch_get_cache_json_filepath(_Args->weather_dir, now, true);

  if (wch_read_cache_json(_W, weather_cache_path) != SUCCESS) {
    LOG_ERROR("wch_read_cache_json");
    free((void*)weather_cache_path);
    free(_S->prices);
    return ERR_INTERNAL;
  }
  free((void*)weather_cache_path);

  return SUCCESS;
}

/* --------------------------- Interface --------------------------- */

int calc_create_reports(Calc_Args* _Args)
{
  int res;
  if (!_Args->calcs_dir || !_Args->spots_dir || !_Args->weather_dir)
    return ERR_INVALID_ARG;

  create_directory_if_not_exists(_Args->calcs_dir);

  if (_Args->max_threads < 1)
    _Args->max_threads = 1;

  Thread_Pool* TP = tp_init(_Args->max_threads);
  if (!TP) {
    LOG_ERROR("tp_init");
    return ERR_FATAL;
  }

  /* Get cache */
  Weather W = {0};
  Weather* W_Ptr = &W;
  Electricity_Spots S = {0};
  Electricity_Spots* S_Ptr = &S;

  res = calc_fetch_input_data(S_Ptr, W_Ptr, _Args);
  if (res != SUCCESS) {
    LOG_ERROR("calc_fetch_input_data");
    return ERR_FATAL;
  }

  /* Define threadtask arguments */
  Calc_Thread_Args Thread_Args[4] = {
    { 
      .calc_args = _Args,
      .spots = S_Ptr,
      .weather = W_Ptr,
      .interval = HOURLY,
    },
    { 
      .calc_args = _Args,
      .spots = S_Ptr,
      .weather = W_Ptr,
      .interval = QUARTERLY,
    },
    { 
      .calc_args = _Args,
      .spots = S_Ptr,
      .weather = NULL,
      .interval = HOURLY,
    },
    { 
      .calc_args = _Args,
      .spots = S_Ptr,
      .weather = NULL,
      .interval = QUARTERLY,
    },
  };

  /* Define and start threadpool tasks */
  for (int i = 0; i < 4; i++) {
    TP_Task Task = {calc_daily_averages_threadtask, &Thread_Args[i], NULL, NULL};
    res = tp_task_add(TP, &Task);
    if (res != 0) LOG_ERROR("tp_task_add (%i)", res);
    tp_wait(TP); // this after loop
  }

  /* Wait for threads to finish then dispose */
  tp_dispose(TP);

  if (S_Ptr->prices != NULL)
    free(S_Ptr->prices);
  wch_weather_dispose(&W);

  return 0;
}



// void calc_averages_15min_print(void* _context)
// {
//   if (!_context)
//     return;
//
//   Calc_Thread_Args* Task_Args = (Calc_Thread_Args*)_context;
//   Calc_Args* Calc_Args = Task_Args->calc_args;
//   Electricity_Spots* s = Task_Args->spots;
//
//   if (!s || !Calc_Args || !Calc_Args->calcs_dir) {
//     return;
//   }
//
//   FILE* f;
//   time_t t = time(NULL);
//   struct tm tm = *localtime(&t);
//
//   char today[11]; // YYYY-MM-DD
//   strftime(today, sizeof(today), "%Y%m%d", &tm);
//
//   char filename_96[128];
//
//   int res = snprintf(filename_96, sizeof(filename_96), "%s/%s-SP96-SE%i.json", Calc_Args->calcs_dir, today, Calc_Args->price_class+1);
//
//   if (res < 0 || (size_t)res >= sizeof(filename_96)) {
//     LOG_ERROR("Failed to create filename_96");
//     return;
//   }
//
//   f = fopen(filename_96, "w");
//
//   if (!f) {
//     LOG_ERROR("fopen");
//     return;
//   }
//
//   char start_buf[32];
//   char end_buf[32];
//   float daily_avg = math_get_avg_float(&s->prices->spot_price, s->price_count);
//
//   fprintf(f, "\n================ DAILY PRICE SUMMARY ================\n");
//   fprintf(f, "Daily Average: %.4f\n", daily_avg);
//
//   fprintf(f, "--------------------------------------------------------------------------------\n");
//   fprintf(f, "%-*s | %*s | %*s | %-*s\n", INTERVAL_W, "Interval", PRICE_W, "Price/kw", DEV_W,
//           "Dev %", STATUS_W, "Status");
//   fprintf(f, "--------------------------------------------------------------------------------\n");
//
//   for (unsigned int i = 0; i < s->price_count; i++) {
//
//     struct tm tm_start, tm_end;
//     struct tm* tmp;
//
//     tmp = localtime(&s->prices[i].time_start);
//     if (tmp)
//       tm_start = *tmp;
//
//     tmp = localtime(&s->prices[i].time_end);
//     if (tmp)
//       tm_end = *tmp;
//
//     strftime(start_buf, sizeof(start_buf), "%Y-%m-%d %H:%M:%S", &tm_start);
//     strftime(end_buf, sizeof(end_buf), "%Y-%m-%d %H:%M:%S", &tm_end);
//
//     double price = (double)s->prices[i].spot_price;
//     double deviation = ((price - daily_avg) / daily_avg) * 100.0;
//
//     const char* status;
//
//     if (price > daily_avg * 1.1)
//       status = "EXPENSIVE";
//     else if (price < daily_avg * 0.9)
//       status = "CHEAP";
//     else
//       status = "AVERAGE";
//
//     fprintf(f, "%19s - %19s%*s | %*.3f SEK | %+*.2f | %-*s\n", start_buf, end_buf,
//             (int)(INTERVAL_W - 41), "", PRICE_W - 4, price, /* -4 för " SEK" */
//             DEV_W, deviation, STATUS_W, status);
//   }
//
//   fprintf(f,
//           "--------------------------------------------------------------------------------\n\n");
//
//   fclose(f);
//
// }
