#ifndef __WEATHER_CACHE_HANDLER_H__
#define __WEATHER_CACHE_HANDLER_H__

#include "data/weather_structs.h"
// #include "meteo.h"

#include <stdint.h>
#include <stdbool.h>

/* ======================== STRUCTS ======================== */

typedef struct
{
  bool forecast;

} WCH_Conf;

typedef struct
{
  Weather     weather;
  // Meteo       meteo;

  const char* cache_path;

  float       latitude;
  float       longitude;

} WCH; // Weather Cache Handler

/* ======================= INTERFACE ======================= */

int wch_init(WCH* _WCH);

int wch_update_cache(WCH* _WCH, WCH_Conf* _Conf);

int wch_write_cache(const Weather* _Spot, const char* _cache_path);
int wch_read_cache(Weather* _Spot, const char* _cache_path);

void wch_dispose(WCH* _WCH_Ptr);

/* ========================================================= */

#endif
