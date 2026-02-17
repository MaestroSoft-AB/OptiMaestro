#include "optimizer.h"
#include "data/electricity_structs.h"
#include "electricity_cache_handler.h"
#include "maestromodules/thread_pool.h"
#include "maestroutils/error.h"
#include "maestroutils/file_utils.h"

#include "maestromodules/curl.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>

/* --------------------- Internal declarations --------------------- */

#define DATA_DIR "./data" // TODO: Move to conf


void optimizer_run_ech(void* _ech_conf)
{
  int res;
  ECH_Conf* Conf = (ECH_Conf*)_ech_conf;
  ECH E;

  res = ech_init(&E);
  if (res != 0) {
    perror("ech_init");
    return;
  }

  res = ech_update_cache(&E, Conf);
  if (res != 0) {
    perror("ech_update_cache");
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

  res = wch_init(&W);
  if (res != 0) {
    perror("wch_init");
    return;
  }

  res = wch_update_cache(&W, Conf);
  if (res != 0) {
    perror("wch_update_cache");
    wch_dispose(&W);
    return;
  }

  wch_dispose(&W);
}

/* ---------------------------------------------------------------- */

int optimizer_init(Optimizer* _OC)
{
  int res;

#ifdef CURL_GLOBAL_DEFAULT
  curl_global_init(CURL_GLOBAL_DEFAULT);
#endif

  memset(_OC, 0, sizeof(Optimizer));
  create_directory_if_not_exists(DATA_DIR);

  res = optimizer_config_set(_OC, OPTI_CONFIG_PATH);
  if (res != 0) {
    perror("optimizer_config_set");
    return res;
  }

  return SUCCESS;
}

int optimizer_config_set(Optimizer* _OC, const char* _conf_path)
{
  // TODO: read from conf file, add conf parse util

  _OC->config.max_threads = 4;
  _OC->config.data_path = "../data";
  _OC->config.currency = SPOT_SEK;

  return SUCCESS;
}

int optimizer_run(Optimizer* _OC)
{
  int i;
  int res;

  /* Define cache runs with config structs */
  ECH_Conf ECH_Config[4] = {0}; // One per each price_class
  for (i = 0; i < 4; i++) { 
    ECH_Config[i].price_class = i; 
    ECH_Config[i].currency = _OC->config.currency; 
  }
  WCH_Conf WCH_Config[2] = {0}; // One per current+forecast
  WCH_Config[0].forecast = true; WCH_Config[1].forecast = false;

  /* SINGLE THREADED - one by one */
  if (_OC->config.max_threads < 2) {
    ECH E;
    res = ech_init(&E);
    if (res != 0) {
      perror("wch_init");
      return res;
    }
    WCH W;
    res = wch_init(&W);
    if (res != 0) {
      perror("wch_init");
      ech_dispose(&E);
      return res;
    }
    
    for (i = 0; i < 4; i++) { 
      res = ech_update_cache(&E, &ECH_Config[i]);
      if (res != 0) {
        perror("ech_update_cache");
        ech_dispose(&E);
        wch_dispose(&W);
        return res;
      }
    }
    ech_dispose(&E);

    for (i = 0; i < 2; i++) { 
      res = wch_update_cache(&W, &WCH_Config[i]);
      if (res != 0) {
        perror("wch_update_cache");
        ech_dispose(&E);
        wch_dispose(&W);
        return res;
      }
    }
    wch_dispose(&W);

  }
  /* MULTI THREADED - using thread_pool.h󰩧 */
  else {

    _OC->thread_pool = tp_init(_OC->config.max_threads);
    
    if (!_OC->thread_pool) {
      perror("tp_init");
      return ERR_FATAL;
    }

    for (i = 0; i < 4; i++) { 
      TP_Task Task = { optimizer_run_ech, &ECH_Config[i], NULL, NULL };
      res = tp_task_add(_OC->thread_pool, &Task);
      if (res != 0) {
        perror("tp_task_add");
        tp_dispose(_OC->thread_pool);
        return res;
      }
    }

    for (i = 0; i < 2; i++) { 
      TP_Task Task = { optimizer_run_wch, &WCH_Config[i], NULL, NULL };
      res = tp_task_add(_OC->thread_pool, &Task);
      if (res != 0) {
        perror("tp_task_add");
        tp_dispose(_OC->thread_pool);
        return res;
      }
    }

    tp_wait(_OC->thread_pool);
    tp_dispose(_OC->thread_pool);
    _OC->thread_pool = NULL;
  }

  /* run calculator */

  return SUCCESS;
}

void optimizer_dispose(Optimizer* _OC)
{
  if (_OC->thread_pool != NULL) {
    tp_wait(_OC->thread_pool);
    tp_dispose(_OC->thread_pool);
  }

#ifdef CURL_GLOBAL_DEFAULT
  curl_global_cleanup(); 
#endif

  _OC = NULL;
}
