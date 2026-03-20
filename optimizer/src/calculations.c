#include "calculations.h"
#include "data/electricity_structs.h"
#include "data/weather_structs.h"
#include "electricity_cache_handler.h"
#include "maestroutils/file_utils.h"
#include "maestroutils/json_utils.h"
#include "weather_cache_handler.h"
#include <maestromodules/thread_pool.h>
#include <maestroutils/error.h>
#include <maestroutils/file_logging.h>
#include <pthread.h>
#define MAESTROUTILS_WITH_CJSON 1 // get rid of stupid lsp error
#include "sqlite_helpers.h"
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

typedef enum
{
  CHEAP = -1,
  AVERAGE = 0,
  EXPENSIVE = 1

} PriceCheapness;

typedef struct
{
  float* spot_prices;
  float* spot_prices_deviation; // % deviation from daily avg
  PriceCheapness* spot_prices_cheapness;

  float* solar_gains;           // How much we gain from solar panels
  float* solar_gains_deviation; // % deviation from daily avg

  time_t* timestamps;

  float cheapness_thresh; // % to consider above/below average in decimals
  float spot_prices_avg;
  float solar_gains_avg;

  unsigned int count;

} Calc_Results;

typedef struct
{
  const Calc_Args* calc_args;
  const Electricity_Spots* spots;
  const Weather* weather; // pass as null before taskwork for no solar
  ResultsInterval interval;

} Calc_Thread_Args;

void calc_daily_averages_threadtask(void* _context);

static inline int
calc_results_create(Calc_Results* _Res, const Calc_Args* _Args, const Electricity_Spots* _S,
                    const Weather* _W,         // optional
                    unsigned int _count,       // amount of spots
                    ResultsInterval _interval, // output accuracy from quarterly data
                    float _avg_thresh);

static inline int calc_results_json_create(const Calc_Results* _Res, const char* _filename);

static inline int calc_summary_create(const Calc_Results* _Res, const Calc_Args* _Args,
                                      const char* _filename);

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

  if (W != NULL && W->count == 0) {
    LOG_ERROR("No weather data");
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
  int json_name_len = 0, txt_name_len = 0;
  char filename_json[128];
  char filename_txt[128];
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  char today[11]; // YYYY-MM-DD
  strftime(today, sizeof(today), "%Y-%m-%d", &tm);

  if (W != NULL) { // With solar
    json_name_len =
        snprintf(filename_json, sizeof(filename_json), "%s/Daily_%s_SP%i_SE%i_Solar.json",
                 C_Args->calcs_dir, today, epd, C_Args->price_class + 1);
    txt_name_len = snprintf(filename_txt, sizeof(filename_txt), "%s/Daily_%s_SP%i_SE%i_Solar.txt",
                            C_Args->calcs_dir, today, epd, C_Args->price_class + 1);
  } else {
    json_name_len = snprintf(filename_json, sizeof(filename_json), "%s/Daily_%s_SP%i_SE%i.json",
                             C_Args->calcs_dir, today, epd, C_Args->price_class + 1);
    txt_name_len = snprintf(filename_txt, sizeof(filename_txt), "%s/Daily_%s_SP%i_SE%i.txt",
                            C_Args->calcs_dir, today, epd, C_Args->price_class + 1);
  }

  if (json_name_len < 0 || (size_t)json_name_len >= sizeof(filename_json)) {
    LOG_ERROR("Failed to build filename_json");
    return;
  }
  if (txt_name_len < 0 || (size_t)txt_name_len >= sizeof(filename_txt)) {
    LOG_ERROR("Failed to build filename_txt");
    return;
  }

  Calc_Results Results = {0};
  if (calc_results_create(&Results, C_Args, S, W, epd, T_Args->interval, 0.25) != SUCCESS) {
    LOG_ERROR("calc_results_create");
    return;
  }

  if (calc_results_json_create(&Results, filename_json) != SUCCESS) {
    LOG_ERROR("calc_create_json");
    calc_results_dispose(&Results);
    return;
  }
  LOG_INFO("%s created!", filename_json);

  if (calc_summary_create(&Results, C_Args, filename_txt) != SUCCESS) {
    LOG_ERROR("calc_summary_create");
    calc_results_dispose(&Results);
    return;
  }
  LOG_INFO("%s created!", filename_txt);

  calc_results_dispose(&Results);
}

// TODO: Consider highest/lowest prices more than arbitrary deviance from average
static inline int
calc_results_create(Calc_Results* _Res, const Calc_Args* _Args, const Electricity_Spots* _S,
                    const Weather* _W,         // optional
                    unsigned int _count,       // amount of expected output spots (e.g 24 or 96)
                    ResultsInterval _interval, // output accuracy from quarterly data (e.g 4 or 1)
                    float _avg_thresh)
{

  if (!_S || _interval == 0                     // don't divide by zero
      || _count < 1 || _count > _S->price_count // count must not be more than available spots
      || _interval > _count                     // update interval not more than count
      // || (_W && _W->count != _S->price_count) // spots and weather must have same count
      || _avg_thresh < 0.0 || _avg_thresh > 1.0) {
    LOG_ERROR("One or more conditions not met for calculations");
    return ERR_INVALID_ARG;
  }

  float solars_avg;
  float solars_gain_avg;
  _Res->cheapness_thresh = _avg_thresh;
  _Res->count = _count;

  /* Sync spot and weather indexes (TODO: remove this when db implemented and just make sure count
   * and time_start is the same for them) */
  int weather_index_start = 0;
  int spot_index_start = 0;
  if (_W) {
    weather_index_start = -1;
    for (int i = 0; i < (int)_W->count; i++)
      if (_W->values[i].timestamp == _S->prices[0].time_start)
        weather_index_start = i;

    // printf("weather index start: %i\n", weather_index_start);
    // printf("weather timestamp start: %li\n", _W->values[weather_index_start].timestamp);
    if (weather_index_start < 0) {
      LOG_ERROR("No overlapping weather+spots data as input");
      return ERR_INVALID_ARG;
    }

    int synced_weather_count = _W->count - weather_index_start; // removed +1
    if (synced_weather_count < (int)_S->price_count) {
      _Res->count = synced_weather_count / _interval;
      spot_index_start = (int)_S->price_count - synced_weather_count; // removed -1
    }
    // printf("synced_weather_count: %i\n",  synced_weather_count);
    // printf("spot index start: %i\n", spot_index_start);
    // printf("spot timestamp start: %li\n", _S->prices[spot_index_start].time_start);
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
    price_sum += _S->prices[i + spot_index_start].spot_price;

  _Res->spot_prices_avg = price_sum / (_Res->count * _interval);

  // printf("spot prices average: %f\n", _Res->spot_prices_avg);

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
      solars_sum += _W->values[i + weather_index_start].radiation_tilted;

    solars_avg = solars_sum / (float)(_Res->count * _interval);
    solars_gain_avg = (solars_avg / 1000) * _Res->spot_prices_avg;
    _Res->solar_gains_avg = solars_gain_avg;
  }

  unsigned int i;
  for (i = 0; i < _Res->count; i++) {
    unsigned int base_index = (i + spot_index_start) * _interval;

    _Res->timestamps[i] = _S->prices[base_index].time_start;

    float spot_sum = 0.0;
    float solar_sum = 0.0;

    /* Calculate sums within interval */
    unsigned int j;
    for (j = 0; j < _interval && (base_index + j) < _S->price_count; j++) {
      spot_sum += _S->prices[base_index + j].spot_price;
      if (_W)
        solar_sum += _W->values[base_index + j + weather_index_start].radiation_tilted;
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
      } else {
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

static inline int calc_results_json_create(const Calc_Results* _Res, const char* _filename)
{
  if (!_Res || !_Res->timestamps || !_Res->spot_prices || !_Res->spot_prices_cheapness ||
      !_Res->spot_prices_deviation)
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

/* TODO: Fix concurrent writes, ie. better use of mutex lock */
static inline int calc_summary_create(const Calc_Results* _Res, const Calc_Args* _Args,
                                      const char* _filename)
{
  if (!_Res || !_Args || !_Res->timestamps || !_Res->spot_prices || !_Res->spot_prices_deviation ||
      !_Res->spot_prices_cheapness)
    return ERR_INVALID_ARG;

  pthread_mutex_lock(&mutex_global);
  FILE* file = fopen(_filename, "w");
  if (!file)
    return ERR_FATAL;

  time_t now = time(NULL);
  char time_now_str[32];
  strftime(time_now_str, sizeof(time_now_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

  /* Summary */
  fprintf(file, "============================================================\n");
  fprintf(file, "              ENERGY CALCULATION SUMMARY REPORT              \n");
  fprintf(file, "============================================================\n");
  fprintf(file, "Generated at: %s\n", time_now_str);
  fprintf(file, "Directory:    %s\n", _Args->calcs_dir);
  fprintf(file, "Spot Class:   %d\n", (int)_Args->price_class);
  fprintf(file, "Currency:     %d\n", (int)_Args->currency);
  fprintf(file, "Threads:      %d\n", _Args->max_threads);
  fprintf(file, "Panel Size:   %d\n", _Args->panel_size);
  fprintf(file, "------------------------------------------------------------\n");
  fprintf(file, "Average Spot Price: %.2f %s/kW\n", _Res->spot_prices_avg, "SEK");
  fprintf(file, "Cheapness Threshold: %.2f%%\n", _Res->cheapness_thresh * 100);
  if (_Res->solar_gains && _Res->solar_gains_deviation)
    fprintf(file, "Average Solar Gains: %.2f kW\n", _Res->solar_gains_avg);
  fprintf(file, "Data Points: %u\n", _Res->count);
  fprintf(file, "============================================================\n\n");

  /* Table Header */
  if (_Res->solar_gains && _Res->solar_gains_deviation) {
    fprintf(file, "%-25s | %-10s | %-10s | %-10s | %-10s | %-10s\n", "Timestamp", "Spot", "Dev(%)",
            "Cheapness", "SolarGains", "SolarDev(%)");
    fprintf(file, "--------------------------------------------------------------------------------"
                  "------------\n");
  } else {
    fprintf(file, "%-25s | %-10s | %-10s | %-10s\n", "Timestamp", "Spot", "Dev(%)", "Cheapness");
    fprintf(file, "---------------------------------------------------------------\n");
  }

  /* Table Row data */
  for (unsigned int i = 0; i < _Res->count; i++) {
    const char* ts_str = parse_epoch_to_iso_full_datetime_string(&_Res->timestamps[i], 0);
    const char* cheap_str = NULL;
    switch (_Res->spot_prices_cheapness[i]) {
    case CHEAP:
      cheap_str = "CHEAP";
      break;
    case AVERAGE:
      cheap_str = "AVERAGE";
      break;
    case EXPENSIVE:
      cheap_str = "EXPENSIVE";
      break;
    default:
      cheap_str = "-";
      break;
    }

    if (_Res->solar_gains && _Res->solar_gains_deviation) {
      fprintf(file, "%-10.2f | %-10.2f | %-10s | %-10.2f | %-10.2f \n", _Res->spot_prices[i],
              _Res->spot_prices_deviation[i], cheap_str, _Res->solar_gains[i],
              _Res->solar_gains_deviation[i]);
    } else {
      fprintf(file, "%-20s | %-10.2f | %-10.2f | %-10s\n", ts_str, _Res->spot_prices[i],
              _Res->spot_prices_deviation[i], cheap_str);
    }
    free((void*)ts_str);
  }

  fprintf(file, "\n============================================================\n");
  fprintf(file, "End of report.\n");
  fprintf(file, "============================================================\n");

  fclose(file);
  pthread_mutex_unlock(&mutex_global);

  return SUCCESS;
}

// TODO: Improve this shit, write proper cache fetching interfaces
//  just based on date range, caller caring only about initing data struct
int calc_fetch_input_data(Electricity_Spots* _S, Weather* _W, Calc_Args* _Args)
{
  if (!_S || !_W || !_Args || !_Args->sqlhelper) {
    return ERR_INVALID_ARG;
  }

  int res;
  memset(_S, 0, sizeof(Electricity_Spots));
  memset(_W, 0, sizeof(Weather));

  time_t start = (epoch_now_day() + 86400) - 3600;
  time_t end = start + 86400;

  res = ech_get_spots_range(_Args->sqlhelper, _S, _Args->data_dir, _Args->price_class,
                            _Args->currency, start, end);

  if (res != SUCCESS) {
    LOG_WARN("No spot data for tomorrow, falling back to today");

    start = epoch_now_day() - 3600;
    end = start + 86400;

    res = ech_get_spots_range(_Args->sqlhelper, _S, _Args->data_dir, _Args->price_class,
                              _Args->currency, start, end);

    if (res != SUCCESS) {
      LOG_ERROR("ech_get_spots_range failed for both tomorrow and today (%i)", res);
      return res;
    }
  }

  start = _S->prices[0].time_start;
  end = _S->prices[_S->price_count - 1].time_end;

  res = wch_get_weather_range(_Args->sqlhelper, _W, _Args->latitude, _Args->longitude,
                              _Args->panel_tilt, _Args->panel_azimuth, _Args->forecast, start, end);
  if (res != SUCCESS) {
    LOG_ERROR("wch_get_weather_range (%i)", res);
    free(_S->prices);
    _S->prices = NULL;
    return res;
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

/* --------------------------- Interface --------------------------- */

int calc_create_reports(Calc_Args* _Args)
{
  int res;
  if (!_Args->calcs_dir || !_Args->data_dir || !_Args->weather_dir)
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
    if (res != 0)
      LOG_ERROR("tp_task_add (%i)", res);
    tp_wait(TP); // this after loop
  }

  /* Wait for threads to finish then dispose */
  tp_dispose(TP);

  if (S_Ptr->prices != NULL)
    free(S_Ptr->prices);
  wch_weather_dispose(&W);

  return 0;
}
