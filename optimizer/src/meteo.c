#include "meteo.h"
#include "cJSON.h"
#include "data/electricity_structs.h"
#include "data/weather_structs.h"
#define MAESTROUTILS_WITH_CJSON 1 // get rid of stupid lsp error
#include <maestromodules/http_client.h>
#include <maestroutils/error.h>
#include <maestroutils/file_logging.h>
#include <maestroutils/json_utils.h>
#include <maestroutils/time_utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------- Internal --------------------------- */

/*------------------------------------------------------------------*/
#define METEO_MAX_URL_LEN 512

typedef struct
{
  float lat;
  float lon;
  time_t startdate;
  time_t enddate;

} Meteo_Request;

const char* meteo_get_response_json(const char* _url);

int meteo_parse_forecast(const char* _json, Weather* _Weather, char* _interval);
int meteo_parse_current(const char* _json, Weather* _Weather, char* _interval);

/* ---------------------------------------------------------------- */

int meteo_get_hourly(Weather* _Weather, float _lat, float _lon, float _panel_azimuth,
                     float _panel_tilt, time_t _start_date, time_t _end_date)
{
  if (!_Weather)
    return ERR_INVALID_ARG;

  if (_Weather->values != NULL)
    free(_Weather->values); // free if still allocated for some reason

  return SUCCESS;
}

int meteo_get_15_minutely(Weather* _Weather, float _lat, float _lon, float _panel_azimuth,
                          float _panel_tilt, time_t _start_date, time_t _end_date)
{
  if (!_Weather)
    return ERR_INVALID_ARG;

  char interval[] = "minutely_15";
  _Weather->update_interval = 15;

  /* Define URL */
  char url_buf[METEO_MAX_URL_LEN];

  printf("lat : %f lon: %f, azimuth: %f, tilt: %f\n", _lat, _lon, (float)_Weather->panel_azimuth,
         (float)_Weather->panel_tilt);

  char start_date[11];
  char end_date[11];

  time_t api_start = _start_date - 86400;
  time_t api_end = _end_date;

  epoch_to_utc_date_string(api_start, start_date, sizeof(start_date));
  epoch_to_utc_date_string(api_end, end_date, sizeof(end_date));

  size_t url_len = snprintf(url_buf, METEO_MAX_URL_LEN,
                            "http://api.open-meteo.com/v1/forecast"
                            "?latitude=%f"
                            "&longitude=%f"
                            "&timezone=GMT"
                            "&azimuth=%f"
                            "&tilt=%f"
                            "&%s"
                            "&start_date=%s"
                            "&end_date=%s",
                            _lat, _lon, _panel_azimuth, _panel_tilt, METEO_15_MINUTELY_DATE_QUERY,
                            start_date, end_date);

  char* url = malloc((url_len + 1) * sizeof(char));
  if (url == NULL) {
    LOG_ERROR("malloc");
    return ERR_NO_MEMORY;
  }

  strncpy(url, url_buf, url_len);
  url[url_len] = '\0';

  /* Get json string */
  const char* response_json = meteo_get_response_json(url);
  if (response_json == NULL) {
    free(url);
    LOG_ERROR("meteo_get_response_json");
    return ERR_INTERNAL;
  }
  free(url);

  /* Parse json to weather struct */
  if (meteo_parse_forecast(response_json, _Weather, interval) != 0) {
    free((void*)response_json);
    LOG_ERROR("meteo_parse_forecast");
    return ERR_INTERNAL;
  }

  free((void*)response_json);

  return SUCCESS;
}

int meteo_get_current(Weather* _Weather, float _lat, float _lon, float _panel_azimuth,
                      float _panel_tilt)
{
  if (!_Weather)
    return ERR_INVALID_ARG;

  char interval[] = "current";
  _Weather->update_interval = 15;

  /* Define URL */
  char url_buf[METEO_MAX_URL_LEN];

  size_t url_len = snprintf(url_buf, METEO_MAX_URL_LEN, METEO_BASE_URL, _lat, _lon, _panel_azimuth,
                            _panel_tilt, METEO_CURRENT_QUERY);

  char* url = malloc((url_len + 1) * sizeof(char));
  if (url == NULL) {
    LOG_ERROR("malloc");
    return ERR_NO_MEMORY;
  }

  strncpy(url, url_buf, url_len);
  url[url_len] = '\0';

  /* Get json string */
  const char* response_json = meteo_get_response_json(url);
  if (response_json == NULL) {
    free(url);
    LOG_ERROR("meteo_get_response_json");
    return ERR_INTERNAL;
  }
  free(url);

  /* Parse json to weather struct */
  if (meteo_parse_current(response_json, _Weather, interval) != 0) {
    free((void*)response_json);
    LOG_ERROR("meteo_parse_forecast");
    return ERR_INTERNAL;
  }
  free((void*)response_json);

  return SUCCESS;
}

/* VALID open-meteo _intervals:
 * 15_minutely
 * hourly
 * daily*/
int meteo_parse_forecast(const char* _json, Weather* _Weather, char* _interval)
{
  if (!_Weather)
    return ERR_INVALID_ARG;

  if (_Weather->values != NULL)
    free(_Weather->values); // free if allocated for some reason

  cJSON* Json_Root = cJSON_Parse(_json);
  if (Json_Root == NULL) {
    const char* err = cJSON_GetErrorPtr();
    if (err != NULL)
      LOG_ERROR("cJSON: %s\n", err); // TODO: Logger
    return ERR_JSON_PARSE;
  }
  _Weather->latitude = json_get_double(Json_Root, "latitude");
  _Weather->longitude = json_get_double(Json_Root, "longitude");

  /* The two main bodies in meteo response */
  cJSON* Forecast_Values = cJSON_GetObjectItemCaseSensitive(Json_Root, _interval);
  if (Forecast_Values == NULL) {
    LOG_ERROR("cJSON_GetObjectItemCaseSensitive"); // TODO: Logger
    cJSON_Delete(Json_Root);
    return ERR_JSON_OBJ_NOT_FOUND;
  }

  char units_name[64];
  snprintf(units_name, sizeof(units_name), "%s_units", _interval);

  cJSON* Forecast_Units = cJSON_GetObjectItemCaseSensitive(Json_Root, units_name);
  if (Forecast_Units == NULL) {
    LOG_ERROR("cJSON_GetObjectItemCaseSensitive"); // TODO: Logger
    cJSON_Delete(Json_Root);
    return ERR_JSON_OBJ_NOT_FOUND;
  }

  /* Get units */
  _Weather->temperature_unit = strdup(json_get_string(Forecast_Units, "temperature_2m"));
  _Weather->windspeed_unit = strdup(json_get_string(Forecast_Units, "wind_speed_10m"));
  _Weather->precipitation_unit = strdup(json_get_string(Forecast_Units, "precipitation"));
  _Weather->winddirection_unit = strdup(json_get_string(Forecast_Units, "wind_direction_10m"));
  _Weather->radiation_unit = strdup(json_get_string(
      Forecast_Units, "direct_radiation")); // this should be same for all radiation readings
  _Weather->sun_duration_unit = 's';        // can't be bothered, shoot me if it isn't 's' one day

  /* Parse the the first array to get count*/
  cJSON* Time = cJSON_GetObjectItemCaseSensitive(Forecast_Values, "time");
  int arr_c = cJSON_GetArraySize(Time);

  /* Allocate values for as many array items */
  Weather_Values* Vals = calloc(1, (arr_c * sizeof(Weather_Values)));
  if (!Vals) {
    LOG_ERROR("calloc"); // TODO: Logger
    cJSON_Delete(Json_Root);
    return ERR_NO_MEMORY;
  }
  _Weather->count = arr_c;

  /* Define the values arrays */
  cJSON* Tmprtur = cJSON_GetObjectItemCaseSensitive(Forecast_Values, "temperature_2m");
  cJSON* Precptn = cJSON_GetObjectItemCaseSensitive(Forecast_Values, "precipitation");
  cJSON* Wnd_Spd = cJSON_GetObjectItemCaseSensitive(Forecast_Values, "wind_speed_10m");
  cJSON* Wnd_Dir = cJSON_GetObjectItemCaseSensitive(Forecast_Values, "wind_direction_10m");
  cJSON* Sun_Dur = cJSON_GetObjectItemCaseSensitive(Forecast_Values, "sunshine_duration");
  cJSON* Rad_Dir = cJSON_GetObjectItemCaseSensitive(Forecast_Values, "direct_radiation");
  cJSON* Rad_DNI = cJSON_GetObjectItemCaseSensitive(Forecast_Values, "direct_normal_irradiance");
  cJSON* Rad_DHI = cJSON_GetObjectItemCaseSensitive(Forecast_Values, "diffuse_radiation");
  cJSON* Rad_GHI = cJSON_GetObjectItemCaseSensitive(Forecast_Values, "shortwave_radiation");
  cJSON* Rad_GTI = cJSON_GetObjectItemCaseSensitive(Forecast_Values, "global_tilted_irradiance");

  for (int i = 0; i < arr_c; i++) {

    cJSON* Time_i = cJSON_GetArrayItem(Time, i);
    if (cJSON_IsString(Time_i)) {
      time_t stamp = parse_iso_datetime_string_to_epoch(Time_i->valuestring);
      if (stamp > 0)
        Vals[i].timestamp = stamp;
    }

    cJSON* Tmprtur_i = cJSON_GetArrayItem(Tmprtur, i);
    if (cJSON_IsNumber(Tmprtur_i))
      Vals[i].temperature = Tmprtur_i->valuedouble;

    cJSON* Precptn_i = cJSON_GetArrayItem(Precptn, i);
    if (cJSON_IsNumber(Precptn_i))
      Vals[i].precipitation = Precptn_i->valuedouble;

    cJSON* Wnd_Spd_i = cJSON_GetArrayItem(Wnd_Spd, i);
    if (cJSON_IsNumber(Wnd_Spd_i))
      Vals[i].windspeed = Wnd_Spd_i->valuedouble;

    cJSON* Wnd_Dir_i = cJSON_GetArrayItem(Wnd_Dir, i);
    if (cJSON_IsNumber(Wnd_Dir_i))
      Vals[i].winddirection_azimuth = Wnd_Dir_i->valueint;

    cJSON* Sun_Dur_i = cJSON_GetArrayItem(Sun_Dur, i);
    if (cJSON_IsNumber(Sun_Dur_i))
      Vals[i].sun_duration = Sun_Dur_i->valueint;

    cJSON* Rad_Dir_i = cJSON_GetArrayItem(Rad_Dir, i);
    if (cJSON_IsNumber(Rad_Dir_i))
      Vals[i].radiation_direct = Rad_Dir_i->valuedouble;

    cJSON* Rad_DNI_i = cJSON_GetArrayItem(Rad_DNI, i);
    if (cJSON_IsNumber(Rad_DNI_i))
      Vals[i].radiation_direct_n = Rad_DNI_i->valuedouble;

    cJSON* Rad_GHI_i = cJSON_GetArrayItem(Rad_GHI, i);
    if (cJSON_IsNumber(Rad_GHI_i))
      Vals[i].radiation_shortwave = Rad_GHI_i->valuedouble;

    cJSON* Rad_DHI_i = cJSON_GetArrayItem(Rad_DHI, i);
    if (cJSON_IsNumber(Rad_DHI_i))
      Vals[i].radiation_diffuse = Rad_DHI_i->valuedouble;

    cJSON* Rad_GTI_i = cJSON_GetArrayItem(Rad_GTI, i);
    if (cJSON_IsNumber(Rad_GTI_i))
      Vals[i].radiation_tilted = Rad_GTI_i->valuedouble;
  }

  _Weather->values = Vals;

  cJSON_Delete(Json_Root);

  return SUCCESS;
}

int meteo_parse_current(const char* _json, Weather* _Weather, char* _interval)
{
  if (!_Weather)
    return ERR_INVALID_ARG;

  if (_Weather->values != NULL)
    free(_Weather->values); // free if allocated for some reason

  cJSON* Json_Root = cJSON_Parse(_json);
  if (Json_Root == NULL) {
    const char* err = cJSON_GetErrorPtr();
    if (err != NULL)
      LOG_ERROR("cJSON: %s\n", err); // TODO: Logger
    return ERR_JSON_PARSE;
  }
  _Weather->latitude = json_get_double(Json_Root, "latitude");
  _Weather->longitude = json_get_double(Json_Root, "longitude");

  /* The two main bodies in meteo response */
  cJSON* Current_Values = cJSON_GetObjectItemCaseSensitive(Json_Root, _interval);
  if (Current_Values == NULL) {
    LOG_ERROR("cJSON_GetObjectItemCaseSensitive"); // TODO: Logger
    cJSON_Delete(Json_Root);
    return ERR_JSON_OBJ_NOT_FOUND;
  }

  char units_name[64];
  snprintf(units_name, sizeof(units_name), "%s_units", _interval);
  cJSON* Current_Units = cJSON_GetObjectItemCaseSensitive(Json_Root, units_name);
  if (Current_Units == NULL) {
    LOG_ERROR("cJSON_GetObjectItemCaseSensitive"); // TODO: Logger
    cJSON_Delete(Json_Root);
    return ERR_JSON_OBJ_NOT_FOUND;
  }

  /* Allocate values for as many array items */
  Weather_Values* Vals = calloc(1, sizeof(Weather_Values));
  if (!Vals) {
    LOG_ERROR("calloc"); // TODO: Logger
    cJSON_Delete(Json_Root);
    return ERR_NO_MEMORY;
  }
  _Weather->count = 1;

  /* Get units */
  _Weather->temperature_unit = strdup(json_get_string(Current_Units, "temperature_2m"));
  _Weather->windspeed_unit = strdup(json_get_string(Current_Units, "wind_speed_10m"));
  _Weather->precipitation_unit = strdup(json_get_string(Current_Units, "precipitation"));
  _Weather->winddirection_unit = strdup(json_get_string(Current_Units, "wind_direction_10m"));
  _Weather->radiation_unit = strdup(json_get_string(
      Current_Units, "direct_radiation")); // this should be same for all radiation readings
  _Weather->sun_duration_unit = 's';       // can't be bothered, shoot me if it isn't 's' one day

  /* Define the values arrays */
  char timestamp[17];
  memcpy(timestamp, json_get_string(Current_Values, "time"), 16);
  timestamp[16] = '\0';
  Vals->timestamp = parse_iso_datetime_string_to_epoch(timestamp);

  Vals->temperature = json_get_double(Current_Values, "temperature_2m");
  Vals->precipitation = json_get_double(Current_Values, "precipitation");
  Vals->windspeed = json_get_double(Current_Values, "wind_speed_10m");
  Vals->winddirection_azimuth = json_get_int(Current_Values, "wind_direction_10m");
  Vals->sun_duration = json_get_int(Current_Values, "sunshine_duration");

  Vals->radiation_tilted = json_get_double(Current_Values, "global_tilted_irradiance");
  Vals->radiation_direct_n = json_get_double(Current_Values, "direct_normal_irradiance");
  Vals->radiation_direct = json_get_double(Current_Values, "direct_radiation");
  Vals->radiation_diffuse = json_get_double(Current_Values, "diffuse_radiation");
  Vals->radiation_shortwave = json_get_double(Current_Values, "shortwave_radiation");

  _Weather->values = Vals;

  cJSON_Delete(Json_Root);

  return SUCCESS;
}

const char* meteo_get_response_json(const char* _url)
{
  http_data H_Data;

  int res;
  res = http_blocking_get(_url, &H_Data, 10000);

  if (res != SUCCESS) {
    LOG_ERROR("http_blocking_get failed: %d", res);
  }

  char* response = malloc(H_Data.size + 1);
  if (response == NULL) {
    LOG_ERROR("malloc");
    return NULL;
  }

  memcpy(response, H_Data.addr, H_Data.size);
  response[H_Data.size] = '\0';

  free(H_Data.addr);
  H_Data.size = 0;

  return response;
}
