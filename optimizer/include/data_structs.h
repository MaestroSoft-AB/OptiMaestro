#ifndef __DATA_STRUCTS_H__
#define __DATA_STRUCTS_H__

#include <stdint.h>
#include <time.h>

/* ====================== Electricty Spotprice ====================== */

typedef struct
{
  time_t  timestamp;
  float   spot_price;

} Electricity_Spot_Price;

typedef struct
{
  Electricity_Spot_Price* prices; 
  const char*             unit;         // e.g "SEK/kWh"
  int                     prices_c;
  int                     interval;     // minutes between spots
  char                    currency[4];
  uint8_t                 price_class;  // 1-4, i.e SE1, SE2 etc.

} Electricity_Spots;


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
