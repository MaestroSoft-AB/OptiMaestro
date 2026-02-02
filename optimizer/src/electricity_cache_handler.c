#include "electricity_cache_handler.h"
#include "maestroutils/error.h"
#include "maestroutils/time_utils.h"
#include "maestroutils/file_utils.h"
#include "maestroutils/time_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define PRICE_CLASS 3 // This should be read from config
#define ECH_BASE_CACHE_PATH "./data/cache"

/* ----------------------- Internal functions -----------------------*/

int ech_validate_cache(const char* _cache_path);

const char* ech_get_cache_filepath(const char* _base_path, uint8_t _price_class, time_t _start, time_t _end);

/* ----------------------------------------------------------------- */

int ech_init(Electricity_Cache_Handler* _ECH)
{
  memset(_ECH, 0, sizeof(Electricity_Cache_Handler));
  memset(&_ECH->spot, 0, sizeof(Electricity_Spots));

  return SUCCESS;
}

int ech_update_cache(Electricity_Cache_Handler* _ECH)
{
  printf("updating electricity cache!\r\n");

  /* Free previous path allocation */
  if (_ECH->cache_path != NULL)
    free((void*)_ECH->cache_path);

/*
  time_t now = time(NULL);
  struct tm local_now;
  localtime_r(&now, &local_now);

  struct tm local_tmrw = local_now;
  local_tmrw.tm_yday++;

  char startdate[ISO_DATE_STRING_LEN + 1];
  char enddate[ISO_DATE_STRING_LEN + 1];

  snprintf(startdate, ISO_DATE_STRING_LEN + 1, ISO_DATE_STRING,
      local_now.tm_year, local_now.tm_mon + 1, local_now.tm_mday);

  snprintf(enddate, ISO_DATE_STRING_LEN + 1, ISO_DATE_STRING,
      local_tmrw.tm_year, local_tmrw.tm_mon + 1, local_tmrw.tm_mday);
*/

  time_t today = epoch_now_day();
  time_t tmrw = today + 86400;
  _ECH->cache_path = ech_get_cache_filepath(ECH_BASE_CACHE_PATH, PRICE_CLASS, today, tmrw);
  if (!_ECH->cache_path)
  {
    perror("ech_set_cache_filepath");
    return ERR_FATAL;
  }

  if (ech_validate_cache(_ECH->cache_path) != 0)
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

  printf("%s\r\n", _ECH->cache_path);

  sleep(1); // remove!

  return SUCCESS;
}

const char* ech_get_cache_filepath(const char* _base_path, uint8_t _price_class, time_t _start, time_t _end)
{
   if (_price_class > 5 || _price_class < 1)
     return NULL;

   char path_buf[256];

   const char* start_str = parse_epoch_to_iso_datetime_string(&_start);
   if (start_str == NULL)
     return NULL;
   const char* end_str = parse_epoch_to_iso_datetime_string(&_end);
   if (start_str == NULL) {
     free((void*)start_str);
     return NULL;
   }

   snprintf(path_buf, sizeof(path_buf), 
            "%s/SE%01i_%s_%s.json",
            _base_path,
            _price_class,
            start_str,
            end_str);

   free((void*)start_str);
   free((void*)end_str);

   size_t path_len = strlen(path_buf);
   char* path = malloc(path_len + 1);
   if (!path)
     return NULL;

   memcpy(path, path_buf, path_len);
   path[path_len] = '\0';

   return path;
}

int ech_validate_cache(const char* _cache_path)
{

  int update_hour = 13;

  if (_cache_path)
    return ERR_INVALID_ARG;
  
  if (file_exists(_cache_path))
  {
    // validate fields 
    // check interval
    if (time_is_at_or_after_hour(update_hour))
    {
      //ExtAPI update
      printf("ECH update from ext api");
    }
    return SUCCESS;
  }

  return ERR_NOT_FOUND;
}

void ech_dispose(Electricity_Cache_Handler* _ECH)
{
  printf("disposing ECH, cache path: %s\\r\\r", _ECH->cache_path);
  if (_ECH->cache_path != NULL)
    free((void*)_ECH->cache_path);

  _ECH = NULL;
}
