#ifndef __ELECTRICITY_CACHE_HANDLER_H__
#define __ELECTRICITY_CACHE_HANDLER_H__

typedef struct
{

} Electricity_Cache_Handler;

/* ======================= INTERFACE ======================= */

int ech_init(Electricity_Cache_Handler* _ECH);

int ech_update_cache(Electricity_Cache_Handler* _ECH);

void ech_dispose(Electricity_Cache_Handler* _ECH);

/* ========================================================= */

#endif
