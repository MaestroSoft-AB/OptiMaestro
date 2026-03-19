#ifndef __ELECTRICITY_CACHE_HANDLER_H__
#define __ELECTRICITY_CACHE_HANDLER_H__

#include "data/electricity_structs.h"
#include "elprisjustnu.h"
#include "sqlite_helpers.h"
#include <stdint.h>

/* ======================== STRUCTS ======================== */

typedef struct
{
  const char* data_dir;

  SpotPriceClass price_class;
  SpotCurrency currency;
  SqlHelper* sqlhelper;

} ECH_Conf;

typedef struct
{
  Electricity_Spots spot;
  EPJN_Spots epjn_spot;
  ECH_Conf conf;

  char* cache_path;
  SqlHelper* sqlhelper;
} ECH; // Electricity Cache Handler

/* ======================= INTERFACE ======================= */

int ech_init(ECH* _ECH, const ECH_Conf* _Conf);

int ech_update_cache(ECH* _ECH);

int ech_write_cache_json(const Electricity_Spots* _Spot, const char* _cache_path);
int ech_read_cache(Electricity_Spots* _Spot, const char* _cache_path);
char* ech_get_cache_filepath(const char* _base_path, const time_t _start,
                             const SpotPriceClass _price_class, const SpotCurrency _currency);

int ech_get_spots_range(SqlHelper* _H, Electricity_Spots* _S, const char* _spots_dir,
                        SpotPriceClass _price_class, SpotCurrency _currency, time_t _start,
                        time_t _end);
void ech_dispose(ECH* _ECH_Ptr);

/* ========================================================= */

#endif
