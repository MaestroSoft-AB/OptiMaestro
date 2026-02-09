#include "elprisjustnu.h"
#include "data/electricity_structs.h"
#include "maestroutils/error.h"
#define MAESTROUTILS_WITH_CJSON 1 // get rid of stupid lsp error
#include "maestroutils/json_utils.h"
#include "maestroutils/time_utils.h"

#include "maestromodules/curl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------- Internal --------------------------- */

const char* epjn_get_response_json(const char* _url);

/* ---------------------------------------------------------------- */

int epjn_init(EPJN_Spots* _EPJN)
{
  memset(_EPJN, 0, sizeof(EPJN_Spots));

  return SUCCESS;
}

/** Call epjn API to build struct values */
int epjn_update(EPJN_Spots* _EPJN, 
                        SpotPriceClass _pc, 
                        time_t _date)
{
  if (_EPJN->prices != NULL)
    free(_EPJN->prices); // free if still allocated for some reason

  _EPJN->price_class = _pc;

  /* Define URL and spot price interval */
  struct tm* tm = gmtime(&_date);
  int year = tm->tm_year + 1900;
  int month = tm->tm_mon;
  int day = tm->tm_mday;

  char url[EPJN_URL_LEN + 1];
  snprintf(url, EPJN_URL_LEN, EPJN_URL, 
           year, month, day, (char)(_pc+1+48));

  url[EPJN_URL_LEN] = '\0';

  printf("epjn url: %s\r\n", url);

  // TODO: Add cache funcs, base_path in config, use if available instead of request

  /* Call API for json */
  const char* json = epjn_get_response_json(url);
  if (json == NULL) {
    perror("epjn_get_response_json"); // TODO: Logger
    return ERR_INTERNAL;
  }

  /* Parse json to struct */
  cJSON* Json_Root = cJSON_Parse(json);
  if (Json_Root == NULL) {
    const char* err = cJSON_GetErrorPtr();
    if (err != NULL) 
      fprintf(stderr, "cJSON: %s\n", err); //TODO: Logger
    free((void*)json);
    return ERR_JSON_PARSE;
  }
  free((void*)json);
  
  int arr_c = cJSON_GetArraySize(Json_Root);
  _EPJN->prices = malloc(arr_c * sizeof(EPJN_Spot_Price));
  if (_EPJN->prices == NULL) {
    perror("malloc"); //TODO: Logger
    cJSON_Delete(Json_Root);
    return ERR_NO_MEMORY;
  }

  _EPJN->price_count = arr_c;
  int i;
  for (i = 0; i < arr_c; i++) {
    cJSON* item = cJSON_GetArrayItem(Json_Root, i);
    if (!cJSON_IsObject(item)) {
      fprintf(stderr, "cJSON: Failed to parse %d as object\r\n", i); //TODO: Logger
      cJSON_Delete(Json_Root);
      return ERR_JSON_OBJ_NOT_FOUND;
    }

    _EPJN->prices[i].spot_sek = json_get_double(item, "SEK_per_kWh");
    _EPJN->prices[i].spot_eur = json_get_double(item, "EUR_per_kWh");
    _EPJN->prices[i].exr      = json_get_double(item, "EXR");

    const char* time_start = json_get_string(item, "time_start");
    if (!time_start) {
      perror("json_get_string"); //TODO: Logger
      cJSON_Delete(Json_Root);
      return ERR_NO_MEMORY;
    }
    const char* time_end = json_get_string(item, "time_end");
    if (!time_end) {
      perror("json_get_string"); //TODO: Logger
      free((void*)time_start);
      cJSON_Delete(Json_Root);
      return ERR_NO_MEMORY;
    }

    _EPJN->prices[i].time_start = parse_iso_full_datetime_string_to_epoch(time_start);
    _EPJN->prices[i].time_end = parse_iso_full_datetime_string_to_epoch(time_end);

    if (_EPJN->prices[i].time_start == -1 || _EPJN->prices[i].time_end == -1) {
      fprintf(stderr, "Failed to parse timestamps in epjn (%i)\r\n", i);
      cJSON_Delete(Json_Root);
      return ERR_JSON_PARSE;
    }
  }

  cJSON_Delete(Json_Root);

  return SUCCESS;
}

const char* epjn_get_response_json(const char* _url)
{
  printf("We make it here, url: %s\r\n", _url);
  // TODO: Replace with http_client
  Curl_Data C_Data;
  if (curl_init(&C_Data) != 0)
    return NULL;

  int result = curl_get_response(&C_Data, _url);
  if (result != 0) {
    perror("curl_get_response");
    curl_dispose(&C_Data);
    return NULL;
  }

  char* response = malloc(C_Data.size + 1);
  if (response == NULL) {
    perror("malloc");
    curl_dispose(&C_Data);
    return NULL;
  }

  memcpy(response, C_Data.addr, C_Data.size);
  response[C_Data.size] = '\0';
  curl_dispose(&C_Data);

  printf("===== Elprisjustnu Response JSON =====\n\n%s\n\n", response);
  return response;
}

/** Parses EPJN struct to Spot struct
 * _currency: 0 = SEK, 1 = EUR
 * _Spot->prices will be free()'d if not NULL */
int epjn_parse(EPJN_Spots* _EPJN, 
               Electricity_Spots* _Spots,
               SpotCurrency _currency)
{
  if (!_EPJN || !_Spots)
    return ERR_INVALID_ARG;

  if (_Spots->prices) // free existing prices if still reachable
    free(_Spots->prices);

  /* Set currency */
  if (_currency == SPOT_SEK) 
    _Spots->currency = SPOT_SEK;
  else if (_currency == SPOT_EUR)
    _Spots->currency = SPOT_EUR;
  else {
    fprintf(stderr, "EPJN: Invalid currency (%i)\r\n", _currency); //TODO: Logger
    return ERR_INVALID_ARG;
  }

  /* Set price class */
  int i;
  // for (i = 0; i < 4; i++)
  //   _Spots->price_class[i] = _EPJN->price_class[i];
  _Spots->price_class = 

  /* Allocate prices array */
  _Spots->price_count = _EPJN->price_count;
  _Spots->prices = malloc(_Spots->price_count * sizeof(Electricity_Spot_Price));
  if (!_Spots->prices) {
    perror("malloc"); //TODO: Logger
    return ERR_NO_MEMORY;
  }

  /* Set interval */
  _Spots->interval = 1440 / _Spots->price_count;

  /* Set prices */
  for (i = 0; i < _Spots->price_count; i++) {
    _Spots->prices[i].time_start = _EPJN->prices[i].time_start;
    _Spots->prices[i].time_end = _EPJN->prices[i].time_end;

    if (_Spots->currency == SPOT_SEK)
      _Spots->prices[i].spot_price = _EPJN->prices[i].spot_sek;
    if (_Spots->currency == SPOT_EUR)
      _Spots->prices[i].spot_price = _EPJN->prices[i].spot_eur;
  }

  return SUCCESS;
}

void epjn_dispose(EPJN_Spots* _EPJN)
{
  if (_EPJN->prices)
    free(_EPJN->prices);

  _EPJN = NULL;
}
