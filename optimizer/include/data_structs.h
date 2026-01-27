#ifndef __DATA_STRUCTS_H__
#define __DATA_STRUCTS_H__

#include <stdint.h>
#include <time.h>

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

#endif
