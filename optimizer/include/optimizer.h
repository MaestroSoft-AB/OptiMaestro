#ifndef __OPTIMIZER_H__
#define __OPTIMIZER_H__

#include "data/electricity_structs.h"
#include "electricity_cache_handler.h"
#include "maestromodules/thread_pool.h"
#include "weather_cache_handler.h"

#include <pthread.h>
#include <stdint.h>

#define OPTIMIZER_CONF_PATH "/etc/maestro/optimizer.conf"
#define OPTIMIZER_LOG_PATH "/var/log/maestro.log"

typedef struct
{
  char* data_dir;
  char* data_spots_dir;
  char* data_weather_dir;
  char* data_calcs_dir;

  SpotCurrency currency;
  SpotPriceClass price_class;

  float latitude;
  float longitude;

  short panel_azimuth; // +/- 180
  unsigned short panel_tilt;
  unsigned short panel_size;

  uint8_t max_threads;

  // bool      ext_spot;
  // bool      ext_weather;

  // ECH_Config* ech_conf;

} Optimizer_Config;

typedef struct
{
  Optimizer_Config config;
  Thread_Pool* thread_pool;
  SqlHelper sqlhelper;
} Optimizer;

/* ========================== INTERFACE ========================== */

int optimizer_init(Optimizer* _O);

int optimizer_config_set(Optimizer_Config* _OC);

/* Runs the optimizer with the given config, updating all data */
int optimizer_run(Optimizer* _O);

void optimizer_dispose(Optimizer* _O);

/* =============================================================== */

#endif
