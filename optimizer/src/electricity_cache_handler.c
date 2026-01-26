#include "electricity_cache_handler.h"

#include <stdio.h>
#include <unistd.h>

int ech_init(Electricity_Cache_Handler* _ECH)
{
  return 0;
}

int ech_update_cache(Electricity_Cache_Handler* _ECH)
{
  printf("updating electricity cache!\r\n");

  sleep(1);

  return 0;
}

void ech_dispose(Electricity_Cache_Handler* _ECH)
{

}

