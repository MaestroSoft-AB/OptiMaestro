#ifndef __ELECTRICITY_CACHE_HANDLER_H__
#define __ELECTRICITY_CACHE_HANDLER_H__

#include "data/electricity_structs.h"
#include "elprisjustnu.h"

#include <stdint.h>

/* ======================== STRUCTS ======================== */

typedef struct
{
  SpotPriceClass  price_class;
  SpotCurrency    currency;

} ECH_Conf;

typedef struct
{
  Electricity_Spots   spot;  
  EPJN_Spots          epjn_spot;

  const char*         cache_path;

} ECH; // Electricity Cache Handler

/* ======================= INTERFACE ======================= */

int ech_init(ECH* _ECH);

int ech_update_cache(ECH* _ECH, const ECH_Conf* _Conf);

int ech_write_cache_json(const Electricity_Spots* _Spot, const char* _cache_path);
int ech_read_cache(Electricity_Spots* _Spot, const char* _cache_path);

void ech_dispose(ECH* _ECH_Ptr);

/* ========================================================= */

#endif
