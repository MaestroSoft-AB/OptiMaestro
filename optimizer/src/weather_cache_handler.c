#include "weather_cache_handler.h"
#include "data/weather_structs.h"
#include "maestroutils/error.h"
#include "maestroutils/time_utils.h"
#include "maestroutils/file_utils.h"
#include "maestroutils/time_utils.h"
#define MAESTROUTILS_WITH_CJSON 1 // get rid of stupid lsp error
#include "maestroutils/json_utils.h"
#include "maestroutils/file_logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --------------------------- Internal --------------------------- */

const char* wch_get_cache_filepath(const char* _base_path,
                                   time_t      _start_date,
                                   bool        _forecast);

int wch_validate_cache(const char* _cache_path);

// int wch_parse_json(Weather* _W, const char* _json_str);

int wch_write_cache_json(const Weather* _Weather, const char* _cache_path);

/* ---------------------------------------------------------------- */

int wch_init(WCH* _WCH, const WCH_Conf* _Conf)
{
  memset(_WCH, 0, sizeof(WCH));
  memset(&_WCH->weather, 0, sizeof(Weather));

  printf("WCH data dir: %s\n", _Conf->data_dir);

  if (!_Conf->data_dir) {
    create_directory_if_not_exists(WCH_BASE_CACHE_PATH_FALLBACK);
    /* size_t path_len = strlen(WCH_BASE_CACHE_PATH_FALLBACK);
    _WCH->conf.data_dir = malloc(path_len + 1);
    if (!_WCH->conf.data_dir) {
      LOG_ERROR("malloc");
      return ERR_NO_MEMORY;
    }
    memcpy(_WCH->conf.data_dir) */

  }
  else {
    create_directory_if_not_exists(_Conf->data_dir);
    _WCH->conf.data_dir = _Conf->data_dir;
  }

  _WCH->conf.latitude = _Conf->latitude;
  _WCH->conf.longitude = _Conf->longitude;

  return SUCCESS;
}

int wch_update_cache(WCH* _WCH)
{
  LOG_INFO("Updating weather cache...\r\n");

  /* Set conf weather vars */
  _WCH->weather.latitude  = _WCH->conf.latitude; 
  _WCH->weather.longitude = _WCH->conf.longitude; 

  /* Free previous path allocation if exists */
  if (_WCH->data_path != NULL)
    free((void*)_WCH->data_path);

  /* Define cache path */
  time_t today = epoch_now_day();
  // time_t tmrw  = today + 86400;

  if (!_WCH->conf.data_dir)  // fallback data dir
    _WCH->data_path = wch_get_cache_filepath(WCH_BASE_CACHE_PATH_FALLBACK, 
        today, _WCH->conf.forecast);
  else
    _WCH->data_path = wch_get_cache_filepath(_WCH->conf.data_dir, 
        today, _WCH->conf.forecast);
  
  if (!_WCH->data_path) {
    perror("wch_set_cache_filepath");
    return ERR_FATAL;
  }

  printf("WCH data_path: %s\r\n", _WCH->data_path);

  /* TODO: Caching+validating - Ideally not just based on cache name 
   * since we don't want files that overlap in data 
   * Maybe just start on implementing HDF5/sqlite tbh */

  /* Get weather data from external API */
  /* TODO: better interval handling and ability to get specific date range 
   * (so we can create a historical archive) */
  if (_WCH->conf.forecast) {
    if (meteo_get_15_minutely(&_WCH->weather, 
          _WCH->conf.latitude, 
          _WCH->conf.longitude) != 0) {
      perror("meteo_get_15_minutes");
      return ERR_INTERNAL;
    }
  }
  else {
    if (meteo_get_current(&_WCH->weather, _WCH->conf.latitude, _WCH->conf.longitude) != 0) {
      perror("meteo_get_15_current");
      return ERR_INTERNAL;
    }
  }

  printf("Weather Cache updated! Example:\r\n");
  printf("time=%lu   radiation_DHI=%f%s   temp=%f %s\r\n", _WCH->weather.values[0].timestamp, _WCH->weather.values[0].radiation_diffuse, _WCH->weather.radiation_unit, _WCH->weather.values[0].temperature, _WCH->weather.temperature_unit);

  /* Write parsed weather to cache */
  if (wch_write_cache_json(&_WCH->weather, _WCH->data_path) != 0) {
    perror("wch_write_cache_json");
    return ERR_INTERNAL;
  }

  return SUCCESS;
}

/* Define and return the cache path from given parameters */
/* TODO: create recursive directories for year/month */
const char* wch_get_cache_filepath(const char*    _base_path,
                                   time_t         _start_date,
                                   bool           _forecast)
{
  char path_buf[512];

  /* Set timestamp */
  const char* date_str = parse_epoch_to_iso_datetime_string(&_start_date);
  if (date_str == NULL)
    return NULL;

  /* Build path name */
  size_t path_len;
  if (_forecast) {
    path_len = snprintf(path_buf, sizeof(path_buf), 
            "%s/forecast-%s.json",
            _base_path,
            date_str);
  } 
  else {
    path_len = snprintf(path_buf, sizeof(path_buf), 
            "%s/current.json",
            _base_path); 
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
      LOG_ERROR("cJSON: %s\n", err); //TODO: Logger
    return ERR_JSON_PARSE;
  }

  /* Create meta info object */
  cJSON* Json_Meta = cJSON_CreateObject();

  cJSON_AddNumberToObject(Json_Meta, "interval_minutes", _Weather->update_interval);
  cJSON_AddNumberToObject(Json_Meta, "latitude", _Weather->latitude);
  cJSON_AddNumberToObject(Json_Meta, "longitude", _Weather->longitude);

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
    cJSON_AddNumberToObject(Json_Object, "radiation_titled", Vals.radiation_tilted);
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

void wch_dispose(WCH* _WCH)
{
  if (_WCH->data_path != NULL)
    free((void*)_WCH->data_path);

  if (_WCH->weather.temperature_unit    != NULL)
    free((void*)_WCH->weather.temperature_unit);
  if (_WCH->weather.windspeed_unit      != NULL)
    free((void*)_WCH->weather.windspeed_unit);
  if (_WCH->weather.precipitation_unit  != NULL)
    free((void*)_WCH->weather.precipitation_unit);
  if (_WCH->weather.winddirection_unit  != NULL)
    free((void*)_WCH->weather.winddirection_unit);
  if (_WCH->weather.radiation_unit      != NULL)
    free((void*)_WCH->weather.radiation_unit);

  if (_WCH->weather.values != NULL)
    free(_WCH->weather.values);

  memset(_WCH, 0, sizeof(WCH));

  _WCH = NULL;
}
