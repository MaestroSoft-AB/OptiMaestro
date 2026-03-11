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
#include "maestroutils/json_utils.h"
#define MAESTROUTILS_WITH_CJSON 1 // get rid of stupid lsp error
#include <stdio.h>
#include <time.h>
// #define BASE_CACHE_PATH "/var/lib/maestro/spots"
// #define AVG_CACHE_PATH "/var/lib/maestro/"

/* --------------------------- Internal --------------------------- */

enum
{
  INTERVAL_W = 43,
  PRICE_W = 12,
  DEV_W = 8,
  STATUS_W = 9
};

typedef enum
{
  DAILY = 24,
  HOURLY = 4,
  QUARTERLY = 1,

} ResultsInterval;

typedef struct
{
  Calc_Args*         calc_args;
  Electricity_Spots* spots;
  Weather*           weather;

} Calc_Thread_Args;

int calc_init_ech(ECH* ech);
static inline int calc_results_create(Calc_Results* _Res, 
  const Calc_Args* _Args,
  const Electricity_Spots* _S, 
  const Weather* _W, // optional
  unsigned int _count, // amount of spots
  ResultsInterval _interval, // output accuracy from quarterly data
  float _avg_thresh);
void calc_results_dispose(Calc_Results* _Res);
static inline int calc_json_create(Calc_Results* _Res, const char* _filename);

/* ---------------------------------------------------------------- */


void calc_averages_15min(void* _context)
{
  if (!_context)
    return;

  Calc_Thread_Args* Task_Args = (Calc_Thread_Args*)_context;
  Calc_Args* Calc_Args = Task_Args->calc_args;
  Electricity_Spots* s = Task_Args->spots;

  if (!s || !Calc_Args || !Calc_Args->calcs_dir) {
    return;
  }

  FILE* f;
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);

  char today[11]; // YYYY-MM-DD
  strftime(today, sizeof(today), "%Y%m%d", &tm);

  char filename_96[128];

  int res = snprintf(filename_96, sizeof(filename_96), "%s/%s-SP96-SE%i.json", Calc_Args->calcs_dir, today, Calc_Args->price_class+1);

  if (res < 0 || (size_t)res >= sizeof(filename_96)) {
    LOG_ERROR("Failed to create filename_96");
    return;
  }

  f = fopen(filename_96, "w");

  if (!f) {
    LOG_ERROR("fopen");
    return;
  }

  char start_buf[32];
  char end_buf[32];
  float daily_avg = math_get_avg_float(&s->prices->spot_price, s->price_count);

  fprintf(f, "\n================ DAILY PRICE SUMMARY ================\n");
  fprintf(f, "Daily Average: %.4f\n", daily_avg);

  fprintf(f, "--------------------------------------------------------------------------------\n");
  fprintf(f, "%-*s | %*s | %*s | %-*s\n", INTERVAL_W, "Interval", PRICE_W, "Price/kw", DEV_W,
          "Dev %", STATUS_W, "Status");
  fprintf(f, "--------------------------------------------------------------------------------\n");

  for (unsigned int i = 0; i < s->price_count; i++) {

    struct tm tm_start, tm_end;
    struct tm* tmp;

    tmp = localtime(&s->prices[i].time_start);
    if (tmp)
      tm_start = *tmp;

    tmp = localtime(&s->prices[i].time_end);
    if (tmp)
      tm_end = *tmp;

    strftime(start_buf, sizeof(start_buf), "%Y-%m-%d %H:%M:%S", &tm_start);
    strftime(end_buf, sizeof(end_buf), "%Y-%m-%d %H:%M:%S", &tm_end);

    double price = (double)s->prices[i].spot_price;
    double deviation = ((price - daily_avg) / daily_avg) * 100.0;

    const char* status;

    if (price > daily_avg * 1.1)
      status = "EXPENSIVE";
    else if (price < daily_avg * 0.9)
      status = "CHEAP";
    else
      status = "AVERAGE";

    fprintf(f, "%19s - %19s%*s | %*.3f SEK | %+*.2f | %-*s\n", start_buf, end_buf,
            (int)(INTERVAL_W - 41), "", PRICE_W - 4, price, /* -4 för " SEK" */
            DEV_W, deviation, STATUS_W, status);
  }

  fprintf(f,
          "--------------------------------------------------------------------------------\n\n");

  fclose(f);

}

void calc_averages_1hour(void* _context)
{
  if (!_context)
    return;

  Calc_Thread_Args* Task_Args = (Calc_Thread_Args*)_context;
  Calc_Args* Calc_Args = Task_Args->calc_args;
  Electricity_Spots* s = Task_Args->spots;

  if (!s || !Calc_Args || !Calc_Args->calcs_dir) {
    return;
  }

  int res;
  FILE* f;
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);

  char today[11]; // YYYY-MM-DD
  strftime(today, sizeof(today), "%Y%m%d", &tm);

  char filename_24[128];

  res = snprintf(filename_24, sizeof(filename_24), "%s/%s-SP24-SE%i.json", Calc_Args->calcs_dir, today, Calc_Args->price_class+1);

  if (res < 0 || (size_t)res >= sizeof(filename_24)) {
    LOG_ERROR("Failed to create filename_24");
    return;
  }

  f = fopen(filename_24, "w");

  enum
  {
    TIME_W = 19,
    AVG_W = 12,
    DEV_W = 8
  };

  float daily_avg = math_get_avg_float(&s->prices->spot_price, s->price_count);

  fprintf(f, "\n================ HOURLY AVERAGES =====================\n");
  fprintf(f, "Daily Average: %.4f\n", daily_avg);

  fprintf(f, "------------------------------------------------------\n");
  fprintf(f, "%-*s | %*s | %*s\n", TIME_W, "Hour starting", AVG_W, "Hour Avg", DEV_W, "Dev %");
  fprintf(f, "------------------------------------------------------\n");

  for (unsigned int i = 0; i < s->price_count; i += 4) {

    double hour_sum = 0.0;
    int count = 0;

    for (int j = 0; j < 4 && (i + j) < s->price_count; j++) {
      hour_sum += s->prices[i + j].spot_price;
      count++;
    }
    double hour_avg = hour_sum / count;

    double deviation = ((hour_avg - daily_avg) / daily_avg) * 100.0;

    char start_buf[20];
    struct tm tm_start;
    struct tm* tmp;

    tmp = localtime(&s->prices[i].time_start);
    if (tmp)
      tm_start = *tmp;

    strftime(start_buf, sizeof(start_buf), "%Y-%m-%d %H:%M:%S", &tm_start);

    fprintf(f, "%-*s | %*.3f SEK | %+*.2f\n", TIME_W, start_buf, AVG_W - 4,
            hour_avg, /* -4 för " SEK" */
            DEV_W, deviation);
  }

  fprintf(f, "------------------------------------------------------\n\n");

  fclose(f);
}

void calc_averages_1hour_solar(void* _context)
{
  if (!_context)
    return;

  Calc_Thread_Args* T_Args = (Calc_Thread_Args*)_context;
  Calc_Args* C_Args = T_Args->calc_args;
  Electricity_Spots* S = T_Args->spots;
  Weather* W = T_Args->weather;

  if (!S || !W || !C_Args || !C_Args->calcs_dir) {
    LOG_ERROR("One or more args are missing");
    return;
  }

  if (S->price_count != W->count) {
    LOG_ERROR("Spot price count do not equal weather count");
    return;
  }

  time_t t = time(NULL);
  struct tm tm = *localtime(&t);

  char today[11]; // YYYY-MM-DD
  strftime(today, sizeof(today), "%Y-%m-%d", &tm);

  char filename_24[128];

  int name_len = snprintf(filename_24, sizeof(filename_24), "%s/%s_SP24_SE%i_Solar.json", C_Args->calcs_dir, today, C_Args->price_class+1);

  if (name_len < 0 || (size_t)name_len >= sizeof(filename_24)) {
    LOG_ERROR("Failed to create filename_24");
    return;
  }

  Calc_Results Res;
  if (calc_results_create(&Res, C_Args, S, W, 24, HOURLY, 0.1) != SUCCESS) {
    LOG_ERROR("calc_results_create");
    return;
  }

  if (calc_json_create(&Res, filename_24) != SUCCESS) {
    LOG_ERROR("calc_create_json");
    calc_results_dispose(&Res);
    return;
  }

}

/* TODO: Consider highest/lowest prices more than arbitrary deviance from average */
static inline int calc_results_create(Calc_Results* _Res, 
    const Calc_Args* _Args,
    const Electricity_Spots* _S, 
    const Weather* _W, // optional
    unsigned int _count, // amount of spots
    ResultsInterval _interval, // output accuracy from quarterly data
    float _avg_thresh)
{
  if (!_S 
      || _count > _S->price_count // count must not be more than available spots
      || _interval > _count // update interval not more than count
      || (_W && _W->count != _S->price_count) // spots and weather must have same count
      || _avg_thresh < 0.0 || _avg_thresh > 1.0) {
    LOG_ERROR("One or more conditions not met for calculations");
    return ERR_INVALID_ARG;
  }

  float spots_avg;
  float solars_avg;
  float solars_gain_avg;
  _Res->cheapness_thresh = _avg_thresh;

  _Res->timestamps = calloc(1, sizeof(float) * _count);
  if (!_Res->timestamps) {
    LOG_ERROR("calloc");
    return ERR_NO_MEMORY;
  }
  _Res->spot_prices = calloc(1, sizeof(float) * _count);
  if (!_Res->spot_prices) {
    LOG_ERROR("calloc");
    free(_Res->timestamps);
    return ERR_NO_MEMORY;
  }
  _Res->spot_prices_cheapness = calloc(1, sizeof(PriceCheapness) * _count);
  if (!_Res->spot_prices_cheapness) {
    LOG_ERROR("calloc");
    free(_Res->timestamps);
    free(_Res->spot_prices);
    return ERR_NO_MEMORY;
  }
  _Res->spot_prices_deviation = calloc(1, sizeof(PriceCheapness) * _count);
  if (!_Res->spot_prices_deviation) {
    LOG_ERROR("calloc");
    free(_Res->timestamps);
    free(_Res->spot_prices);
    free(_Res->spot_prices_cheapness);
    return ERR_NO_MEMORY;
  }

  spots_avg = math_get_avg_float(&_S->prices->spot_price, _S->price_count);
  _Res->spot_prices_avg = spots_avg;

  /* Solar specific */
  if (_W) {
    _Res->solar_gains = calloc(1, sizeof(float) * _count);
    if (!_Res->solar_gains) {
      LOG_ERROR("calloc");
      free(_Res->timestamps);
      free(_Res->spot_prices);
      free(_Res->spot_prices_cheapness);
      free(_Res->spot_prices_deviation);
      return ERR_NO_MEMORY;
    }
    _Res->solar_gains_deviation = calloc(1, sizeof(float) * _count);
    if (!_Res->solar_gains_deviation) {
      LOG_ERROR("calloc");
      free(_Res->timestamps);
      free(_Res->spot_prices);
      free(_Res->spot_prices_cheapness);
      free(_Res->spot_prices_deviation);
      free(_Res->solar_gains);
      return ERR_NO_MEMORY;
    }

    // We assume we have GTI
    solars_avg = math_get_avg_float(&_W->values->radiation_tilted, _W->count);
    solars_gain_avg = (solars_avg / 1000) * spots_avg;
    _Res->solar_gains_avg = solars_gain_avg;
  }

  unsigned int i;
  for (i = 0; i < _count; i += (int)_interval) {
    _Res->spot_prices[i] = _S->prices[i].spot_price;
    _Res->timestamps[i] = _S->prices[i].time_start;

    float spot_sum = 0.0;
    float solar_sum = 0.0;
    int count = 0;

    for (int j = 0; j < (int)_interval && (i + j) < _S->price_count; j++) {
      spot_sum += _S->prices[i + j].spot_price;
      if (_W) solar_sum += _W->values[i + j].radiation_tilted;
      count++;
    }
    float spot_price = spot_sum / count;

    float spots_deviation = ((spot_price - spots_avg) / spots_avg) * 100.0;
    
    /* Get how much money we gain from solar panels */
    if (_W) {
      float solar_watt = (solar_sum * _Args->panel_size) / count;
      float solar_gain = (solar_watt / 1000) * spot_price;
      double solar_gain_deviation = ((solar_gain - solars_gain_avg) / solars_gain_avg) * 100.0;
      _Res->solar_gains[i] = solar_gain;
      _Res->solar_gains_deviation[i] = solar_gain_deviation;
    }

    _Res->spot_prices_deviation[i] = spots_deviation;
    if (spot_price > spots_avg * _Res->cheapness_thresh)
      _Res->spot_prices_cheapness[i] = EXPENSIVE;
    else if (spot_price < spots_avg * _Res->cheapness_thresh)
      _Res->spot_prices_cheapness[i] = CHEAP;
    else
      _Res->spot_prices_cheapness[i] = AVERAGE;

  }
  _Res->count = i;

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

int calc_json_create(Calc_Results* _Res, const char* _filename)
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
  cJSON_AddStringToObject(Json_Meta, "currency", "SEK");
  cJSON_AddNumberToObject(Json_Meta, "spot_price_average_threshold", _Res->spot_prices_avg);
  cJSON_AddNumberToObject(Json_Meta, "spot_price_average", _Res->spot_prices_avg);


  if (_Res->solar_gains && _Res->solar_gains_deviation) {
    cJSON_AddNumberToObject(Json_Meta, "solar_gains_average", _Res->solar_gains_avg);
  }

  cJSON_AddItemToObject(Json_Root, "meta", Json_Meta);

  /* Array of main data */
  cJSON* Json_Results = cJSON_AddArrayToObject(Json_Root, "results");
  for (unsigned int i = 0; i < _Res->count; i++) {
    cJSON* Json_Result = cJSON_CreateObject();
    
    /* Timestamp */
    const char* time_start = parse_epoch_to_iso_full_datetime_string(&_Res->timestamps[i], 0);
    cJSON_AddStringToObject(Json_Result, "timestamp", time_start);

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

  if (write_string_to_file(json_str, _filename) != 0)
    LOG_ERROR("Failed to write string \"%p\" to cache \"%p\"\n", json_str, _filename); 

  free(json_str);

  cJSON_Delete(Json_Root);
  
  return SUCCESS;
}

int calc_summary_create(Calc_Results* _Res, const char* _filename)
{

  return SUCCESS;
}

//TODO: Get rid of all this shit, write proper cache fetching interfaces 
// just based on date range, caller caring only about data struct
int calc_init_ech(ECH* _ech)
{
  if (!_ech) {
    return ERR_INVALID_ARG;
  }

  memset(&_ech->spot, 0, sizeof(Electricity_Spots));
  memset(&_ech->epjn_spot, 0, sizeof(EPJN_Spots));

  _ech->spot.price_class = _ech->conf.price_class;
  _ech->spot.currency = _ech->conf.currency;

  time_t today = epoch_now_day();

  _ech->cache_path =
      ech_get_cache_filepath(_ech->conf.data_dir, today, _ech->conf.price_class, _ech->conf.currency);

  return SUCCESS;
}

int calc_init_weather(Weather* _W, const char* _cache_dir)
{
  if (!_W) {
    return ERR_INVALID_ARG;
  }

  memset(_W, 0, sizeof(Weather));

  

  char* cache_path =
      wch_get_cache_filepath();

  wch_read_cache(_W, cache_path);


  return SUCCESS;
}

/* --------------------------- Interface --------------------------- */

int calc_create_reports(Calc_Args* _Args)
{
  if (!_Args->calcs_dir || !_Args->spots_dir || !_Args->weather_dir)
    return ERR_INVALID_ARG;

  create_directory_if_not_exists(_Args->calcs_dir);

  if (_Args->max_threads < 1)
    _Args->max_threads = 1;

  int res;

  Calc_Thread_Args Thread_Args = {0};
  Thread_Args.calc_args = _Args;

  Thread_Pool* TP = tp_init(_Args->max_threads);
  if (!TP) {
    LOG_ERROR("tp_init");
    return ERR_FATAL;
  }

  /* Get Electricity cache */
  ECH_Conf conf = {0};
  ECH_Conf* conf_ptr = &conf;
  conf_ptr->currency = _Args->currency;
  conf_ptr->data_dir = _Args->spots_dir;
  conf_ptr->price_class = _Args->price_class;

  ECH ech = {0};
  ech.conf = conf;
  ECH* ech_ptr = &ech;

  res = calc_init_ech(&ech);
  if (res != 0) {
    LOG_ERROR("calc_init_ech (%i)", res);
    ech_dispose(ech_ptr);
    return res;
  }

  res = ech_read_cache(&ech_ptr->spot, ech_ptr->cache_path);
  if (res != 0) {
    LOG_ERROR("ech_read_cache (%i)", res);
    ech_dispose(ech_ptr);
    return res;
  }

  Thread_Args.spots = &ech_ptr->spot;

  /* Get weather cache */
  Weather W;
  res = calc_init_weather(&W, _Args->weather_dir);

  /* Define and start thread tasks */
  TP_Task Avg_Q_Task = {calc_averages_15min, &Thread_Args, NULL, NULL};
  TP_Task Avg_H_Task = {calc_averages_1hour, &Thread_Args, NULL, NULL};

  res = tp_task_add(TP, &Avg_H_Task);
  if (res != 0)
    LOG_ERROR("tp_task_add (%i)", res);

  res = tp_task_add(TP, &Avg_Q_Task);
  if (res != 0)
    LOG_ERROR("tp_task_add (%i)", res);

  /* Wait for threads then dispose */
  tp_wait(TP);
  tp_dispose(TP);

  ech_dispose(ech_ptr);
  wch_weather_dispose(&W);

  return 0;
}

