#ifndef __OPTIMIZER_H__
#define __OPTIMIZER_H__

#include "data/electricity_structs.h"
#include "electricity_cache_handler.h"
#include "weather_cache_handler.h"
#include "maestromodules/thread_pool.h"

#include <stdint.h>
#include <pthread.h>

#define OPTIMIZER_CONF_PATH "/etc/maestro.conf"
#define OPTIMIZER_LOG_PATH "/var/log/maestro.log"

typedef struct
{
  const char*  data_path;
  uint8_t      max_threads;

  SpotCurrency currency;

  // bool      ext_spot;
  // bool      ext_weather;

  // ECH_Config* ech_conf;

} Optimizer_Config;

typedef struct
{
  Optimizer_Config  config;
  Thread_Pool*      thread_pool;

} Optimizer;

/* ========================== INTERFACE ========================== */

int optimizer_init(Optimizer* _OC);

int optimizer_config_set(Optimizer* _OC);

/* Runs the optimizer with the given config, updating all data */
int optimizer_run(Optimizer* _OC);

void optimizer_dispose(Optimizer* _OC);

/* =============================================================== */

#endif
