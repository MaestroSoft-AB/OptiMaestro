#include "optimizer.h"
#include "calculations.h"
#include "data/electricity_structs.h"
#include "electricity_cache_handler.h"
#include "maestromodules/curl.h"
#include "maestromodules/thread_pool.h"
#include "maestroutils/config_handler.h"
#include "maestroutils/error.h"
#include "maestroutils/file_logging.h"
#include "maestroutils/file_utils.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* --------------------- Internal declarations --------------------- */

// #define DATA_DIR "/var/lib/maestro" // TODO: Move to conf

void optimizer_run_ech(void* _ech_conf)
{
  int res;
  ECH_Conf* Conf = (ECH_Conf*)_ech_conf;
  ECH E;

  res = ech_init(&E, Conf);
  if (res != 0) {
    LOG_ERROR("ech_init"); // TODO: Logger
    return;
  }

  res = ech_update_cache(&E);
  if (res != 0) {
    LOG_ERROR("ech_update_cache"); // TODO: Logger
    ech_dispose(&E);
    return;
  }

  ech_dispose(&E);
}

void optimizer_run_wch(void* _wch_conf)
{
  int res;
  WCH_Conf* Conf = (WCH_Conf*)_wch_conf;
  WCH W;

  res = wch_init(&W, Conf);
  if (res != 0) {
    LOG_ERROR("wch_init"); // TODO: Logger
    return;
  }

  res = wch_update_cache(&W);
  if (res != 0) {
    LOG_ERROR("wch_update_cache"); // TODO: Logger
    wch_dispose(&W);
    return;
  }

  wch_dispose(&W);
}

/* ---------------------------------------------------------------- */

int optimizer_init(Optimizer* _O)
{
  int res;

#ifdef CURL_GLOBAL_DEFAULT
  curl_global_init(CURL_GLOBAL_DEFAULT);
#endif

  memset(_O, 0, sizeof(Optimizer));
  create_directory_if_not_exists(OPTIMIZER_CONF_PATH);

  res = optimizer_config_set(&_O->config);
  if (res != 0) {
    LOG_ERROR("optimizer_config_set");
    return res;
  }

  return SUCCESS;
}

int optimizer_config_set(Optimizer_Config* _OC)
{
  // TODO: read from conf file, add conf parse util

  const char* keys[] = {
      "sys.max_threads",     "data.spots.currency",    "data.dir",          "data.spots.dir",
      "data.weather.dir",    "data.calcs.dir",         "facility.latitude", "facility.longitude",
      "facility.panel.tilt", "facility.panel.azimuth",
  };

  char conf_max_threads[64] = {0};
  char conf_currency[64] = {0};
  char conf_data_dir[64] = {0};
  char conf_data_spots_dir[64] = {0};
  char conf_data_weather_dir[64] = {0};
  char conf_data_calcs_dir[64] = {0};
  char conf_facility_lat[64] = {0};
  char conf_facility_lon[64] = {0};
  char conf_solar_tilt[64] = {0};
  char conf_solar_azimuth[64] = {0};

  char* values[] = {
      conf_max_threads,      conf_currency,       conf_data_dir,     conf_data_spots_dir,
      conf_data_weather_dir, conf_data_calcs_dir, conf_facility_lat, conf_facility_lon,
      conf_solar_tilt,       conf_solar_azimuth,
  };

  int res = config_get_value(OPTIMIZER_CONF_PATH, keys, values, 64, 10);

  if (res != SUCCESS)
    return res;

  int max_threads = atoi(conf_max_threads);
  if (max_threads > 0)
    _OC->max_threads = max_threads;
  else
    _OC->max_threads = 1;

  float lat = atof(conf_facility_lat);
  float lon = atof(conf_facility_lon);
  _OC->latitude = lat;
  _OC->longitude = lon;

  int panel_tilt = atoi(conf_solar_tilt);
  int panel_azimuth = atoi(conf_solar_azimuth);
  _OC->panel_tilt = (unsigned short)panel_tilt;
  _OC->panel_azimuth = (short)panel_azimuth;

  printf("lat: %s\n", conf_facility_lat);
  printf("lon: %s\n", conf_facility_lat);

  LOG_INFO("max_threads: %i\n", _OC->max_threads);
  LOG_INFO("latitude: %f, longitude %f\n", _OC->longitude, _OC->latitude);
  LOG_INFO("azimuth %i, tilt: %i\n", _OC->panel_azimuth, _OC->panel_tilt);

  /* if (strcmp(values[1], "SEK") == 0)
    _OC->config.currency = SPOT_SEK; */

  /* THESE DO NOT WORK, END UP NULL... */
  size_t path_len;
  if (strcmp(conf_data_dir, "") != 0) {
    path_len = strlen(conf_data_dir);
    _OC->data_dir = malloc(path_len + 1);
    memcpy(_OC->data_dir, conf_data_dir, path_len);
    _OC->data_dir[path_len] = '\0';
    if (!_OC->data_dir) {
      LOG_ERROR("malloc");
      return ERR_NO_MEMORY;
    }
  }
  if (strcmp(conf_data_spots_dir, "") != 0) {
    path_len = strlen(conf_data_spots_dir);
    _OC->data_spots_dir = malloc(path_len + 1);
    memcpy(_OC->data_spots_dir, conf_data_spots_dir, path_len);
    _OC->data_spots_dir[path_len] = '\0';
    if (!_OC->data_spots_dir) {
      LOG_ERROR("malloc");
      return ERR_NO_MEMORY;
    }
  }
  if (strcmp(conf_data_weather_dir, "") != 0) {
    path_len = strlen(conf_data_weather_dir);
    _OC->data_weather_dir = malloc(path_len + 1);
    memcpy(_OC->data_weather_dir, conf_data_weather_dir, path_len);
    _OC->data_weather_dir[path_len] = '\0';
    if (!_OC->data_weather_dir) {
      LOG_ERROR("malloc");
      return ERR_NO_MEMORY;
    }
  }
  if (strcmp(conf_data_calcs_dir, "") != 0) {
    path_len = strlen(conf_data_calcs_dir);
    _OC->data_calcs_dir = malloc(path_len + 1);
    memcpy(_OC->data_calcs_dir, conf_data_calcs_dir, path_len);
    _OC->data_calcs_dir[path_len] = '\0';
    if (!_OC->data_calcs_dir) {
      LOG_ERROR("malloc");
      return ERR_NO_MEMORY;
    }
  }

  _OC->currency = SPOT_SEK;

  return SUCCESS;
}

int optimizer_run(Optimizer* _O)
{
  int i;
  int res;

  /* Define cache runs with config structs */
  ECH_Conf ECH_Config[4] = {0}; // One per each price_class
  for (i = 0; i < 4; i++) {
    ECH_Config[i].price_class = i;
    ECH_Config[i].currency = _O->config.currency;
    if (_O->config.data_spots_dir != NULL)
      ECH_Config[i].data_dir = _O->config.data_spots_dir;
  }
  WCH_Conf WCH_Config[2] = {0}; // One per current+forecast
  WCH_Config[0].forecast = true;
  WCH_Config[1].forecast = false;
  for (i = 0; i < 2; i++) {
    WCH_Config[i].latitude = _O->config.latitude;
    WCH_Config[i].longitude = _O->config.longitude;
    WCH_Config[i].panel_azimuth = _O->config.panel_azimuth;
    WCH_Config[i].panel_tilt = _O->config.panel_tilt;
    if (_O->config.data_weather_dir != NULL)
      WCH_Config[i].data_dir = _O->config.data_weather_dir;
  }

  /* Initiate thread pools */
  _O->thread_pool = tp_init(_O->config.max_threads);
  if (!_O->thread_pool) {
    LOG_ERROR("tp_init"); // TODO: Logger
    return ERR_FATAL;
  }

  /* Run cache handler threads */
  for (i = 0; i < 4; i++) {
    TP_Task Task = {optimizer_run_ech, &ECH_Config[i], NULL, NULL};
    res = tp_task_add(_O->thread_pool, &Task);
    if (res != 0)
      LOG_ERROR("tp_task_add"); // TODO: Logger
  }

  for (i = 0; i < 2; i++) {
    TP_Task Task = {optimizer_run_wch, &WCH_Config[i], NULL, NULL};
    res = tp_task_add(_O->thread_pool, &Task);
    if (res != 0)
      LOG_ERROR("tp_task_add"); // TODO: Logger
  }

  tp_wait(_O->thread_pool);
  tp_dispose(_O->thread_pool);
  _O->thread_pool = NULL;

  /* run calculator */

  calc_get_average();

  return SUCCESS;
}

void optimizer_dispose(Optimizer* _O)
{
  if (_O->thread_pool != NULL) {
    tp_wait(_O->thread_pool);
    tp_dispose(_O->thread_pool);
  }

  if (!_O->config.data_dir)
    free(_O->config.data_dir);
  if (!_O->config.data_calcs_dir)
    free(_O->config.data_calcs_dir);
  if (!_O->config.data_weather_dir)
    free(_O->config.data_weather_dir);
  if (!_O->config.data_spots_dir)
    free(_O->config.data_spots_dir);

#ifdef CURL_GLOBAL_DEFAULT
  curl_global_cleanup();
#endif

  _O = NULL;
}
