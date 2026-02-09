#ifndef __ELRPISJUSTNU_H__
#define __ELRPISJUSTNU_H__

#define EPJN_URL "https://www.elprisetjustnu.se/api/v1/prices/%04i/%02i-%02i_SE%c.json"
#define EPJN_URL_LEN 64 

#include "data/electricity_structs.h"

#include <stdint.h>
#include <time.h>

/* ======================== STRUCTS ======================== */

typedef struct
{
  time_t  time_start;
  time_t  time_end;
  float   spot_sek;
  float   spot_eur;
  float   exr;

} EPJN_Spot_Price;

typedef struct
{
  EPJN_Spot_Price* prices;
  int              price_count;
  SpotPriceClass   price_class;

} EPJN_Spots;

/* ======================= INTERFACE ======================= */

int epjn_init(EPJN_Spots* _Elpris_Spot);

int epjn_update(EPJN_Spots* _Elpris_Spot, 
                        SpotPriceClass _price_class, 
                        time_t _date);

int epjn_parse(EPJN_Spots* _Elpris_Spot, 
                       Electricity_Spots* _Spot,
                       SpotCurrency _currency);

void epjn_dispose(EPJN_Spots* _Elpris_Spot);

/* ========================================================= */


/*
"Morgondagens elpris anländer tidigast kl 13 dagen innan." 
Example json: 
[
  {
    "SEK_per_kWh": 1.08563,
    "EUR_per_kWh": 0.10257,
    "EXR": 10.584272,
    "time_start": "2026-01-26T00:00:00+01:00",
    "time_end": "2026-01-26T00:15:00+01:00"
  },
  ...
]
*/
// EXR = Växelkursen som används från EUR till SEK.

#endif
