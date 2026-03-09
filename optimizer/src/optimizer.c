#include "optimizer.h"
#include "calculations.h"
#include "data/electricity_structs.h"
#include "electricity_cache_handler.h"
#include "maestromodules/thread_pool.h"
#include "maestroutils/config_handler.h"
#include "maestroutils/error.h"
#include "maestroutils/file_logging.h"
#include "maestroutils/file_utils.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
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

  res = optimizer_config_set(&_O->config);
  if (res != 0) {
    LOG_ERROR("optimizer_config_set");
    return res;
  }

  return SUCCESS;
}

int optimizer_config_set(Optimizer_Config* _OC)
{
  if (!_OC) {
    return ERR_INVALID_ARG;
  }

  if (_OC->data_dir)
    free(_OC->data_dir);
  if (_OC->data_weather_dir)
    free(_OC->data_weather_dir);
  if (_OC->data_spots_dir)
    free(_OC->data_spots_dir);
  if (_OC->data_calcs_dir)
    free(_OC->data_calcs_dir);

  config_handler_t* cfg = NULL;
  int res = config_handler_load(&cfg, OPTIMIZER_CONF_PATH);
  if (res != 0) {
    config_handler_free(cfg);
    return res;
  }

  /* Keep old behavior: require these keys to exist */
  const char* required_keys[] = {
      "sys.max_threads",
      "data.spots.currency",
      "data.spots.price_class",
      "data.dir",
      "data.spots.dir",
      "data.weather.dir",
      "data.calcs.dir",
      "facility.latitude",
      "facility.longitude",
      "facility.panel.tilt",
      "facility.panel.azimuth",
      "facility.panel.m2_size",
  };
  for (size_t i = 0; i < sizeof(required_keys) / sizeof(required_keys[0]); i++) {
    if (config_handler_get(cfg, required_keys[i]) == NULL) {
      config_handler_free(cfg);
      return -2;
    }
  }

  int max_threads = 1;
  if (config_handler_get_int_default(cfg, "sys.max_threads", 1, &max_threads) < 0) {
    config_handler_free(cfg);
    return ERR_INVALID_ARG;
  }
  if (max_threads > 0)
    _OC->max_threads = max_threads;
  else
    _OC->max_threads = 1;

  _OC->currency = SPOT_SEK;

  int price_class_conf = 1;
  if (config_handler_get_int_default(cfg, "data.spots.price_class", 1, &price_class_conf) < 0) {
    config_handler_free(cfg);
    return ERR_INVALID_ARG;
  }

  int price_class = price_class_conf - 1;
  if (price_class >= 0 && price_class <= 3)
    _OC->price_class = price_class;
  else
    _OC->price_class = 0;

  {
    double lat = 0.0;
    double lon = 0.0;
    if (config_handler_get_double_default(cfg, "facility.latitude", 0.0, &lat) < 0 ||
        config_handler_get_double_default(cfg, "facility.longitude", 0.0, &lon) < 0) {
      config_handler_free(cfg);
      return ERR_INVALID_ARG;
    }
    _OC->latitude = (float)lat;
    _OC->longitude = (float)lon;
  }

  int panel_tilt = 0;
  int panel_azimuth = 0;
  int panel_size = 0;
  if (config_handler_get_int_default(cfg, "facility.panel.tilt", 0, &panel_tilt) < 0 ||
      config_handler_get_int_default(cfg, "facility.panel.azimuth", 0, &panel_azimuth) < 0 ||
      config_handler_get_int_default(cfg, "facility.panel.m2_size", 0, &panel_size) < 0) {
    config_handler_free(cfg);
    return ERR_INVALID_ARG;
  }
  _OC->panel_tilt = (unsigned short)panel_tilt;
  _OC->panel_azimuth = (short)panel_azimuth;
  _OC->panel_size = (unsigned short)panel_size;

  LOG_INFO("max_threads: %i\n", _OC->max_threads);
  LOG_INFO("latitude: %f, longitude %f\n", _OC->longitude, _OC->latitude);
  LOG_INFO("azimuth %i, tilt: %i\n", _OC->panel_azimuth, _OC->panel_tilt);

  /* if (strcmp(values[1], "SEK") == 0)
    _OC->config.currency = SPOT_SEK; */

  /* THESE DO NOT WORK, END UP NULL... */
  size_t path_len;

  const char* conf_data_dir = config_handler_get_default(cfg, "data.dir", "");
  if (strcmp(conf_data_dir, "") != 0) {
    path_len = strlen(conf_data_dir);
    _OC->data_dir = malloc(path_len + 1);
    if (!_OC->data_dir) {
      LOG_ERROR("malloc");
      config_handler_free(cfg);
      return ERR_NO_MEMORY;
    }
    memcpy(_OC->data_dir, conf_data_dir, path_len);
    _OC->data_dir[path_len] = '\0';
  }

  const char* conf_data_spots_dir = config_handler_get_default(cfg, "data.spots.dir", "");
  if (strcmp(conf_data_spots_dir, "") != 0) {
    path_len = strlen(conf_data_spots_dir);
    _OC->data_spots_dir = malloc(path_len + 1);
    if (!_OC->data_spots_dir) {
      LOG_ERROR("malloc");
      config_handler_free(cfg);
      return ERR_NO_MEMORY;
    }
    memcpy(_OC->data_spots_dir, conf_data_spots_dir, path_len);
    _OC->data_spots_dir[path_len] = '\0';
  }

  const char* conf_data_weather_dir = config_handler_get_default(cfg, "data.weather.dir", "");
  if (strcmp(conf_data_weather_dir, "") != 0) {
    path_len = strlen(conf_data_weather_dir);
    _OC->data_weather_dir = malloc(path_len + 1);
    if (!_OC->data_weather_dir) {
      LOG_ERROR("malloc");
      config_handler_free(cfg);
      return ERR_NO_MEMORY;
    }
    memcpy(_OC->data_weather_dir, conf_data_weather_dir, path_len);
    _OC->data_weather_dir[path_len] = '\0';
  }

  const char* conf_data_calcs_dir = config_handler_get_default(cfg, "data.calcs.dir", "");
  if (strcmp(conf_data_calcs_dir, "") != 0) {
    path_len = strlen(conf_data_calcs_dir);
    _OC->data_calcs_dir = malloc(path_len + 1);
    if (!_OC->data_calcs_dir) {
      LOG_ERROR("malloc");
      config_handler_free(cfg);
      return ERR_NO_MEMORY;
    }
    memcpy(_OC->data_calcs_dir, conf_data_calcs_dir, path_len);
    _OC->data_calcs_dir[path_len] = '\0';
  }

  config_handler_free(cfg);

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

  Calc_Args C_Args = {
    .calcs_dir = _O->config.data_calcs_dir,
    .spots_dir = _O->config.data_spots_dir, 
    .weather_dir = _O->config.data_weather_dir, 
    .price_class = _O->config.price_class, 
    .currency = _O->config.currency, 
    .max_threads = _O->config.max_threads, 
    .panel_size = (int)_O->config.panel_size,
  };

  if (calc_create_reports(&C_Args) != SUCCESS) {
    LOG_ERROR("calc_create_reports");
    return ERR_FATAL;
  }

  return SUCCESS;
}

void optimizer_dispose(Optimizer* _O)
{
  if (_O->thread_pool != NULL) {
    tp_wait(_O->thread_pool);
    tp_dispose(_O->thread_pool);
  }

  if (_O->config.data_dir)
    free(_O->config.data_dir);
  if (_O->config.data_calcs_dir)
    free(_O->config.data_calcs_dir);
  if (_O->config.data_weather_dir)
    free(_O->config.data_weather_dir);
  if (_O->config.data_spots_dir)
    free(_O->config.data_spots_dir);

#ifdef CURL_GLOBAL_DEFAULT
  curl_global_cleanup();
#endif

  _O = NULL;
}
