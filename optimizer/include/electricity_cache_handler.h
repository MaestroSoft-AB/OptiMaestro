#ifndef __ELECTRICITY_CACHE_HANDLER_H__
#define __ELECTRICITY_CACHE_HANDLER_H__

#include "data_structs.h"

#include <stdint.h>
#include <time.h>

/* ======================== STRUCTS ======================== */

typedef struct
{
  Electricity_Spots spot;  
  const char*       cache_path;

} Electricity_Cache_Handler;

/* ======================= INTERFACE ======================= */

int ech_init(Electricity_Cache_Handler* _ECH);

int ech_update_cache(Electricity_Cache_Handler* _ECH);

void ech_dispose(Electricity_Cache_Handler* _ECH_Ptr);

/* ========================================================= */

#endif
