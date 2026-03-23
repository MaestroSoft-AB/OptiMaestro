#ifndef __WEATHER_CACHE_HANDLER_H__
#define __WEATHER_CACHE_HANDLER_H__

#include "data/weather_structs.h"
#include "meteo.h"
#include "sqlite_helpers.h"
#include <stdbool.h>
#include <stdint.h>

#define WCH_BASE_CACHE_PATH_FALLBACK "/var/lib/maestro/weather"

/* ======================== STRUCTS ======================== */

typedef struct
{
  // const char* data_dir;
  float latitude;
  float longitude;

  int panel_tilt;
  unsigned int panel_azimuth;

  bool forecast;
  SqlHelper* sqlhelper;
} WCH_Conf;

typedef struct
{
  Weather weather;
  WCH_Conf conf;

  const char* data_path;
  SqlHelper* sqlhelper;
} WCH; // Weather Cache Handler

/* ======================= INTERFACE ======================= */

int wch_init(WCH* _WCH, const WCH_Conf* _Conf);

int wch_update_cache(WCH* _WCH);

int wch_write_cache(const Weather* _W, const char* _cache_path);

// TODO: Replace with more modular fetch method,
// specifying time range and interval for instance
int wch_read_cache_json(Weather* _W, const char* _cache_path);
char* wch_get_cache_json_filepath(const char* _base_path, time_t _datetime, bool _forecast);

// int wch_read_cache_hdf5(Weather* _W, const char* _cache_path,
//                         time_t _start_date, time_t _end_date);
int wch_get_weather_range(SqlHelper* _H, Weather* _W, double _latitude, double _longitude,
                          int _panel_tilt, unsigned int _panel_azimuth, bool _forecast,
                          time_t _start, time_t _end);
void wch_weather_dispose(Weather* _W);
void wch_dispose(WCH* _WCH_Ptr);

/* ========================================================= */

#endif
