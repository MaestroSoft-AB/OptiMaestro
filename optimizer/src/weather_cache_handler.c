#include "weather_cache_handler.h"
#include "data/weather_structs.h"
#include "maestroutils/error.h"
#include "maestroutils/file_utils.h"
#include "maestroutils/time_utils.h"
#define MAESTROUTILS_WITH_CJSON 1 // get rid of stupid lsp error
#include "maestroutils/file_logging.h"
#include "maestroutils/json_utils.h"
#include "sqlite_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --------------------------- Internal --------------------------- */

/* const char* wch_get_cache_filepath(const char* _base_path,
                                   time_t      _start_date,
                                   bool        _forecast); */

int wch_validate_cache(const char* _cache_path);

// int wch_parse_json(Weather* _W, const char* _json_str);

int wch_write_cache_json(const Weather* _Weather, const char* _cache_path);

/* ---------------------------------------------------------------- */

int wch_init(WCH* _WCH, const WCH_Conf* _Conf)
{
  memset(_WCH, 0, sizeof(WCH));
  memset(&_WCH->weather, 0, sizeof(Weather));

  // if (!_Conf->data_dir) {
  //   LOG_WARN("No weather cache dir found, setting fallback: %s", WCH_BASE_CACHE_PATH_FALLBACK);
  //   create_directory_if_not_exists(WCH_BASE_CACHE_PATH_FALLBACK);
  // } else {
  //   create_directory_if_not_exists(_Conf->data_dir);
  //   _WCH->conf.data_dir = _Conf->data_dir;
  // }

  _WCH->conf.forecast = _Conf->forecast;
  _WCH->conf.latitude = _Conf->latitude;
  _WCH->conf.longitude = _Conf->longitude;
  _WCH->conf.panel_azimuth = _Conf->panel_azimuth;
  _WCH->conf.panel_tilt = _Conf->panel_tilt;
  _WCH->conf.sqlhelper = _Conf->sqlhelper;

  return SUCCESS;
}

int wch_update_cache(WCH* _WCH)
{
  LOG_INFO("Updating weather cache...\r\n");

  _WCH->weather.latitude = _WCH->conf.latitude;
  _WCH->weather.longitude = _WCH->conf.longitude;
  _WCH->weather.panel_tilt = _WCH->conf.panel_tilt;
  _WCH->weather.panel_azimuth = _WCH->conf.panel_azimuth;

  time_t today = epoch_now_day();
  time_t tmrw = today + 86400;

  time_t target_day = today;
  int update_hour = 13;

  if (time_is_at_or_after_hour(update_hour)) {
    target_day = tmrw;
  } else {
    target_day = today;
  }
  time_t range_start = target_day - 3600;
  time_t range_end = range_start + 86400;

  if (_WCH->conf.forecast) {
    if (meteo_get_15_minutely(&_WCH->weather, (float)_WCH->conf.latitude,
                              (float)_WCH->conf.longitude, (float)_WCH->conf.panel_azimuth,
                              (float)_WCH->conf.panel_tilt, range_start, range_end) != 0) {
      LOG_ERROR("meteo_get_15_minutely");
      return ERR_INTERNAL;
    }
  } else {
    if (meteo_get_current(&_WCH->weather, (float)_WCH->conf.latitude, (float)_WCH->conf.longitude,
                          (float)_WCH->conf.panel_azimuth, (float)_WCH->conf.panel_tilt) != 0) {
      LOG_ERROR("meteo_get_current");
      return ERR_INTERNAL;
    }
  }

  _WCH->weather.latitude = _WCH->conf.latitude;
  _WCH->weather.longitude = _WCH->conf.longitude;
  _WCH->weather.panel_tilt = _WCH->conf.panel_tilt;
  _WCH->weather.panel_azimuth = _WCH->conf.panel_azimuth;

  int res = sql_helper_insert_weather(_WCH->conf.sqlhelper, &_WCH->weather, _WCH->conf.forecast);
  if (res != SUCCESS) {
    LOG_ERROR("sql_helper_insert_weather (%i)", res);
    return res;
  }

  return SUCCESS;
}

/* Define and return the cache path from given parameters */
/* TODO: create recursive directories for year/month */
char* wch_get_cache_json_filepath(const char* _base_path, time_t _start_date, bool _forecast)
{
  char path_buf[512];

  /* Set timestamp */
  const char* date_str = parse_epoch_to_iso_datetime_string(&_start_date);
  if (date_str == NULL)
    return NULL;

  /* Build path name */
  size_t path_len;
  if (_forecast) {
    path_len = snprintf(path_buf, sizeof(path_buf), "%s/forecast-%s.json", _base_path, date_str);
  } else {
    path_len = snprintf(path_buf, sizeof(path_buf), "%s/current.json", _base_path);
  }

  free((void*)date_str);

  char* path = malloc(path_len + 1);
  if (!path)
    return NULL;

  memcpy(path, path_buf, path_len);
  path[path_len] = '\0';

  return path;
}

int wch_validate_cache(const char* _cache_path)
{
  if (!_cache_path)
    return ERR_INVALID_ARG;

  if (file_exists(_cache_path)) {
    // TODO: Actually validate something?
    return SUCCESS;
  }
  return ERR_NOT_FOUND;
}

int wch_write_cache_json(const Weather* _Weather, const char* _cache_path)
{
  if (!_Weather || !_cache_path)
    return ERR_INVALID_ARG;

  cJSON* Json_Root = cJSON_CreateObject();
  if (Json_Root == NULL) {
    const char* err = cJSON_GetErrorPtr();
    if (err != NULL)
      LOG_ERROR("cJSON: %s\n", err); // TODO: Logger
    return ERR_JSON_PARSE;
  }

  /* Create meta info object */
  cJSON* Json_Meta = cJSON_CreateObject();

  cJSON_AddNumberToObject(Json_Meta, "interval_minutes", _Weather->update_interval);
  cJSON_AddNumberToObject(Json_Meta, "latitude", _Weather->latitude);
  cJSON_AddNumberToObject(Json_Meta, "longitude", _Weather->longitude);
  cJSON_AddNumberToObject(Json_Meta, "solar_panel_azimuth", _Weather->panel_azimuth);
  cJSON_AddNumberToObject(Json_Meta, "solar_panel_tilt", _Weather->panel_tilt);

  cJSON_AddStringToObject(Json_Meta, "temperature_unit", _Weather->temperature_unit);
  cJSON_AddStringToObject(Json_Meta, "windspeed_unit", _Weather->windspeed_unit);
  cJSON_AddStringToObject(Json_Meta, "precipitation_unit", _Weather->precipitation_unit);
  cJSON_AddStringToObject(Json_Meta, "winddirection_unit", _Weather->winddirection_unit);
  cJSON_AddStringToObject(Json_Meta, "radiation_unit", _Weather->radiation_unit);

  cJSON_AddItemToObject(Json_Root, "meta", Json_Meta);

  /* Create values array to contain data for the day */
  cJSON* Json_Values = cJSON_AddArrayToObject(Json_Root, "values");

  unsigned int i;
  for (i = 0; i < _Weather->count; i++) {
    Weather_Values Vals = _Weather->values[i];
    cJSON* Json_Object = cJSON_CreateObject();

    const char* timestamp = parse_epoch_to_iso_full_datetime_string(&Vals.timestamp, 0);
    cJSON_AddStringToObject(Json_Object, "timestamp", timestamp);
    free((void*)timestamp);

    cJSON_AddNumberToObject(Json_Object, "temperature", Vals.temperature);
    cJSON_AddNumberToObject(Json_Object, "windspeed", Vals.windspeed);
    cJSON_AddNumberToObject(Json_Object, "winddirection", Vals.winddirection_azimuth);
    cJSON_AddNumberToObject(Json_Object, "precipitation", Vals.precipitation);
    cJSON_AddNumberToObject(Json_Object, "radiation_direct", Vals.radiation_direct);
    cJSON_AddNumberToObject(Json_Object, "radiation_direct_n", Vals.radiation_direct_n);
    cJSON_AddNumberToObject(Json_Object, "radiation_diffuse", Vals.radiation_diffuse);
    cJSON_AddNumberToObject(Json_Object, "radiation_shortwave", Vals.radiation_shortwave);
    cJSON_AddNumberToObject(Json_Object, "radiation_tilted", Vals.radiation_tilted);
    cJSON_AddNumberToObject(Json_Object, "sun_duration", Vals.sun_duration);

    cJSON_AddItemToArray(Json_Values, Json_Object);
  }

  char* json_str = cJSON_Print(Json_Root);

  LOG_INFO("\r\n--- WCH writing to %s ---\r\n", _cache_path);

  if (write_string_to_file(json_str, _cache_path) != 0)
    fprintf(stderr, "Failed to write string \"%p\" to cache \"%p\"\n", json_str, _cache_path);

  free(json_str);

  cJSON_Delete(Json_Root);

  return SUCCESS;
}

int wch_read_cache_json(Weather* _W, const char* _cache_path)
{
  if (!_W || !_cache_path)
    return ERR_INVALID_ARG;

  char* json_str = read_file_to_string(_cache_path);
  if (json_str == NULL)
    return ERR_FATAL;

  cJSON* Json_Root = cJSON_Parse(json_str);
  if (Json_Root == NULL) {
    const char* err = cJSON_GetErrorPtr();
    if (err != NULL)
      LOG_ERROR("cJSON: %s\n", err);
    free(json_str);
    return ERR_JSON_PARSE;
  }

  if (!cJSON_IsObject(Json_Root)) {
    LOG_ERROR("cJSON: root is not an object\n");
    cJSON_Delete(Json_Root);
    free(json_str);
    return ERR_JSON_PARSE;
  }

  cJSON* Json_Meta = cJSON_GetObjectItemCaseSensitive(Json_Root, "meta");
  if (Json_Meta && cJSON_IsObject(Json_Meta)) {
    _W->update_interval = json_get_int(Json_Meta, "interval_minutes");
    _W->latitude = json_get_double(Json_Meta, "latitude");
    _W->longitude = json_get_double(Json_Meta, "longitude");
    _W->panel_azimuth = json_get_double(Json_Meta, "solar_panel_azimuth");
    _W->panel_tilt = json_get_double(Json_Meta, "solar_panel_tilt");

    _W->temperature_unit = strdup(json_get_string(Json_Meta, "temperature_unit"));
    _W->windspeed_unit = strdup(json_get_string(Json_Meta, "windspeed_unit"));
    _W->precipitation_unit = strdup(json_get_string(Json_Meta, "precipitation_unit"));
    _W->winddirection_unit = strdup(json_get_string(Json_Meta, "winddirection_unit"));
    _W->radiation_unit = strdup(json_get_string(Json_Meta, "radiation_unit"));
  }

  cJSON* Json_Values = cJSON_GetObjectItemCaseSensitive(Json_Root, "values");
  if (cJSON_IsArray(Json_Values)) {
    unsigned int count = (unsigned int)cJSON_GetArraySize(Json_Values);
    _W->count = count;

    if (_W->values != NULL)
      free(_W->values); // free if still allocated

    _W->values = calloc(1, (sizeof(Weather_Values) * count));
    if (!_W->values) {
      LOG_ERROR("calloc");
      return ERR_NO_MEMORY;
    }

    for (unsigned int i = 0; i < count; ++i) {
      cJSON* Json_Object = cJSON_GetArrayItem(Json_Values, i);
      if (!cJSON_IsObject(Json_Object))
        continue;

      Weather_Values Vals;
      memset(&Vals, 0, sizeof(Vals));

      const char* timestamp_str = json_get_string(Json_Object, "timestamp");
      if (timestamp_str && strcmp(timestamp_str, "Unknown") != 0)
        Vals.timestamp = parse_iso_full_datetime_string_to_epoch(timestamp_str);

      Vals.temperature = json_get_double(Json_Object, "temperature");
      Vals.windspeed = json_get_double(Json_Object, "windspeed");
      Vals.winddirection_azimuth = json_get_double(Json_Object, "winddirection");
      Vals.precipitation = json_get_double(Json_Object, "precipitation");
      Vals.radiation_direct = json_get_double(Json_Object, "radiation_direct");
      Vals.radiation_direct_n = json_get_double(Json_Object, "radiation_direct_n");
      Vals.radiation_diffuse = json_get_double(Json_Object, "radiation_diffuse");
      Vals.radiation_shortwave = json_get_double(Json_Object, "radiation_shortwave");
      Vals.radiation_tilted = json_get_double(Json_Object, "radiation_tilted");
      Vals.sun_duration = json_get_double(Json_Object, "sun_duration");

      _W->values[i] = Vals;
    }
  }

  cJSON_Delete(Json_Root);
  free(json_str);

  return SUCCESS;
}

int wch_get_weather_range(SqlHelper* _H, Weather* _W, double _latitude, double _longitude,
                          int _panel_tilt, unsigned int _panel_azimuth, bool _forecast,
                          time_t _start, time_t _end)
{
  if (!_H || !_W) {
    return ERR_INVALID_ARG;
  }

  return sql_helper_read_weather(_H, _W, _latitude, _longitude, _panel_tilt, _panel_azimuth,
                                 _forecast, _start, _end);
}

void wch_weather_dispose(Weather* _W)
{
  if (_W != NULL) {
    if (_W->temperature_unit != NULL)
      free((void*)_W->temperature_unit);
    if (_W->windspeed_unit != NULL)
      free((void*)_W->windspeed_unit);
    if (_W->precipitation_unit != NULL)
      free((void*)_W->precipitation_unit);
    if (_W->winddirection_unit != NULL)
      free((void*)_W->winddirection_unit);
    if (_W->radiation_unit != NULL)
      free((void*)_W->radiation_unit);

    if (_W->values != NULL)
      free(_W->values);
  }
}

void wch_dispose(WCH* _WCH)
{
  if (_WCH->data_path != NULL)
    free((void*)_WCH->data_path);

  wch_weather_dispose(&_WCH->weather);

  memset(_WCH, 0, sizeof(WCH));

  _WCH = NULL;
}
