#define _POSIX_C_SOURCE 200809L
#include "consumption_analysis.h"
#include "electricity_cache_handler.h"
#include "meter_reading_store.h"
#include "sqlite_helpers.h"
#include "weather_cache_handler.h"
#include <maestroutils/error.h>
#include <maestroutils/file_logging.h>
#include <maestroutils/file_utils.h>
#include <maestroutils/time_utils.h>
#define MAESTROUTILS_WITH_CJSON 1
#include <cJSON.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PROFILE_BUCKETS 96
#define PROFILE_HISTORY_DAYS 14
#define PROFILE_PEAK_THRESHOLD 1.35
#define PROFILE_WINDOW_BUCKETS 6
#define PRICE_ESTIMATE_HISTORY_DAYS 2

typedef enum
{
  TOMORROW_SPOTS_ACTUAL,
  TOMORROW_SPOTS_ESTIMATED_WITH_WEATHER,
  TOMORROW_SPOTS_ESTIMATED_FROM_PRICE_HISTORY,
  TOMORROW_SPOTS_FALLBACK_TODAY,
} Tomorrow_Spot_Source;

typedef struct
{
  double today_kwh[PROFILE_BUCKETS];
  double trend_kwh[PROFILE_BUCKETS];
  int    trend_samples[PROFILE_BUCKETS];

  double today_total_kwh;
  double today_cost_sek;
  double trend_total_kwh;
  double predicted_cost_sek;

  int today_peak_bucket;
  int today_low_bucket;
  int trend_peak_bucket;
  int trend_low_bucket;
  int recommendation_start_bucket;

  int detected_peaks;
  int history_days;
} Consumption_Profile;

typedef struct
{
  double temperature_avg;
  double windspeed_avg;
  double precipitation_sum;
  double radiation_avg;
  double sunshine_sum;
  int    samples;
} Weather_Day_Summary;

static int    create_facility_report(const Calc_Args* _Args, const Facility_Config* _F);
static int    build_profile(Consumption_Profile* _P, SqlHelper* _Sql, const Facility_Config* _F,
                            const char* _data_dir);
static int    accumulate_readings(Consumption_Profile* _P, const Meter_Reading_Row* _rows,
                                  size_t _count, time_t _today_start, time_t _tomorrow_start);
static int    load_spots(SqlHelper* _Sql, Electricity_Spots* _S, const Facility_Config* _F,
                         const char* _data_dir, time_t _day_start, bool _allow_today_fallback);
static int    estimate_tomorrow_spots(SqlHelper* _Sql, Electricity_Spots* _out,
                                      const Electricity_Spots* _today, const Facility_Config* _F,
                                      const char* _data_dir, time_t _tomorrow_start,
                                      Tomorrow_Spot_Source* _source);
static int    read_weather_summary(SqlHelper* _Sql, const Facility_Config* _F, time_t _start,
                                   Weather_Day_Summary* _summary);
static double weather_similarity(const Weather_Day_Summary* _target,
                                 const Weather_Day_Summary* _candidate);
static void   apply_spot_costs(Consumption_Profile* _P, const Electricity_Spots* _today,
                               const Electricity_Spots* _tomorrow);
static int    write_json_report(const Consumption_Profile* _P, const Electricity_Spots* _today,
                                const Electricity_Spots* _tomorrow, const Facility_Config* _F,
                                Tomorrow_Spot_Source _tomorrow_spot_source,
                                const char* _filename);
static int    write_display_json_report(const Consumption_Profile* _P,
                                        const Electricity_Spots* _today,
                                        const Electricity_Spots* _tomorrow,
                                        const Facility_Config* _F,
                                        Tomorrow_Spot_Source _tomorrow_spot_source,
                                        time_t _updated_at, const char* _filename);
static int    write_txt_report(const Consumption_Profile* _P, const Electricity_Spots* _today,
                               const Facility_Config* _F,
                               Tomorrow_Spot_Source _tomorrow_spot_source,
                               const char* _filename);
static void   log_report_written(const char* _filename, int _existed);
static int    bucket_for_time(time_t _ts);
static double spot_at_bucket(const Electricity_Spots* _S, int _bucket);
static int    best_price_window(const Electricity_Spots* _S, const Consumption_Profile* _P);
static void   minmax_price_windows(const Electricity_Spots* _S, int* _max_start, int* _max_end,
                                   int* _min_start, int* _min_end);
static void   format_bucket_time(int _bucket, char* _out, size_t _out_sz);
static void   format_bucket_range(int _start, int _count, char* _out, size_t _out_sz);
static void   format_hour_range(int _start_hour, int _end_hour, char* _out, size_t _out_sz);
static char*  report_name(const char* _dir, const char* _facility, const char* _ext, time_t _date);
static char*  display_report_name(const char* _dir, const char* _facility, time_t _date);
static const char* tomorrow_spot_source_string(Tomorrow_Spot_Source _source);
static int    tomorrow_spot_source_code(Tomorrow_Spot_Source _source);
static int    round_to_int(double _value);

int consumption_analysis_create_reports(const Calc_Args* _Args) {
  if (!_Args || !_Args->calcs_dir || !_Args->data_dir || !_Args->sqlhelper ||
      !_Args->facility_configs || _Args->facility_count == 0) {
    return ERR_INVALID_ARG;
  }

  create_directory_if_not_exists(_Args->calcs_dir);

  int result = SUCCESS;
  for (size_t i = 0; i < _Args->facility_count; i++) {
    int res = create_facility_report(_Args, _Args->facility_configs[i]);
    if (res != SUCCESS) {
      LOG_WARN("consumption analysis skipped for facility %s (%i)",
               _Args->facility_configs[i] ? _Args->facility_configs[i]->name : "-", res);
      result = res;
    }
  }

  return result;
}

static int create_facility_report(const Calc_Args* _Args, const Facility_Config* _F) {
  if (!_Args || !_F) {
    return ERR_INVALID_ARG;
  }

  Consumption_Profile profile = {0};
  int res = build_profile(&profile, _Args->sqlhelper, _F, _Args->data_dir);
  if (res != SUCCESS) {
    return res;
  }

  Electricity_Spots today = {0};
  Electricity_Spots tomorrow = {0};
  Tomorrow_Spot_Source tomorrow_spot_source = TOMORROW_SPOTS_ACTUAL;
  time_t now = time(NULL);
  time_t today_start = epoch_now_day();
  time_t tomorrow_start = today_start + 86400;

  res = load_spots(_Args->sqlhelper, &today, _F, _Args->data_dir, today_start - 3600, true);
  if (res != SUCCESS) {
    return res;
  }

  res = load_spots(_Args->sqlhelper, &tomorrow, _F, _Args->data_dir, tomorrow_start - 3600, false);
  if (res != SUCCESS) {
    res = estimate_tomorrow_spots(_Args->sqlhelper, &tomorrow, &today, _F, _Args->data_dir,
                                  tomorrow_start - 3600, &tomorrow_spot_source);
    if (res == SUCCESS) {
      /* source is set by estimate_tomorrow_spots */
    } else {
      LOG_WARN("No tomorrow spot data or estimate available; using today as fallback");
      tomorrow = today;
      tomorrow_spot_source = TOMORROW_SPOTS_FALLBACK_TODAY;
    }
  }

  apply_spot_costs(&profile, &today, &tomorrow);

  char* json_name = report_name(_Args->calcs_dir, _F->name, "json", now);
  if (!json_name) {
    if (tomorrow.prices != today.prices)
      free(tomorrow.prices);
    free(today.prices);
    return ERR_NO_MEMORY;
  }

  int json_existed = access(json_name, F_OK) == 0;
  res = write_json_report(&profile, &today, &tomorrow, _F, tomorrow_spot_source, json_name);
  if (res == SUCCESS) {
    log_report_written(json_name, json_existed);
  }
  free(json_name);
  if (res != SUCCESS) {
    if (tomorrow.prices != today.prices)
      free(tomorrow.prices);
    free(today.prices);
    return res;
  }

  char* display_json_name = display_report_name(_Args->calcs_dir, _F->name, now);
  if (!display_json_name) {
    if (tomorrow.prices != today.prices)
      free(tomorrow.prices);
    free(today.prices);
    return ERR_NO_MEMORY;
  }

  int display_json_existed = access(display_json_name, F_OK) == 0;
  res = write_display_json_report(&profile, &today, &tomorrow, _F, tomorrow_spot_source, now,
                                  display_json_name);
  if (res == SUCCESS) {
    log_report_written(display_json_name, display_json_existed);
  }
  free(display_json_name);
  if (res != SUCCESS) {
    if (tomorrow.prices != today.prices)
      free(tomorrow.prices);
    free(today.prices);
    return res;
  }

  char* txt_name = report_name(_Args->calcs_dir, _F->name, "txt", now);
  if (!txt_name) {
    if (tomorrow.prices != today.prices)
      free(tomorrow.prices);
    free(today.prices);
    return ERR_NO_MEMORY;
  }

  int txt_existed = access(txt_name, F_OK) == 0;
  res = write_txt_report(&profile, &today, _F, tomorrow_spot_source, txt_name);
  if (res == SUCCESS) {
    log_report_written(txt_name, txt_existed);
  }
  free(txt_name);

  if (tomorrow.prices != today.prices)
    free(tomorrow.prices);
  free(today.prices);

  return res;
}

static int build_profile(Consumption_Profile* _P, SqlHelper* _Sql, const Facility_Config* _F,
                         const char* _data_dir) {
  (void)_Sql;
  (void)_F;
  (void)_data_dir;

  Meter_Reading_Store store;
  int res = meter_store_init(&store);
  if (res != SUCCESS) {
    return res;
  }

  res = meter_store_open(&store, METER_READING_STORE_DEFAULT_DB_PATH, true);
  if (res != SUCCESS) {
    meter_store_dispose(&store);
    return res;
  }

  time_t today_start = epoch_now_day();
  time_t tomorrow_start = today_start + 86400;
  time_t history_start = today_start - (PROFILE_HISTORY_DAYS * 86400);

  Meter_Reading_Row* rows = NULL;
  size_t count = 0;
  res = meter_store_read_range(&store, history_start, tomorrow_start, &rows, &count);
  meter_store_close(&store);
  if (res != SUCCESS) {
    meter_store_dispose(&store);
    return res;
  }

  res = accumulate_readings(_P, rows, count, today_start, tomorrow_start);
  meter_store_rows_dispose(&rows, &count);
  meter_store_dispose(&store);

  return res;
}

static int accumulate_readings(Consumption_Profile* _P, const Meter_Reading_Row* _rows,
                               size_t _count, time_t _today_start, time_t _tomorrow_start) {
  if (!_P || !_rows || _count < 2) {
    return ERR_NOT_FOUND;
  }

  double history_kwh[PROFILE_HISTORY_DAYS][PROFILE_BUCKETS] = {{0}};
  int history_days_seen[PROFILE_HISTORY_DAYS] = {0};

  for (size_t i = 1; i < _count; i++) {
    const Meter_Reading* prev = &_rows[i - 1].reading;
    const Meter_Reading* cur = &_rows[i].reading;
    time_t from = _rows[i - 1].received_at;
    time_t to = _rows[i].received_at;

    if (to <= from || (to - from) > 7200) {
      continue;
    }

    double kwh = 0.0;
    if ((prev->present_flags & METER_READING_PRESENT_ENERGY_IMPORT) &&
        (cur->present_flags & METER_READING_PRESENT_ENERGY_IMPORT)) {
      kwh = cur->energy_import_kwh - prev->energy_import_kwh;
      if (kwh < 0.0 || kwh > 50.0) {
        continue;
      }
    } else if (cur->present_flags & METER_READING_PRESENT_AVERAGE_POWER_15M) {
      kwh = (cur->average_power_15m_w / 1000.0) * ((double)(to - from) / 3600.0);
    } else if (cur->present_flags & METER_READING_PRESENT_POWER) {
      kwh = (cur->power_w / 1000.0) * ((double)(to - from) / 3600.0);
    } else {
      continue;
    }

    int bucket = bucket_for_time(to);
    if (bucket < 0 || bucket >= PROFILE_BUCKETS) {
      continue;
    }

    if (to >= _today_start && to < _tomorrow_start) {
      _P->today_kwh[bucket] += kwh;
      _P->today_total_kwh += kwh;
    } else if (to < _today_start) {
      int day_index = (int)((to - (_today_start - (PROFILE_HISTORY_DAYS * 86400))) / 86400);
      if (day_index >= 0 && day_index < PROFILE_HISTORY_DAYS) {
        history_days_seen[day_index] = 1;
        history_kwh[day_index][bucket] += kwh;
      }
    }
  }

  _P->history_days = 0;
  for (int i = 0; i < PROFILE_HISTORY_DAYS; i++)
    _P->history_days += history_days_seen[i];

  if (_P->history_days > 0) {
    for (int bucket = 0; bucket < PROFILE_BUCKETS; bucket++) {
      for (int day = 0; day < PROFILE_HISTORY_DAYS; day++) {
        if (history_days_seen[day]) {
          _P->trend_kwh[bucket] += history_kwh[day][bucket];
          _P->trend_samples[bucket]++;
        }
      }
      _P->trend_kwh[bucket] /= _P->history_days;
      _P->trend_total_kwh += _P->trend_kwh[bucket];
    }
  }

  _P->today_peak_bucket = 0;
  _P->today_low_bucket = 0;
  _P->trend_peak_bucket = 0;
  _P->trend_low_bucket = 0;

  for (int i = 1; i < PROFILE_BUCKETS; i++) {
    if (_P->today_kwh[i] > _P->today_kwh[_P->today_peak_bucket])
      _P->today_peak_bucket = i;
    if (_P->today_kwh[i] < _P->today_kwh[_P->today_low_bucket])
      _P->today_low_bucket = i;
    if (_P->trend_kwh[i] > _P->trend_kwh[_P->trend_peak_bucket])
      _P->trend_peak_bucket = i;
    if (_P->trend_kwh[i] < _P->trend_kwh[_P->trend_low_bucket])
      _P->trend_low_bucket = i;
  }

  double avg = _P->today_total_kwh / PROFILE_BUCKETS;
  for (int i = 0; i < PROFILE_BUCKETS; i++) {
    if (avg > 0.0 && _P->today_kwh[i] >= avg * PROFILE_PEAK_THRESHOLD) {
      _P->detected_peaks++;
    }
  }

  return (_P->today_total_kwh > 0.0 || _P->trend_total_kwh > 0.0) ? SUCCESS : ERR_NOT_FOUND;
}

static int load_spots(SqlHelper* _Sql, Electricity_Spots* _S, const Facility_Config* _F,
                      const char* _data_dir, time_t _day_start, bool _allow_today_fallback) {
  int res = ech_get_spots_range(_Sql, _S, _data_dir, _F->price_class, _F->currency, _day_start,
                                _day_start + 86400);
  if (res == SUCCESS || !_allow_today_fallback) {
    return res;
  }

  return ech_get_spots_range(_Sql, _S, _data_dir, _F->price_class, _F->currency,
                             epoch_now_day() - 3600, epoch_now_day() + 86400 - 3600);
}

static int estimate_tomorrow_spots(SqlHelper* _Sql, Electricity_Spots* _out,
                                   const Electricity_Spots* _today, const Facility_Config* _F,
                                   const char* _data_dir, time_t _tomorrow_start,
                                   Tomorrow_Spot_Source* _source) {
  if (!_Sql || !_out || !_today || !_today->prices || !_F || !_data_dir ||
      _today->price_count == 0 || !_source) {
    return ERR_INVALID_ARG;
  }

  Electricity_Spots history[PRICE_ESTIMATE_HISTORY_DAYS] = {0};
  Weather_Day_Summary tomorrow_weather = {0};
  Weather_Day_Summary history_weather[PRICE_ESTIMATE_HISTORY_DAYS] = {0};
  double weights[PRICE_ESTIMATE_HISTORY_DAYS] = {0};
  int valid_count = 0;
  int weather_weighted_count = 0;

  int has_tomorrow_weather =
      read_weather_summary(_Sql, _F, _tomorrow_start, &tomorrow_weather) == SUCCESS;

  for (int i = 0; i < PRICE_ESTIMATE_HISTORY_DAYS; i++) {
    time_t history_start = _tomorrow_start - ((i + 1) * 7 * 86400);
    int res = ech_get_spots_range(_Sql, &history[i], _data_dir, _F->price_class, _F->currency,
                                  history_start, history_start + 86400);
    if (res != SUCCESS || history[i].price_count == 0) {
      continue;
    }

    weights[i] = 1.0;
    if (has_tomorrow_weather &&
        read_weather_summary(_Sql, _F, history_start, &history_weather[i]) == SUCCESS) {
      weights[i] = weather_similarity(&tomorrow_weather, &history_weather[i]);
      weather_weighted_count++;
    }
    valid_count++;
  }

  if (valid_count == 0) {
    return ERR_NOT_FOUND;
  }

  _out->price_count = _today->price_count;
  _out->interval = _today->interval;
  _out->price_class = _F->price_class;
  _out->currency = _F->currency;
  _out->unit = _today->unit;
  _out->prices = calloc(_out->price_count, sizeof(Electricity_Spot_Price));
  if (!_out->prices) {
    for (int i = 0; i < PRICE_ESTIMATE_HISTORY_DAYS; i++)
      free(history[i].prices);
    return ERR_NO_MEMORY;
  }

  double today_avg = 0.0;
  for (unsigned int i = 0; i < _today->price_count; i++)
    today_avg += _today->prices[i].spot_price;
  today_avg /= _today->price_count;

  double history_avg_sum = 0.0;
  double history_weight_sum = 0.0;
  for (int i = 0; i < PRICE_ESTIMATE_HISTORY_DAYS; i++) {
    if (!history[i].prices || history[i].price_count == 0)
      continue;
    double avg = 0.0;
    for (unsigned int j = 0; j < history[i].price_count; j++)
      avg += history[i].prices[j].spot_price;
    avg /= history[i].price_count;
    history_avg_sum += avg * weights[i];
    history_weight_sum += weights[i];
  }
  double history_avg = history_weight_sum > 0.0 ? history_avg_sum / history_weight_sum : today_avg;

  for (unsigned int slot = 0; slot < _out->price_count; slot++) {
    double weighted_price = 0.0;
    double weight_sum = 0.0;
    for (int h = 0; h < PRICE_ESTIMATE_HISTORY_DAYS; h++) {
      if (!history[h].prices || history[h].price_count == 0)
        continue;
      unsigned int idx = (slot * history[h].price_count) / _out->price_count;
      if (idx >= history[h].price_count)
        idx = history[h].price_count - 1;
      weighted_price += history[h].prices[idx].spot_price * weights[h];
      weight_sum += weights[h];
    }

    double same_weekday_price = weight_sum > 0.0 ? weighted_price / weight_sum
                                                 : _today->prices[slot].spot_price;
    double normalized_history = history_avg > 0.0 ? same_weekday_price * (today_avg / history_avg)
                                                  : same_weekday_price;

    time_t slot_duration = 86400 / _out->price_count;
    _out->prices[slot].time_start = _tomorrow_start + (slot * slot_duration);
    _out->prices[slot].time_end = _out->prices[slot].time_start + slot_duration;
    _out->prices[slot].spot_price = (normalized_history * 0.65) + (_today->prices[slot].spot_price * 0.35);
  }

  for (int i = 0; i < PRICE_ESTIMATE_HISTORY_DAYS; i++)
    free(history[i].prices);

  *_source = weather_weighted_count > 0 ? TOMORROW_SPOTS_ESTIMATED_WITH_WEATHER
                                        : TOMORROW_SPOTS_ESTIMATED_FROM_PRICE_HISTORY;

  return SUCCESS;
}

static int read_weather_summary(SqlHelper* _Sql, const Facility_Config* _F, time_t _start,
                                Weather_Day_Summary* _summary) {
  if (!_Sql || !_F || !_summary) {
    return ERR_INVALID_ARG;
  }

  int panel_tilt = 0;
  unsigned int panel_azimuth = 0;
  if (_F->panel) {
    panel_tilt = _F->panel->tilt;
    panel_azimuth = _F->panel->azimuth;
  }

  Weather weather = {0};
  int res = wch_get_weather_range(_Sql, &weather, _F->lat, _F->lon, panel_tilt, panel_azimuth,
                                  true, _start, _start + 86400);
  if (res != SUCCESS || weather.count == 0 || !weather.values) {
    wch_weather_dispose(&weather);
    return ERR_NOT_FOUND;
  }

  memset(_summary, 0, sizeof(*_summary));
  for (unsigned int i = 0; i < weather.count; i++) {
    _summary->temperature_avg += weather.values[i].temperature;
    _summary->windspeed_avg += weather.values[i].windspeed;
    _summary->precipitation_sum += weather.values[i].precipitation;
    _summary->radiation_avg += weather.values[i].radiation_tilted;
    _summary->sunshine_sum += weather.values[i].sun_duration;
    _summary->samples++;
  }

  if (_summary->samples > 0) {
    _summary->temperature_avg /= _summary->samples;
    _summary->windspeed_avg /= _summary->samples;
    _summary->radiation_avg /= _summary->samples;
  }

  wch_weather_dispose(&weather);
  return _summary->samples > 0 ? SUCCESS : ERR_NOT_FOUND;
}

static double weather_similarity(const Weather_Day_Summary* _target,
                                 const Weather_Day_Summary* _candidate) {
  if (!_target || !_candidate || _target->samples == 0 || _candidate->samples == 0) {
    return 1.0;
  }

  double temp_diff = fabs(_target->temperature_avg - _candidate->temperature_avg) / 10.0;
  double wind_diff = fabs(_target->windspeed_avg - _candidate->windspeed_avg) / 8.0;
  double precip_diff = fabs(_target->precipitation_sum - _candidate->precipitation_sum) / 20.0;
  double radiation_diff = fabs(_target->radiation_avg - _candidate->radiation_avg) / 500.0;
  double sunshine_diff = fabs(_target->sunshine_sum - _candidate->sunshine_sum) / 21600.0;

  return 1.0 / (1.0 + temp_diff + wind_diff + precip_diff + radiation_diff + sunshine_diff);
}

static void apply_spot_costs(Consumption_Profile* _P, const Electricity_Spots* _today,
                             const Electricity_Spots* _tomorrow) {
  for (int i = 0; i < PROFILE_BUCKETS; i++) {
    double today_price = spot_at_bucket(_today, i);
    double tomorrow_price = spot_at_bucket(_tomorrow, i);
    _P->today_cost_sek += _P->today_kwh[i] * today_price;
    _P->predicted_cost_sek += _P->trend_kwh[i] * tomorrow_price;
  }

  _P->recommendation_start_bucket = best_price_window(_tomorrow, _P);
}

static int write_json_report(const Consumption_Profile* _P, const Electricity_Spots* _today,
                             const Electricity_Spots* _tomorrow, const Facility_Config* _F,
                             Tomorrow_Spot_Source _tomorrow_spot_source, const char* _filename) {
  cJSON* root = cJSON_CreateObject();
  if (!root) {
    return ERR_JSON_PARSE;
  }

  int max_start = 0;
  int max_end = 0;
  int min_start = 0;
  int min_end = 0;
  minmax_price_windows(_today, &max_start, &max_end, &min_start, &min_end);

  cJSON* meta = cJSON_AddObjectToObject(root, "meta");
  double avg_price = 0.0;
  for (unsigned int i = 0; i < _today->price_count; i++)
    avg_price += _today->prices[i].spot_price;
  if (_today->price_count > 0)
    avg_price /= _today->price_count;

  cJSON_AddStringToObject(meta, "facility", _F->name);
  cJSON_AddNumberToObject(meta, "electricity_area", (int)_F->price_class + 1);
  cJSON_AddNumberToObject(meta, "slot_count", PROFILE_BUCKETS);
  cJSON_AddNumberToObject(meta, "slot_duration_minutes", 15);
  cJSON_AddStringToObject(meta, "spot_price_unit", "SEK/kWh");
  cJSON_AddStringToObject(meta, "consumption_unit", "kWh");
  cJSON_AddNumberToObject(meta, "historical_days_used", _P->history_days);
  cJSON_AddStringToObject(meta, "tomorrow_spot_price_source",
                          tomorrow_spot_source_string(_tomorrow_spot_source));

  cJSON* summary = cJSON_AddObjectToObject(root, "summary");
  cJSON_AddNumberToObject(summary, "consumption_today_kwh", _P->today_total_kwh);
  cJSON_AddNumberToObject(summary, "spot_cost_today_sek", _P->today_cost_sek);
  cJSON_AddNumberToObject(summary, "predicted_consumption_tomorrow_kwh", _P->trend_total_kwh);
  cJSON_AddNumberToObject(summary, "estimated_spot_cost_tomorrow_sek", _P->predicted_cost_sek);
  cJSON_AddNumberToObject(summary, "detected_high_consumption_slots_today", _P->detected_peaks);
  cJSON_AddNumberToObject(summary, "average_spot_price_today_sek_per_kwh", avg_price);

  cJSON* prices = cJSON_AddObjectToObject(root, "spot_price_today");
  cJSON* highest = cJSON_AddObjectToObject(prices, "highest_price_period");
  cJSON_AddNumberToObject(highest, "start_hour", max_start);
  cJSON_AddNumberToObject(highest, "end_hour", max_end);
  cJSON_AddNumberToObject(highest, "price_sek_per_kwh", spot_at_bucket(_today, max_start * 4));
  cJSON* lowest = cJSON_AddObjectToObject(prices, "lowest_price_period");
  cJSON_AddNumberToObject(lowest, "start_hour", min_start);
  cJSON_AddNumberToObject(lowest, "end_hour", min_end);
  cJSON_AddNumberToObject(lowest, "price_sek_per_kwh", spot_at_bucket(_today, min_start * 4));

  cJSON* load = cJSON_AddObjectToObject(root, "consumption_pattern");
  cJSON_AddNumberToObject(load, "highest_consumption_slot_today", _P->today_peak_bucket);
  cJSON_AddNumberToObject(load, "lowest_consumption_slot_today", _P->today_low_bucket);
  cJSON_AddNumberToObject(load, "highest_consumption_slot_historical_average", _P->trend_peak_bucket);
  cJSON_AddNumberToObject(load, "lowest_consumption_slot_historical_average", _P->trend_low_bucket);
  cJSON_AddNumberToObject(load, "recommended_load_start_slot", _P->recommendation_start_bucket);
  cJSON_AddNumberToObject(load, "recommended_load_duration_minutes", PROFILE_WINDOW_BUCKETS * 15);

  cJSON* slots = cJSON_AddArrayToObject(root, "time_slots");
  for (int i = 0; i < PROFILE_BUCKETS; i++) {
    cJSON* item = cJSON_CreateObject();
    char start_time[6];
    char end_time[6];
    format_bucket_time(i, start_time, sizeof(start_time));
    format_bucket_time((i + 1) % PROFILE_BUCKETS, end_time, sizeof(end_time));
    cJSON_AddNumberToObject(item, "slot_index", i);
    cJSON_AddStringToObject(item, "start_time", start_time);
    cJSON_AddStringToObject(item, "end_time", end_time);
    cJSON_AddNumberToObject(item, "consumption_today_kwh", _P->today_kwh[i]);
    cJSON_AddNumberToObject(item, "consumption_historical_average_kwh", _P->trend_kwh[i]);
    cJSON_AddNumberToObject(item, "spot_price_today_sek_per_kwh", spot_at_bucket(_today, i));
    cJSON_AddNumberToObject(item, "spot_price_tomorrow_sek_per_kwh", spot_at_bucket(_tomorrow, i));
    cJSON_AddStringToObject(item, "spot_price_tomorrow_source",
                            tomorrow_spot_source_string(_tomorrow_spot_source));
    cJSON_AddItemToArray(slots, item);
  }

  char* json = cJSON_Print(root);
  cJSON_Delete(root);
  if (!json) {
    return ERR_JSON_PARSE;
  }

  int res = write_string_to_file(json, _filename) == 0 ? SUCCESS : ERR_FATAL;
  free(json);
  return res;
}

static int write_display_json_report(const Consumption_Profile* _P,
                                     const Electricity_Spots* _today,
                                     const Electricity_Spots* _tomorrow,
                                     const Facility_Config* _F,
                                     Tomorrow_Spot_Source _tomorrow_spot_source,
                                     time_t _updated_at, const char* _filename) {
  if (!_P || !_today || !_tomorrow || !_F || !_filename) {
    return ERR_INVALID_ARG;
  }

  cJSON* root = cJSON_CreateObject();
  if (!root) {
    return ERR_JSON_PARSE;
  }

  double avg_price = 0.0;
  for (unsigned int i = 0; i < _today->price_count; i++)
    avg_price += _today->prices[i].spot_price;
  if (_today->price_count > 0)
    avg_price /= _today->price_count;

  cJSON_AddStringToObject(root, "v", "od1");
  cJSON_AddStringToObject(root, "f", _F->name);
  cJSON_AddNumberToObject(root, "a", (int)_F->price_class + 1);
  cJSON_AddNumberToObject(root, "u", (double)_updated_at);
  cJSON_AddNumberToObject(root, "m", 15);
  cJSON_AddNumberToObject(root, "src", tomorrow_spot_source_code(_tomorrow_spot_source));

  cJSON* summary = cJSON_AddObjectToObject(root, "sum");
  cJSON_AddNumberToObject(summary, "ct", round_to_int(_P->today_total_kwh * 1000.0));
  cJSON_AddNumberToObject(summary, "cc", round_to_int(_P->today_cost_sek * 100.0));
  cJSON_AddNumberToObject(summary, "pt", round_to_int(_P->trend_total_kwh * 1000.0));
  cJSON_AddNumberToObject(summary, "pc", round_to_int(_P->predicted_cost_sek * 100.0));
  cJSON_AddNumberToObject(summary, "ap", round_to_int(avg_price * 1000.0));
  cJSON_AddNumberToObject(summary, "rs", _P->recommendation_start_bucket * 15);
  cJSON_AddNumberToObject(summary, "rd", PROFILE_WINDOW_BUCKETS * 15);

  cJSON* series = cJSON_AddObjectToObject(root, "s");
  cJSON* consumption_today = cJSON_AddArrayToObject(series, "c");
  cJSON* consumption_history = cJSON_AddArrayToObject(series, "h");
  cJSON* price_today = cJSON_AddArrayToObject(series, "p");
  cJSON* price_tomorrow = cJSON_AddArrayToObject(series, "q");

  for (int bucket = 0; bucket < PROFILE_BUCKETS; bucket++) {
    cJSON_AddItemToArray(consumption_today,
                         cJSON_CreateNumber(round_to_int(_P->today_kwh[bucket] * 1000.0)));
    cJSON_AddItemToArray(consumption_history,
                         cJSON_CreateNumber(round_to_int(_P->trend_kwh[bucket] * 1000.0)));
    cJSON_AddItemToArray(price_today,
                         cJSON_CreateNumber(round_to_int(spot_at_bucket(_today, bucket) * 1000.0)));
    cJSON_AddItemToArray(
        price_tomorrow, cJSON_CreateNumber(round_to_int(spot_at_bucket(_tomorrow, bucket) * 1000.0)));
  }

  char* json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!json) {
    return ERR_JSON_PARSE;
  }

  int res = write_string_to_file(json, _filename) == 0 ? SUCCESS : ERR_FATAL;
  free(json);
  return res;
}

static int write_txt_report(const Consumption_Profile* _P, const Electricity_Spots* _today,
                            const Facility_Config* _F,
                            Tomorrow_Spot_Source _tomorrow_spot_source,
                            const char* _filename) {
  FILE* file = fopen(_filename, "w");
  if (!file) {
    return ERR_FATAL;
  }

  int max_start = 0;
  int max_end = 0;
  int min_start = 0;
  int min_end = 0;
  minmax_price_windows(_today, &max_start, &max_end, &min_start, &min_end);

  double avg_price = 0.0;
  for (unsigned int i = 0; i < _today->price_count; i++)
    avg_price += _today->prices[i].spot_price;
  if (_today->price_count > 0)
    avg_price /= _today->price_count;

  char high_range[16];
  char low_range[16];
  char recommend_range[16];
  char trend_peak[6];
  char trend_low[6];
  format_hour_range(max_start, max_end, high_range, sizeof(high_range));
  format_hour_range(min_start, min_end, low_range, sizeof(low_range));
  format_bucket_range(_P->recommendation_start_bucket, PROFILE_WINDOW_BUCKETS, recommend_range,
                      sizeof(recommend_range));
  format_bucket_time(_P->trend_peak_bucket, trend_peak, sizeof(trend_peak));
  format_bucket_time(_P->trend_low_bucket, trend_low, sizeof(trend_low));

  fprintf(file, "Dagens spotpris i SE%d ar som hogst mellan kl %s (%.3f SEK/kWh), och som "
                "lagst mellan kl %s (%.3f SEK/kWh).\n",
          (int)_F->price_class + 1, high_range, spot_at_bucket(_today, max_start * 4),
          low_range, spot_at_bucket(_today, min_start * 4));
  fprintf(file, "Snittpriset for dygnet ar %.3f SEK/kWh.\n", avg_price);
  fprintf(file, "Dagens forbrukning hittills ar %.2f kWh och faktisk spotkostnad ar %.2f SEK.\n",
          _P->today_total_kwh, _P->today_cost_sek);
  fprintf(file,
          "Estimerad spotkostnad for morgondagen ar %.2f SEK baserat pa %.2f kWh historisk "
          "dygnsprofil och %s spotpriser.\n",
          _P->predicted_cost_sek, _P->trend_total_kwh,
          tomorrow_spot_source_string(_tomorrow_spot_source));
  fprintf(file,
          "Historisk peak ligger oftast runt kl %s och historisk dal runt kl %s. Maximal "
          "belastning rekommenderas mellan kl %s baserat pa spotpris och fastighetens "
          "historiska anvandningstrend.\n",
          trend_peak, trend_low, recommend_range);
  fprintf(file, "Identifierade peak-buckets idag: %d. Historikdagar: %d.\n\n", _P->detected_peaks,
          _P->history_days);

  fprintf(file, "%-5s | %10s | %14s | %12s\n", "Tid", "Idag kWh", "Hist avg kWh", "Spot SEK");
  fprintf(file, "-----------------------------------------------------\n");
  for (int i = 0; i < PROFILE_BUCKETS; i++) {
    char time_buf[6];
    format_bucket_time(i, time_buf, sizeof(time_buf));
    fprintf(file, "%-5s | %10.3f | %14.3f | %12.3f\n", time_buf, _P->today_kwh[i],
            _P->trend_kwh[i], spot_at_bucket(_today, i));
  }

  fclose(file);
  return SUCCESS;
}

static void log_report_written(const char* _filename, int _existed) {
  LOG_INFO("Consumption calculation %s %s", _filename, _existed ? "updated" : "created");
}

static int bucket_for_time(time_t _ts) {
  struct tm local;
  localtime_r(&_ts, &local);
  return (local.tm_hour * 4) + (local.tm_min / 15);
}

static double spot_at_bucket(const Electricity_Spots* _S, int _bucket) {
  if (!_S || !_S->prices || _S->price_count == 0) {
    return 0.0;
  }

  int hour = _bucket / 4;
  if (_S->price_count == 24) {
    if (hour >= 0 && hour < 24) {
      return _S->prices[hour].spot_price;
    }
  }

  unsigned int index = (unsigned int)((_bucket * (int)_S->price_count) / PROFILE_BUCKETS);
  if (index >= _S->price_count) {
    index = _S->price_count - 1;
  }
  return _S->prices[index].spot_price;
}

static int best_price_window(const Electricity_Spots* _S, const Consumption_Profile* _P) {
  int best_start = 0;
  double best_score = HUGE_VAL;

  for (int start = 0; start <= PROFILE_BUCKETS - PROFILE_WINDOW_BUCKETS; start++) {
    double score = 0.0;
    for (int i = 0; i < PROFILE_WINDOW_BUCKETS; i++) {
      int bucket = start + i;
      score += spot_at_bucket(_S, bucket) + (_P->trend_kwh[bucket] * 0.01);
    }
    if (score < best_score) {
      best_score = score;
      best_start = start;
    }
  }

  return best_start;
}

static void minmax_price_windows(const Electricity_Spots* _S, int* _max_start, int* _max_end,
                                 int* _min_start, int* _min_end) {
  *_max_start = 0;
  *_max_end = 1;
  *_min_start = 0;
  *_min_end = 1;
  if (!_S || !_S->prices || _S->price_count == 0) {
    return;
  }

  unsigned int max_idx = 0;
  unsigned int min_idx = 0;
  for (unsigned int i = 1; i < _S->price_count; i++) {
    if (_S->prices[i].spot_price > _S->prices[max_idx].spot_price)
      max_idx = i;
    if (_S->prices[i].spot_price < _S->prices[min_idx].spot_price)
      min_idx = i;
  }

  *_max_start = (int)max_idx;
  *_max_end = (int)max_idx + 1;
  *_min_start = (int)min_idx;
  *_min_end = (int)min_idx + 1;

  while (*_max_start > 0 &&
         fabs(_S->prices[*_max_start - 1].spot_price - _S->prices[max_idx].spot_price) < 0.0001)
    (*_max_start)--;
  while (*_max_end < (int)_S->price_count &&
         fabs(_S->prices[*_max_end].spot_price - _S->prices[max_idx].spot_price) < 0.0001)
    (*_max_end)++;
  while (*_min_start > 0 &&
         fabs(_S->prices[*_min_start - 1].spot_price - _S->prices[min_idx].spot_price) < 0.0001)
    (*_min_start)--;
  while (*_min_end < (int)_S->price_count &&
         fabs(_S->prices[*_min_end].spot_price - _S->prices[min_idx].spot_price) < 0.0001)
    (*_min_end)++;
}

static void format_bucket_time(int _bucket, char* _out, size_t _out_sz) {
  int minutes = _bucket * 15;
  snprintf(_out, _out_sz, "%02d:%02d", (minutes / 60) % 24, minutes % 60);
}

static void format_bucket_range(int _start, int _count, char* _out, size_t _out_sz) {
  char start[6];
  char end[6];
  format_bucket_time(_start, start, sizeof(start));
  format_bucket_time((_start + _count) % PROFILE_BUCKETS, end, sizeof(end));
  snprintf(_out, _out_sz, "%s-%s", start, end);
}

static void format_hour_range(int _start_hour, int _end_hour, char* _out, size_t _out_sz) {
  snprintf(_out, _out_sz, "%02d-%02d", _start_hour % 24, _end_hour % 24);
}

static char* report_name(const char* _dir, const char* _facility, const char* _ext, time_t _date) {
  char path[512];
  struct tm tm;
  localtime_r(&_date, &tm);
  char date[11];
  strftime(date, sizeof(date), "%Y-%m-%d", &tm);

  int len = snprintf(path, sizeof(path), "%s/%s-Consumption_%s.%s", _dir, _facility, date, _ext);
  if (len < 0 || (size_t)len >= sizeof(path)) {
    return NULL;
  }

  char* out = malloc((size_t)len + 1);
  if (!out) {
    return NULL;
  }
  memcpy(out, path, (size_t)len + 1);
  return out;
}

static char* display_report_name(const char* _dir, const char* _facility, time_t _date) {
  char path[512];
  struct tm tm;
  localtime_r(&_date, &tm);
  char date[11];
  strftime(date, sizeof(date), "%Y-%m-%d", &tm);

  int len = snprintf(path, sizeof(path), "%s/%s-Consumption_%s-display.json", _dir, _facility,
                     date);
  if (len < 0 || (size_t)len >= sizeof(path)) {
    return NULL;
  }

  char* out = malloc((size_t)len + 1);
  if (!out) {
    return NULL;
  }
  memcpy(out, path, (size_t)len + 1);
  return out;
}

static const char* tomorrow_spot_source_string(Tomorrow_Spot_Source _source) {
  switch (_source) {
  case TOMORROW_SPOTS_ACTUAL:
    return "actual";
  case TOMORROW_SPOTS_ESTIMATED_WITH_WEATHER:
    return "estimated_from_today_same_weekday_history_and_weather";
  case TOMORROW_SPOTS_ESTIMATED_FROM_PRICE_HISTORY:
    return "estimated_from_today_and_same_weekday_history";
  case TOMORROW_SPOTS_FALLBACK_TODAY:
    return "fallback_today";
  default:
    return "unknown";
  }
}

static int tomorrow_spot_source_code(Tomorrow_Spot_Source _source) {
  switch (_source) {
  case TOMORROW_SPOTS_ACTUAL:
    return 0;
  case TOMORROW_SPOTS_ESTIMATED_WITH_WEATHER:
    return 1;
  case TOMORROW_SPOTS_ESTIMATED_FROM_PRICE_HISTORY:
    return 2;
  case TOMORROW_SPOTS_FALLBACK_TODAY:
    return 3;
  default:
    return -1;
  }
}

static int round_to_int(double _value) {
  if (_value >= 0.0) {
    return (int)(_value + 0.5);
  }
  return (int)(_value - 0.5);
}
