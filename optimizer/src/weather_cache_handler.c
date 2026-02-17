#include "weather_cache_handler.h"

#include <stdio.h>
#include <unistd.h>

int wch_init(WCH* _WCH)
{
  _WCH = NULL;
  return 0;
}

int wch_update_cache(WCH* _WCH, WCH_Conf* _Conf)
{
  _WCH = NULL;
  printf("Updating weather cache...\r\n");
  sleep(1);
  return 0;
}

void wch_dispose(WCH* _WCH)
{

  _WCH = NULL;
}

