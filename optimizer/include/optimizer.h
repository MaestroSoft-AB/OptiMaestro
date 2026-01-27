#ifndef __OPTIMIZER_H__
#define __OPTIMIZER_H__

#include "electricity_cache_handler.h"
#include "weather_cache_handler.h"

#include <stdint.h>
#include <pthread.h>

#define OPTI_CONFIG_PATH "data/config/optimizer.conf"

typedef struct
{
  // Maybe store what implemented devices to provide data for and what data to fetch
  
  uint8_t   max_threads;

  // bool      ext_spot;
  // bool      ext_weather;

} Optimizer_Config;

typedef struct
{
  Optimizer_Config            config;

  Weather_Cache_Handler*      wch;
  Electricity_Cache_Handler*  ech;

  pthread_t*                  threads;

} Optimizer;

/* ========================== INTERFACE ========================== */

int optimizer_init(Optimizer* _OC);

int optimizer_config_set(Optimizer* _OC, const char* _path);

/* Runs the optimizer with the given config, updating all data */
int optimizer_run(Optimizer* _OC);

void optimizer_dispose(Optimizer* _OC);

/* =============================================================== */

#endif
