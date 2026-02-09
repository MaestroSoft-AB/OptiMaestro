#ifndef __ELECTRICITY_STRUCTS_H__
#define __ELECTRICITY_STRUCTS_H__

#include <stdint.h>
#include <time.h>

/* ====================== Electricty Spotprice ====================== */

typedef enum
{
  SPOT_SEK,
  SPOT_EUR,

} SpotCurrency;

typedef enum
{
  SE1,
  SE2,
  SE3,
  SE4,

} SpotPriceClass;

typedef struct
{
  time_t  time_start;
  time_t  time_end;
  float   spot_price;

} Electricity_Spot_Price;

typedef struct
{
  Electricity_Spot_Price* prices;
  const char*             unit;         // e.g "SEK/kWh"
  SpotCurrency            currency;
  SpotPriceClass          price_class;  // 1-4, i.e SE1, SE2 etc.
  int                     price_count;
  int                     interval;     // minutes between spots

} Electricity_Spots;


#endif
