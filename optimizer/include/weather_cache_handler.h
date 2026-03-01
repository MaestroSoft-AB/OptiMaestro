#ifndef __WEATHER_CACHE_HANDLER_H__
#define __WEATHER_CACHE_HANDLER_H__

#include "data/weather_structs.h"
#include "meteo.h"

#include <stdint.h>
#include <stdbool.h>

#define WCH_BASE_CACHE_PATH_FALLBACK "/var/lib/maestro/weather"

/* ======================== STRUCTS ======================== */

typedef struct
{
  const char* data_dir;
  float       latitude;
  float       longitude;

  bool        forecast;

} WCH_Conf;

typedef struct
{
  Weather         weather;
  WCH_Conf        conf;

  const char*     data_path;

} WCH; // Weather Cache Handler

/* ======================= INTERFACE ======================= */

int wch_init(WCH* _WCH, const WCH_Conf* _Conf);

int wch_update_cache(WCH* _WCH);

int wch_write_cache(const Weather* _W, const char* _cache_path);
int wch_read_cache(Weather* _W, const char* _cache_path);

void wch_dispose(WCH* _WCH_Ptr);

/* ========================================================= */

#endif
