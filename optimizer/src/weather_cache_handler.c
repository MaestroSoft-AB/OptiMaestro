#include "weather_cache_handler.h"

#include <stdio.h>
#include <unistd.h>

int wch_init(Weather_Cache_Handler* _ECH)
{
  return 0;
}

int wch_update_cache(Weather_Cache_Handler* _ECH)
{
  for (int i = 0; i < 5; i++)
  {
    printf("Updating weather cache...\r\n");
    sleep(1);
  }
  return 0;
}

void wch_dispose(Weather_Cache_Handler* _ECH)
{

  _ECH = NULL;
}

