#ifndef __OPTIMIZER_H__
#define __OPTIMIZER_H__

#include "data/electricity_structs.h"
#include "data/facility.h"
#include "electricity_cache_handler.h"
#include "maestromodules/thread_pool.h"
#include "weather_cache_handler.h"

#include <pthread.h>
#include <stdint.h>

#define OPTIMIZER_CONF_PATH "/etc/maestro/optimizer.conf"
#define OPTIMIZER_LOG_PATH "/var/log/maestro.log"

typedef struct
{
  char*             data_dir;
  char*             data_spots_dir;
  char*             data_weather_dir;
  char*             data_calcs_dir;
  char*             facility_dir;

  Facility_Config** facility_configs;
  size_t            facility_count;

  uint8_t           max_threads;

} Optimizer_Config;

typedef struct
{
  Optimizer_Config  config;
  Thread_Pool*      thread_pool;

} Optimizer;

/* ========================== INTERFACE ========================== */

int optimizer_init(Optimizer* _O);

int optimizer_config_set(Optimizer_Config* _OC);

/* Runs the optimizer with the given config, updating all data */
int optimizer_run(Optimizer* _O);

void optimizer_dispose(Optimizer* _O);

/* =============================================================== */

#endif
