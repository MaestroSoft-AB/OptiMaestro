#ifndef __WEATHER_CACHE_HANDLER_H__
#define __WEATHER_CACHE_HANDLER_H__

typedef struct
{

} Weather_Cache_Handler;

/* ============================ INTERFACE ============================ */

int wch_init(Weather_Cache_Handler* _WCH);

int wch_update_cache(Weather_Cache_Handler* _WCH);

void wch_dispose(Weather_Cache_Handler* _WCH);

/* =================================================================== */

#endif
