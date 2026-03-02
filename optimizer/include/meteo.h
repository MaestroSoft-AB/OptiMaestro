#ifndef __METEO_H__
#define __METEO_H__

#include "data/weather_structs.h"


// Needs (lat, lon, query)
#define METEO_BASE_URL "http://api.open-meteo.com/v1/forecast" \
  "?latitude=%f&longitude=%f&timezone=GMT&azimuth=%f&tilt=%f&%s"

#define METEO_CURRENT_QUERY "current=shortwave_radiation,direct_radiation,"          \
  "diffuse_radiation,direct_normal_irradiance,global_tilted_irradiance,"             \
  "temperature_2m,sunshine_duration,wind_speed_10m,precipitation,wind_direction_10m" 

/* Gets hourly data between given dates
 * Allowed range is 3 months past and 2 weeks future
 * Can get costly if range is big, so use sparsely
 * &start_date=2025-11-16&end_date=2026-02-26 */
#define METEO_HOURLY_QUERY "hourly=shortwave_radiation,direct_radiation,"            \
  "diffuse_radiation,direct_normal_irradiance,global_tilted_irradiance,"             \
  "temperature_2m,sunshine_duration,wind_speed_10m,precipitation,wind_direction_10m" \
  "&start_date=%s&end_date=%s"

/* 24 hour forward-looking forecast for every 15 minutes (count 96) */
#define METEO_15_MINUTELY_QUERY "minutely_15=shortwave_radiation,direct_radiation,"  \
  "diffuse_radiation,direct_normal_irradiance,global_tilted_irradiance,"             \
  "temperature_2m,sunshine_duration,wind_speed_10m,precipitation,wind_direction_10m" \
  "&forecast_minutely_15=96&past_minutely_15=96"

#define METEO_GTR_QUERY "current=global_tilted_irradiance_instant,"                  \
                        "global_tilted_irradiance&azimuth=%f&tilt=%f"


/* passed Weather struct should be allocated and nulled */
int meteo_get_hourly(Weather* _Weather, 
                     float    _lat,
                     float    _lon,
                     time_t   _start_date, 
                     time_t   _end_date);

int meteo_get_15_minutely(Weather* _Weather,
                         float    _lat,
                         float    _lon);

int meteo_get_current(Weather* _Weather,
                      float    _lat,
                      float    _lon);

#endif

// typedef struct 
// {
//   time_t          timestamp;
//
//   float           temperature;
//   float           precipitation;
//   float           windspeed;
//
//   float           radiation_direct;
//   float           radiation_diffuse;
//   float           radiation_shortwave;
//
//   int             sunshine_duration; // during interval
//
//   const char      winddirection_cardinal[4];
//
//   unsigned short  winddirection_azimuth;
//
// } Meteo_Values;
//
// typedef struct 
// {
//   Meteo_Values*   values;
//
//   const char*     cache_path;
//   const char*     temperature_unit;
//   const char*     windspeed_unit;
//   const char*     precipitation_unit;
//   const char*     winddirection_unit;
//   const char*     radiation_unit; // should be W/m2
//
//   double          elevation;
//
//   float           latitude;
//   float           longitude;
//
//   unsigned int    count;
//   unsigned int    update_interval;
//   
//   char            duration_unit;
//
// } Meteo;


