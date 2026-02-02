#ifndef __ELRPISJUSTNU_H__
#define __ELRPISJUSTNU_H__

#define ELPRISJUSTNU_URL "https://www.elprisetjustnu.se/api/v1/prices/%i/%i-%i_%s.json"

#include "data/electricity_structs.h"

#include <stdint.h>
#include <time.h>

/* ======================== STRUCTS ======================== */

typedef struct
{
  time_t  timestamp;
  float   spot_price;

} Elprisjustnu_Spot_Price;

typedef struct
{
  Elprisjustnu_Spot_Price* prices; 
  int                      prices_c;
  char                     currency[4];
  uint8_t                  price_class;  // 1-4, i.e SE1, SE2 etc.

} Elprisjustnu_Spots;

/* ======================= INTERFACE ======================= */

int elprisjustnu_init(Elprisjustnu_Spots* _Elpris_Spot);

int elprisjustnu_update(Elprisjustnu_Spots* _Elpris_Spot);

int elprisjustnu_parse(Elprisjustnu_Spots* _Elpris_Spot, 
                       Electricity_Spots* _Spot);

void elprisjustnu_dispose(Elprisjustnu_Spots* _Elpris_Spot);

/* ========================================================= */


/*
"Morgondagens elpris anländer tidigast kl 13 dagen innan." 
Example json: 
[
  {
    "SEK_per_kWh": 1.08563,
    "EUR_per_kWh": 0.10257,
    "EXR": 10.584272,       // EXR = Växelkursen som används för denna dag från EUR till SEK. 
    "time_start": "2026-01-26T00:00:00+01:00",
    "time_end": "2026-01-26T00:15:00+01:00"
  },
  ...
]
*/

#endif
