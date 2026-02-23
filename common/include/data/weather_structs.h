#ifndef __WEATHER_STRUCTS_H__
#define __WEATHER_STRUCTS_H__

#include <stdint.h>
#include <time.h>

/* ======================== Weather/Forecast ======================== */

typedef struct 
{
  time_t          timestamp;

  float           temperature;
  float           precipitation;
  float           windspeed;

  float           radiation_direct_n; // DNI - direct normalized
  float           radiation_tilted; // GTI (needs tilt+azimuth params)
  float           radiation_direct;
  float           radiation_diffuse; // DHI
  float           radiation_shortwave; // GHI (DNI × cos(zenith angle) + DHI)

  int             sun_duration; // during interval

  unsigned short  winddirection_azimuth;
  // uint8_t         wmo_code;

} Weather_Values;

typedef struct 
{
  Weather_Values* values;

  const char*     cache_path;

  const char*     temperature_unit;
  const char*     windspeed_unit;
  const char*     precipitation_unit;
  const char*     winddirection_unit;
  const char*     radiation_unit; // should be W/m2

  float           latitude;
  float           longitude;

  unsigned int    count;
  unsigned int    update_interval; // minutes between values
  
  // unsigned short  panel_azimuth; 
  // unsigned short  panel_tilt;

  char            sun_duration_unit;

} Weather;

#endif
