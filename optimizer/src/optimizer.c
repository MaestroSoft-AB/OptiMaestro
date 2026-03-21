#include "optimizer.h"
#include "calculations.h"
#include "data/facility.h"
#include "data/electricity_structs.h"
#include "electricity_cache_handler.h"
#include "maestromodules/thread_pool.h"
#include "maestroutils/config_handler.h"
#include "maestroutils/error.h"
#include "maestroutils/file_logging.h"
#include "maestroutils/file_utils.h"
#include "sqlite_helpers.h"
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
    LOG_ERROR("ech_init");
    return;
  }

  res = ech_update_cache(&E);
  if (res != 0) {
    LOG_ERROR("ech_update_cache");
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
    LOG_ERROR("wch_init");
    return;
  }

  res = wch_update_cache(&W);
  if (res != 0) {
    LOG_ERROR("wch_update_cache");
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

  if (_OC->data_dir)
    free(_OC->data_dir);
  if (_OC->data_weather_dir)
    free(_OC->data_weather_dir);
  if (_OC->data_spots_dir)
    free(_OC->data_spots_dir);
  if (_OC->data_calcs_dir)
    free(_OC->data_calcs_dir);

  const char* keys[] = {
    "sys.max_threads",
    "data.dir",
    "data.spots.dir",
    "data.weather.dir",
    "data.calcs.dir",
    "conf.facility.dir"
  };

  char conf_max_threads[64] = {0};
  char conf_data_dir[64] = {0};
  char conf_data_spots_dir[64] = {0};
  char conf_data_weather_dir[64] = {0};
  char conf_data_calcs_dir[64] = {0};
  char conf_facility_dir[64] = {0};

  char* values[] = {
    conf_max_threads,
    conf_data_dir,
    conf_data_spots_dir, 
    conf_data_weather_dir, 
    conf_data_calcs_dir, 
    conf_facility_dir,
  };

  int res = config_get_value(OPTIMIZER_CONF_PATH, keys, values, 64, 6);

  if (res != SUCCESS)
    return res;

  int max_threads = atoi(conf_max_threads);
  if (max_threads > 0)
    _OC->max_threads = max_threads;
  else
    _OC->max_threads = 1;

  LOG_INFO("max_threads: %i\n", _OC->max_threads);


  /* THESE DO NOT WORK, END UP NULL...
   * ?? */
  size_t path_len;
  if (strcmp(conf_data_dir, "") != 0) {
    path_len = strlen(conf_data_dir);
    _OC->data_dir = malloc(path_len + 1);
    if (!_OC->data_dir) {
      LOG_ERROR("malloc");
      return ERR_NO_MEMORY;
    }
    memcpy(_OC->data_dir, conf_data_dir, path_len);
    _OC->data_dir[path_len] = '\0';
  }
  if (strcmp(conf_data_spots_dir, "") != 0) {
    path_len = strlen(conf_data_spots_dir);
    _OC->data_spots_dir = malloc(path_len + 1);
    if (!_OC->data_spots_dir) {
      LOG_ERROR("malloc");
      return ERR_NO_MEMORY;
    }
    memcpy(_OC->data_spots_dir, conf_data_spots_dir, path_len);
    _OC->data_spots_dir[path_len] = '\0';
  }
  if (strcmp(conf_data_weather_dir, "") != 0) {
    path_len = strlen(conf_data_weather_dir);
    _OC->data_weather_dir = malloc(path_len + 1);
    if (!_OC->data_weather_dir) {
      LOG_ERROR("malloc");
      return ERR_NO_MEMORY;
    }
    memcpy(_OC->data_weather_dir, conf_data_weather_dir, path_len);
    _OC->data_weather_dir[path_len] = '\0';
  }
  if (strcmp(conf_data_calcs_dir, "") != 0) {
    path_len = strlen(conf_data_calcs_dir);
    _OC->data_calcs_dir = malloc(path_len + 1);
    if (!_OC->data_calcs_dir) {
      LOG_ERROR("malloc");
      return ERR_NO_MEMORY;
    }
    memcpy(_OC->data_calcs_dir, conf_data_calcs_dir, path_len);
    _OC->data_calcs_dir[path_len] = '\0';
  }
  if (strcmp(conf_facility_dir, "") != 0) {
    path_len = strlen(conf_facility_dir);
    _OC->data_calcs_dir = malloc(path_len + 1);
    if (!_OC->data_calcs_dir) {
      LOG_ERROR("malloc");
      return ERR_NO_MEMORY;
    }
    memcpy(_OC->facility_dir, conf_facility_dir, path_len);
    _OC->facility_dir[path_len] = '\0';
  }

  /* Facility configs */
  _OC->facility_configs = facility_get_configs(_OC->facility_dir, _OC->facility_count);
  if (!_OC->facility_configs || !_OC->facility_configs[0]) {
    LOG_ERROR("Otpimzer failed to get valid facility configs.");
    return ERR_INTERNAL;
  }

  return SUCCESS;
}

int optimizer_run(Optimizer* _O)
{
  int i = 0; 
  int res;

  sqlite3* db = NULL;
  char db_path[512];

  snprintf(db_path, sizeof(db_path), "%s/cache.db", _O->config.data_dir);

  /* Initiate Squeeeel */
  res = sql_helper_open(&db, db_path);
  if (res != SUCCESS) {
    LOG_ERROR("sql_helper_open (%i)", res);
    return res;
  }

  res = sql_helper_init_schema(db);
  if (res != SUCCESS) {
    LOG_ERROR("sql_helper_init_schema (%i)", res);
    sql_helper_close(db);
    return res;
  }

  sql_helper_close(db);

  /* Initiate thread pools */
  _O->thread_pool = tp_init(_O->config.max_threads);
  if (!_O->thread_pool) {
    LOG_ERROR("tp_init");
    return ERR_FATAL;
  }

  /* Define cache runs with config structs */
  /* Electricity spots are indefferent to facility configs */
  ECH_Conf ECH_Config[4] = {0}; // One per each price_class
  for (i = 0; i < 4; i++) {
    ECH_Config[i].price_class = i;
    ECH_Config[i].currency = SPOT_SEK; // Only have support for SEK
    if (_O->config.data_spots_dir != NULL)
      ECH_Config[i].data_dir = _O->config.data_dir;
  }

  /* Start electricity cache handler threads */
  for (i = 0; i < 4; i++) {
    TP_Task Task = {optimizer_run_ech, &ECH_Config[i], NULL, NULL};
    res = tp_task_add(_O->thread_pool, &Task);
    if (res != 0)
      LOG_ERROR("tp_task_add");
  }

  /* Weather gets two runs per facility: forecast and current */
  /* TODO: Find a way to not run weather less times, meteo gets a lot of requests 
   * - maybe check if last saved weather time already is same or more than last electricity time
   * no need to run if we don't have enough electricity data anyway */
  WCH_Conf* WCH_Confs = calloc(1, sizeof(WCH_Conf) * _O->config.facility_count * 2);
  if (!WCH_Confs) {
    LOG_ERROR("calloc");
    return ERR_NO_MEMORY;
  }

  for (i = 0; i < (int)_O->config.facility_count; i++) {
    int j = i * 2; // destination index

    /* Forecast weather run */
    WCH_Confs[j].forecast = true;
    WCH_Confs[j].latitude = _O->config.facility_configs[i]->lat;
    WCH_Confs[j].longitude = _O->config.facility_configs[i]->lon;
    WCH_Confs[j].data_dir = _O->config.data_weather_dir; // TODO: Switch to db path when db

    /* Facility might not have solarpanels, set to zero if so 
     * TODO: differentiate weather that has panels and that don't in WCH as well */
    if (_O->config.facility_configs[i]->panel != NULL) {
      WCH_Confs[j].panel_azimuth = _O->config.facility_configs[i]->panel->azimuth;
      WCH_Confs[j].panel_tilt = _O->config.facility_configs[i]->panel->tilt;
    }
    else {
      WCH_Confs[j].panel_azimuth = 0;
      WCH_Confs[j].panel_tilt = 0;
    }

    /* Current weather run */
    WCH_Confs[j + 1].forecast = false;
    WCH_Confs[j + 1].latitude = _O->config.facility_configs[i]->lat;
    WCH_Confs[j + 1].longitude = _O->config.facility_configs[i]->lon;
    WCH_Confs[j + 1].data_dir = _O->config.data_weather_dir; // TODO: Switch to db path when db

    /* Facility might not have solarpanels, set to zero if so 
     * TODO: differentiate weather that has panels and that don't in WCH as well */
    if (_O->config.facility_configs[i]->panel != NULL) {
      WCH_Confs[j + 1].panel_azimuth = _O->config.facility_configs[i]->panel->azimuth;
      WCH_Confs[j + 1].panel_tilt = _O->config.facility_configs[i]->panel->tilt;
    }
    else {
      WCH_Confs[j + 1].panel_azimuth = 0;
      WCH_Confs[j + 1].panel_tilt = 0;
    }
  }

  /* Start weather cache handler threads */
  for (i = 0; i < (int)(_O->config.facility_count * 2); i++) {
    TP_Task Task = {optimizer_run_wch, &WCH_Confs[i], NULL, NULL};
    res = tp_task_add(_O->thread_pool, &Task);
    if (res != 0)
      LOG_ERROR("tp_task_add");
  }

  tp_wait(_O->thread_pool);
  tp_dispose(_O->thread_pool);
  _O->thread_pool = NULL;

  /* Run calculator */
  Calc_Args C_Args = {
      .calcs_dir        = _O->config.data_calcs_dir,
      .data_dir         = _O->config.data_dir,
      .weather_dir      = _O->config.data_weather_dir,
      .facility_configs = _O->config.facility_configs,
      .facility_count   = _O->config.facility_count,
      .max_threads      = _O->config.max_threads,
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

  facility_dispose(_O->config.facility_configs, _O->config.facility_count);

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
