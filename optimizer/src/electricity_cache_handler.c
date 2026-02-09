#include "electricity_cache_handler.h"
#include "data/electricity_structs.h"
#include "maestroutils/error.h"
#include "maestroutils/time_utils.h"
#include "maestroutils/file_utils.h"
#include "maestroutils/time_utils.h"
#define MAESTROUTILS_WITH_CJSON 1 // get rid of stupid lsp error
#include "maestroutils/json_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --------------------------- Internal --------------------------- */

#define ECH_BASE_CACHE_PATH "../data/spots" // TODO: Move to conf

const char* ech_get_cache_filepath(const char* _base_path,
                                   time_t _start,
                                   SpotPriceClass _price_class, 
                                   SpotCurrency _currency);

int ech_validate_cache(const char* _cache_path);

/* ---------------------------------------------------------------- */

int ech_init(Electricity_Cache_Handler* _ECH)
{
  memset(_ECH, 0, sizeof(Electricity_Cache_Handler));
  memset(&_ECH->spot, 0, sizeof(Electricity_Spots));
  memset(&_ECH->epjn_spot, 0, sizeof(EPJN_Spots));

  create_directory_if_not_exists(ECH_BASE_CACHE_PATH);

  return SUCCESS;
}

int ech_update_cache(Electricity_Cache_Handler* _ECH)
{
  printf("Updating electricity cache...\r\n");

  // TODO: Get price class dynamically or just fetch cache for all four in every run
  _ECH->spot.price_class = SE3; 

  /* Free previous path allocation */
  if (_ECH->cache_path != NULL)
    free((void*)_ECH->cache_path);

  time_t today = epoch_now_day();
  time_t tmrw = today + 86400;

  int update_hour = 13; // Now the question is what time(s) ech should be run.. 
  if (time_is_at_or_after_hour(update_hour)) {
    printf("Fetching tomorrow's spots...\r\n");
    _ECH->cache_path = ech_get_cache_filepath(ECH_BASE_CACHE_PATH, tmrw, _ECH->spot.price_class, SPOT_SEK);
  }
  else {
    printf("Fetching today's spots...\r\n");
    _ECH->cache_path = ech_get_cache_filepath(ECH_BASE_CACHE_PATH, today, _ECH->spot.price_class, SPOT_SEK);
  }
  
  if (!_ECH->cache_path) {
    perror("ech_set_cache_filepath");
    return ERR_FATAL;
  }

  printf("ECH cache_path: %s\r\n", _ECH->cache_path);

  int res = ech_validate_cache(_ECH->cache_path);
  if (res != SUCCESS) {
    printf("No up to date electricity cache found, fetching..\r\n");
    /* Get updated spots from extAPI and parse it */
    res = epjn_init(&_ECH->epjn_spot);
    if (res != 0) {
      fprintf(stderr, "epjn_init (%i)", res); //TODO: Logger
      return res;
    }
    
    res = epjn_update(&_ECH->epjn_spot, _ECH->spot.price_class, today);
    if (res != 0) {
      fprintf(stderr, "epjn_update (%i)", res); //TODO: Logger
      return res;
    }
    
    res = epjn_parse(&_ECH->epjn_spot, &_ECH->spot, SPOT_SEK); // TODO: set currency from config
    if (res != 0) {
      fprintf(stderr, "epjn_parse (%i)", res); //TODO: Logger
      return res;
    }

    epjn_dispose(&_ECH->epjn_spot);

    /* Write the parsed data to cache */
    res = ech_write_cache(&_ECH->spot, _ECH->cache_path);
    if (res != 0) {
      fprintf(stderr, "epjn_init (%i)", res); //TODO: Logger
      return res;
    }
    
  } 
  else {
    printf("Electricity cache up to date, parse it..\r\n");
    res = ech_read_cache(&_ECH->spot, _ECH->cache_path);
    if (res != 0) {
      fprintf(stderr, "ech_read_cache (%i)", res); //TODO: Logger
      return res;
    }
  }

  printf("Electricity Cache updated! Example:\r\n");
  printf("time=%lu   price=%f   currency=%i\r\n", _ECH->spot.prices[5].time_start, _ECH->spot.prices[5].spot_price, _ECH->spot.currency);

  return SUCCESS;
}

/* Define and return the cache path from given parameters */
/* TODO: create recursive directories for year/month */
const char* ech_get_cache_filepath(const char* _base_path, time_t _start, SpotPriceClass _price_class, SpotCurrency _currency)
{
  char path_buf[512];

  /* Set timestamp (should probably shorten) */
  const char* start_str = parse_epoch_to_iso_datetime_string(&_start);
  if (start_str == NULL)
    return NULL;

  /* Set currency, only SEK and EUR for now */
  char* currency;
  if (_currency == SPOT_SEK)
    currency = "SEK";
  else if (_currency == SPOT_EUR)
    currency = "EUR";
  else {
    fprintf(stderr, "ech_get_cache_filepath: Invalid currency (%i)\r\n", _currency);
    return NULL;
  }

  /* Build path name */
  snprintf(path_buf, sizeof(path_buf), 
          "%s/SE%01i-%s_%s.json",
          _base_path,
          _price_class + 1,
          currency,
          start_str);

  free((void*)start_str);

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
  if (!_cache_path)
    return ERR_INVALID_ARG;
  
  if (file_exists(_cache_path)) {
    // TODO: Actually validate something?
    return SUCCESS;
  }
  return ERR_NOT_FOUND;
}

int ech_write_cache(Electricity_Spots* _Spot, const char* _cache_path)
{
  if (!_Spot || !_cache_path)
    return ERR_INVALID_ARG;
 
  cJSON* Json_Root = cJSON_CreateObject();
  if (Json_Root == NULL) {
    const char* err = cJSON_GetErrorPtr();
    if (err != NULL) 
      fprintf(stderr, "cJSON: %s\n", err); //TODO: Logger
    return ERR_JSON_PARSE;
  }

  // cJSON* Json_Units = cJSON_CreateObject();
  // cJSON_AddItemToObject(Json_Root, "units", Json_Units);

  cJSON* Json_Spots = cJSON_AddArrayToObject(Json_Root, "spots");
  
  int i;
  for (i = 0; i < _Spot->price_count; i++) {
    Electricity_Spot_Price Prices = _Spot->prices[i];
    cJSON* Json_Spot = cJSON_CreateObject();

    const char* time_start = parse_epoch_to_iso_full_datetime_string(&Prices.time_start, 0);
    const char* time_end = parse_epoch_to_iso_full_datetime_string(&Prices.time_start, 0);

    // DEBUG: have to make sure times are correct everywhere, w. timezone n shiet
    printf("time_start: %s | time_end: %s \r\n", time_start, time_end);
    
    cJSON_AddStringToObject(Json_Spot, "time_start", time_start);
    free((void*)time_start);
    cJSON_AddStringToObject(Json_Spot, "time_end", time_end);
    free((void*)time_end);

    cJSON_AddNumberToObject(Json_Spot, "spot_price", Prices.spot_price);

    cJSON_AddItemToArray(Json_Spots, Json_Spot);
  }

  char* json_str = cJSON_Print(Json_Root);

  printf("\r\n--- Writing to %s ---\r\n%s\r\n", _cache_path, json_str);

  if (write_string_to_file(json_str, _cache_path) != 0)
    fprintf(stderr, "Failed to write string \"%p\" to cache \"%p\"\n", json_str, _cache_path); 

  free(json_str);

  cJSON_Delete(Json_Root);

  return SUCCESS;
}

int ech_read_cache(Electricity_Spots* _Spot, const char* _cache_path)
{
  if (!_Spot || !_cache_path)
    return ERR_INVALID_ARG;

  const char* cache_str = read_file_to_string(_cache_path);
  if (cache_str == NULL)
    return false;

  /* Parse json, can be switched if cache medium changes */
  if (ech_parse_json(_Spot, cache_str) != 0) {
    perror("ech_parse_spot");
    free((void*)cache_str);
    return ERR_INTERNAL;
  }
  free((void*)cache_str);

  return SUCCESS;
}

int ech_parse_json(Electricity_Spots* _Spot, const char* _json_str)
{
  if (!_Spot || !_json_str)
    return ERR_INVALID_ARG;

  if (_Spot->prices != NULL)
    free(_Spot->prices); // free if still allocated
  
  /* Parse json to struct */
  cJSON* Json_Root = cJSON_Parse(_json_str);
  if (Json_Root == NULL) {
    const char* err = cJSON_GetErrorPtr();
    if (err != NULL) 
      fprintf(stderr, "cJSON: %s\n", err); //TODO: Logger
    return ERR_JSON_PARSE;
  }

  cJSON* Json_Spots = cJSON_GetObjectItem(Json_Root, "spots");
  if (!Json_Spots || !cJSON_IsArray(Json_Spots)) {
      fprintf(stderr, "cJSON: Unexpected json format, no array found\n"); //TODO: Logger
      cJSON_Delete(Json_Root);
      return ERR_JSON_PARSE;
  }
  int arr_c = cJSON_GetArraySize(Json_Spots);
  _Spot->prices = malloc(arr_c * sizeof(Electricity_Spot_Price));
  if (_Spot->prices == NULL) {
    perror("malloc"); //TODO: Logger
    cJSON_Delete(Json_Root);
    return ERR_NO_MEMORY;
  }

  _Spot->price_count = arr_c;
  int i;
  for (i = 0; i < arr_c; i++) {
    cJSON* Spot = cJSON_GetArrayItem(Json_Spots, i);
    if (!Spot) {
      fprintf(stderr, "cJSON: Failed to parse item %d\r\n", i); //TODO: Logger
      cJSON_Delete(Json_Root);
      return ERR_JSON_OBJ_NOT_FOUND;
    }

    _Spot->prices[i].spot_price = json_get_double(Spot, "spot_price");

    const char* time_start = json_get_string(Spot, "time_start");
    if (!time_start) {
      perror("json_get_string"); //TODO: Logger
      cJSON_Delete(Json_Root);
      return ERR_JSON_PARSE;
    }
    const char* time_end = json_get_string(Spot, "time_end");
    if (!time_end) {
      perror("json_get_string"); //TODO: Logger
      free((void*)time_start);
      cJSON_Delete(Json_Root);
      return ERR_JSON_PARSE;
    }

    // DEBUG: have to make sure times are correct everywhere, w. timezone n shiet
    // printf("time_start: %s | time_end: %s \r\n", time_start, time_end);

    _Spot->prices[i].time_start = parse_iso_full_datetime_string_to_epoch(time_start);
    _Spot->prices[i].time_end = parse_iso_full_datetime_string_to_epoch(time_end);

    if (_Spot->prices[i].time_start == -1 || _Spot->prices[i].time_end == -1) {
      fprintf(stderr, "Failed to parse timestamps in ech (%i)\r\n", i);
      cJSON_Delete(Json_Root);
      return ERR_JSON_PARSE;
    }
  }

  cJSON_Delete(Json_Root);

  return SUCCESS;
}

void ech_dispose(Electricity_Cache_Handler* _ECH)
{
  if (_ECH->cache_path != NULL)
    free((void*)_ECH->cache_path);

  if (_ECH->spot.prices != NULL)
    free(_ECH->spot.prices);

  _ECH = NULL;
}
