#ifndef __WEATHER_STRUCTS_H__
#define __WEATHER_STRUCTS_H__

#include <stdint.h>
#include <time.h>

/* ======================== Weather/Forecast ======================== */

typedef struct 
{
  time_t          timestamp;
  time_t          update_interval;

  float           temperature;
  float           precipitation;
  float           windspeed;

  const char      winddirection_cardinal[4];

  unsigned short  winddirection_azimuth;
  uint8_t         wmo_code;

} Weather_Values;

typedef struct 
{
  Weather_Values* values;

  const char*     cache_path;
  const char*     temperature_unit;
  const char*     windspeed_unit;
  const char*     precipitation_unit;
  const char*     winddirection_unit;

  double          elevation;

  float           latitude;
  float           longitude;

  unsigned int    count;

} Weather;

#endif
