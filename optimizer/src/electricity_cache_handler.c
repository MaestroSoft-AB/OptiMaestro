#include "electricity_cache_handler.h"
#include "maestroutils/error.h"
#include "maestroutils/time_utils.h"
#include "maestroutils/json_utils.h"

#include <stdio.h>
#include <unistd.h>

/* ----------------------- Internal functions -----------------------*/

int ech_validate_cache(Electricity_Cache_Handler* _ECH);

char* ech_set_cache_filepath(Electricity_Cache_Handler* _ECH);

/* ----------------------------------------------------------------- */

int ech_init(Electricity_Cache_Handler* _ECH)
{

  return SUCCESS;
}

int ech_update_cache(Electricity_Cache_Handler* _ECH, time_t _start, time_t end)
{
  printf("updating electricity cache!\r\n");

  _ECH->cache_path = ech_set_cache_filepath(_ECH);

  if (!_ECH->cache_path)
  {
    perror("ech_set_cache_filepath");
    return ERR_FATAL;
  }

  if (ech_validate_cache(_ECH) != 0)
  {
    // update extAPI cache
    // parse extAPI to our struct and save cache
    // validate again 
  }
  else
  {
    printf("Electricity cache up to date, nothing to do\r\n");
    // parse
  }



  sleep(1);

  return SUCCESS;
}

int ech_validate_cache(const char* _cache_path, time_t _start_time, time_t _end_time)
{

  if (_cache_path)
    return ERR_INVALID_ARG;
  
  

  // Check if file exists
  // validate fields 

  return SUCCESS;
}

/* Returns 0 if valid cache exists */

void ech_dispose(Electricity_Cache_Handler* _ECH)
{

}

