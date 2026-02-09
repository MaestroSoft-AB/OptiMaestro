#ifndef __ELECTRICITY_CACHE_HANDLER_H__
#define __ELECTRICITY_CACHE_HANDLER_H__

#include "data/electricity_structs.h"
#include "elprisjustnu.h"

#include <stdint.h>
#include <time.h>

/* ======================== STRUCTS ======================== */

typedef struct
{
  Electricity_Spots   spot;  
  EPJN_Spots          epjn_spot;

  const char*         cache_path;

} Electricity_Cache_Handler;

/* ======================= INTERFACE ======================= */

int ech_init(Electricity_Cache_Handler* _ECH);

int ech_update_cache(Electricity_Cache_Handler* _ECH);

int ech_write_cache(Electricity_Spots* _Spot, const char* _cache_path);
int ech_read_cache(Electricity_Spots* _Spot, const char* _cache_path);
int ech_parse_json(Electricity_Spots* _Spot, const char* _json_str);

void ech_dispose(Electricity_Cache_Handler* _ECH_Ptr);

/* ========================================================= */

#endif
