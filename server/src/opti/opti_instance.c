#include "opti/opti_instance.h"
#include "opti/opti_server.h"
#include "data/calc_name.h"
#include "data/electricity_structs.h"
#include "data/facility.h"
#include "data/meter_reading.h"
#include "data/weather_structs.h"
#include "maestromodules/http_parser.h"
#include "maestroutils/json_utils.h"
#include "maestroutils/time_utils.h"
#include "unix_domain_socket.h"
#include <dirent.h>
#include <maestroutils/file_utils.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define OPTI_AVERAGE_PATH "/var/lib/maestro/calcs/"
#define OPTI_CONFIG_PATH "/etc/maestro/optimizer.conf"
#define OPTI_FACILITY_CONF_DIR_FALLBACK "/etc/maestro/facility"
#define OPTI_CONFIG_EDITABLE_COUNT 8

static const char* osi_blank_facility_config = "name=\n"
                                               "currency=SEK\n"
                                               "energy_zone=3\n"
                                               "latitude=0\n"
                                               "longitude=0\n"
                                               "panel.tilt=0\n"
                                               "panel.azimuth=0\n"
                                               "panel.m2_size=0\n";

static int         osi_append_text(char** buffer, size_t* used, size_t* capacity, const char* text,
                                   size_t text_len);
static int         osi_get_facility_config_dir(char* dir_out, size_t dir_out_size);
static int         osi_parse_time_range(HTTP_Request* req, time_t* start_out, time_t* end_out);
static int         osi_get_default_facility_name(char* name_out, size_t name_out_size);
static int         osi_load_request_facility(HTTP_Request* req, Facility_Config*** configs_out,
                                             size_t* count_out, Facility_Config** facility_out);
static void        osi_default_forecast_range(HTTP_Request* req, time_t* start, time_t* end);
static const char* osi_get_query_param_any(HTTP_Request* req, const char* const* keys);
static int         osi_parse_query_double(HTTP_Request* req, const char* const* keys, double* out);
static int         osi_parse_query_int(HTTP_Request* req, const char* const* keys, int* out);
static int         osi_weather_to_json(const Weather* weather, int forecast, char** body_out);
static int         osi_spots_to_json(const Electricity_Spots* spots, char** body_out);
static void        osi_weather_dispose(Weather* weather);
static void        osi_spots_dispose(Electricity_Spots* spots);
static int osi_parse_meter_reading_json(const char* body, int body_len, Meter_Reading* reading);
static int osi_meter_reading_to_json(const Meter_Reading* reading, time_t received_at,
                                     char** body_out);

//-----------------Internal Functions-----------------
//
int osi_on_http_connection(void* _context, HTTP_Server_Connection* _Connection);
int osi_on_request(void* _context);
int osi_on_dispose(void* _context);

void                    osi_taskwork(void* _context, uint64_t _montime);
OptiServerInstanceState worktask_request_parse(Opti_Server_Instance* _Instance);
OptiServerInstanceState worktask_response_build(Opti_Server_Instance* _Instance);

static int osi_weather_to_txt(const Weather* weather, int forecast, char** body_out) {
  if (!weather || !body_out) {
    return ERR_INVALID_ARG;
  }

  *body_out = NULL;

  size_t capacity = 128 + ((size_t)weather->count * 64);
  char*  body     = (char*)malloc(capacity);
  if (!body) {
    return ERR_NO_MEMORY;
  }

  size_t used = 0;

  int written =
      snprintf(body, capacity, "M,%u,%d,%d\n", weather->count, forecast, weather->update_interval);

  if (written < 0 || (size_t)written >= capacity) {
    free(body);
    return ERR_NO_MEMORY;
  }

  used = (size_t)written;

  for (unsigned int i = 0; i < weather->count; ++i) {
    const Weather_Values* value = &weather->values[i];

    written = snprintf(body + used, capacity - used, "V,%lld,%.1f,%.2f,%.1f,%.0f\n",
                       (long long)value->timestamp, value->temperature, value->precipitation,
                       value->windspeed, value->radiation_shortwave);

    if (written < 0 || (size_t)written >= capacity - used) {
      free(body);
      return ERR_NO_MEMORY;
    }

    used += (size_t)written;
  }

  *body_out = body;
  return SUCCESS;
}

//----------------------------------------------------

/* Functions to be called on specific path from request */
int osi_get_solar_data(Osi_RequestCtx* _ctx);
int osi_get_temp_1_data(Osi_RequestCtx* _ctx);
int osi_get_jacuzzi_data(Osi_RequestCtx* _ctx);
int osi_get_overview(Osi_RequestCtx* _ctx);
int osi_get_average_daily(Osi_RequestCtx* _ctx);
int osi_get_average_hourly(Osi_RequestCtx* _ctx);
int osi_get_config(Osi_RequestCtx* _ctx);
int osi_post_config(Osi_RequestCtx* _ctx);
int osi_recalc(Osi_RequestCtx* _ctx);
int osi_kill(Osi_RequestCtx* _ctx);
int osi_post_ingest(Osi_RequestCtx* _ctx);
int osi_get_weather_cache(Osi_RequestCtx* _ctx);
int osi_get_spot_cache(Osi_RequestCtx* _ctx);
int osi_get_power_current(Osi_RequestCtx* _ctx);
int osi_get_display_current(Osi_RequestCtx* _ctx);
int osi_get_display_graph_hour(Osi_RequestCtx* _ctx);
/* REMEMBER TO CHANGE COUNT WHEN ADDING ENDPOINT! */
#define ENDPOINTS_COUNT 17
int osi_get_facilities(Osi_RequestCtx* _ctx);


const Device_API_Endpoint Endpoints[ENDPOINTS_COUNT] = {
    {
        "/solar-cell",
        HTTP_GET,
        osi_get_solar_data,
    },
    {
        "/temp-sensor-1",
        HTTP_GET,
        osi_get_temp_1_data,
    },
    {
        "/jacuzzi",
        HTTP_GET,
        osi_get_jacuzzi_data,
    },
    {
        "/overview",
        HTTP_GET,
        osi_get_overview,
    },
    {
        "/average-daily",
        HTTP_GET,
        osi_get_average_daily,
    },
    {
        "/average-hourly",
        HTTP_GET,
        osi_get_average_hourly,
    },
    {
        "/config",
        HTTP_GET,
        osi_get_config,
    },
    {
        "/config",
        HTTP_POST,
        osi_post_config,
    },
    {
        "/recalc",
        HTTP_GET,
        osi_recalc,
    },
    {
        "/kill",
        HTTP_GET,
        osi_kill,
    },
    {
        "/facilities",
        HTTP_GET,
        osi_get_facilities,
    },
    {
        "/ingest",
        HTTP_POST,
        osi_post_ingest,
    },
    {
        "/weather/cache",
        HTTP_GET,
        osi_get_weather_cache,
    },
    {
        "/spot-price/cache",
        HTTP_GET,
        osi_get_spot_cache,
    },
    {
        "/power/current",
        HTTP_GET,
        osi_get_power_current,
    },
    {
        "/display/current",
        HTTP_GET,
        osi_get_display_current,
    },
    {
        "/display/graph/hour",
        HTTP_GET,
        osi_get_display_graph_hour,
    },


};

static Meter_Reading osi_latest_meter_reading;
static time_t        osi_latest_meter_reading_received_at;
static int           osi_has_latest_meter_reading = 0;

//--------------------------------------------------------------------------//

/*******************SOME RESPONSEBUILDING, MAYBE MOVE THIS?****************/
static int osi_set_response(HTTP_Server_Connection* _Conn, int _status_code,
                            const char* _content_type, const char* _body) {
  if (!_Conn || !_Conn->response || !_body || !_content_type) {
    return ERR_INVALID_ARG;
  }

  _Conn->response->status_code = _status_code;

  const char* reason = HttpStatus_reasonPhrase(_status_code);
  if (!reason) {
    reason = "OK";
  }

  int body_len = (int)strlen(_body);

  size_t needed_size = snprintf(NULL, 0,
                                "HTTP/1.1 %i %s\r\n"
                                "Content-Type: %s\r\n"
                                "Content-Length: %d\r\n"
                                "Connection: close\r\n"
                                "\r\n"
                                "%s",
                                _status_code, reason, _content_type, body_len, _body);

  char* resp = (char*)malloc(needed_size + 1);
  if (!resp) {
    return ERR_NO_MEMORY;
  }

  snprintf(resp, needed_size + 1,
           "HTTP/1.1 %i %s\r\n"
           "Content-Type: %s\r\n"
           "Content-Length: %d\r\n"
           "Connection: close\r\n"
           "\r\n"
           "%s",
           _status_code, reason, _content_type, body_len, _body);

  if (_Conn->response->full_response) {
    free(_Conn->response->full_response);
    _Conn->response->full_response = NULL;
  }

  _Conn->response->full_response = resp;
  _Conn->weather_done            = 1; // Change this to a more appropriate name

  return SUCCESS;
}

static const char* osi_get_request_body(Osi_RequestCtx* _ctx, int* _body_len) {
  if (!_ctx || !_ctx->conn || !_body_len) {
    return NULL;
  }

  *_body_len = 0;

  if (_ctx->conn->content_length <= 0 || _ctx->conn->tcp_client.data.addr == NULL) {
    return NULL;
  }

  *_body_len = _ctx->conn->content_length;
  return (const char*)_ctx->conn->tcp_client.data.addr;
}

static const char* osi_get_query_param(HTTP_Request* req, const char* key) {
  if (!req || !key || !req->params) {
    return NULL;
  }

  linked_list_foreach(req->params, node) {
    HTTP_Key_Value* param = (HTTP_Key_Value*)node->item;
    if (param && param->key && param->value && strcmp(param->key, key) == 0) {
      return param->value;
    }
  }

  return NULL;
}

static int osi_parse_time_range(HTTP_Request* req, time_t* start_out, time_t* end_out) {
  if (!start_out || !end_out) {
    return ERR_INVALID_ARG;
  }

  time_t now   = time(NULL);
  time_t end   = now;
  time_t start = now - 86400;

  const char* range = osi_get_query_param(req, "range");
  if (range) {
    if (strcmp(range, "7d") == 0) {
      start = now - (7 * 86400);
    } else if (strcmp(range, "30d") == 0) {
      start = now - (30 * 86400);
    } else if (strcmp(range, "24h") == 0) {
      start = now - 86400;
    }
  }

  const char* from = osi_get_query_param(req, "from");
  const char* to   = osi_get_query_param(req, "to");
  if (from && from[0] != '\0') {
    start = (time_t)atoll(from);
  }
  if (to && to[0] != '\0') {
    end = (time_t)atoll(to);
  }

  if (start <= 0 || end <= start) {
    return ERR_INVALID_ARG;
  }

  *start_out = start;
  *end_out   = end;
  return SUCCESS;
}

static void osi_default_forecast_range(HTTP_Request* req, time_t* start, time_t* end) {
  if (!req || !start || !end || osi_get_query_param(req, "from") || osi_get_query_param(req, "to")) {
    return;
  }

  time_t now = time(NULL);
  const char* range = osi_get_query_param(req, "range");
  if (range && strcmp(range, "7d") == 0) {
    *start = now;
    *end = now + (7 * 86400);
  } else if (range && strcmp(range, "30d") == 0) {
    *start = now;
    *end = now + (30 * 86400);
  } else if (range && strcmp(range, "24h") == 0) {
    *start = now;
    *end = now + 86400;
  } else {
    *start = now;
    *end = now + 86400;
  }
}

static int osi_load_request_facility(HTTP_Request* req, Facility_Config*** configs_out,
                                     size_t* count_out, Facility_Config** facility_out) {
  if (!configs_out || !count_out || !facility_out) {
    return ERR_INVALID_ARG;
  }

  *configs_out  = NULL;
  *count_out    = 0;
  *facility_out = NULL;

  char facility_dir[256] = {0};
  int  dir_result        = osi_get_facility_config_dir(facility_dir, sizeof(facility_dir));
  if (dir_result != SUCCESS) {
    return dir_result;
  }

  Facility_Config** configs = facility_get_configs(facility_dir, count_out);
  if (!configs || *count_out == 0) {
    return ERR_NOT_FOUND;
  }

  const char* requested_name = osi_get_query_param(req, "name");
  if (!requested_name || requested_name[0] == '\0') {
    for (size_t i = 0; i < *count_out; ++i) {
      if (!configs[i]) {
        continue;
      }

      *configs_out  = configs;
      *facility_out = configs[i];
      return SUCCESS;
    }
  } else {
    for (size_t i = 0; i < *count_out; ++i) {
      if (configs[i] && configs[i]->name && strcmp(configs[i]->name, requested_name) == 0) {
        *configs_out  = configs;
        *facility_out = configs[i];
        return SUCCESS;
      }
    }
  }

  facility_dispose(configs, *count_out);
  *count_out = 0;
  return ERR_NOT_FOUND;
}

static const char* osi_get_query_param_any(HTTP_Request* req, const char* const* keys) {
  if (!req || !keys) {
    return NULL;
  }

  for (int i = 0; keys[i] != NULL; ++i) {
    const char* value = osi_get_query_param(req, keys[i]);
    if (value && value[0] != '\0') {
      return value;
    }
  }

  return NULL;
}

static int osi_parse_query_double(HTTP_Request* req, const char* const* keys, double* out) {
  if (!out) {
    return 0;
  }

  const char* value = osi_get_query_param_any(req, keys);
  if (!value) {
    return 0;
  }

  char*  end    = NULL;
  double parsed = strtod(value, &end);
  if (end == value || !end || *end != '\0') {
    return 0;
  }

  *out = parsed;
  return 1;
}

static int osi_parse_query_int(HTTP_Request* req, const char* const* keys, int* out) {
  if (!out) {
    return 0;
  }

  const char* value = osi_get_query_param_any(req, keys);
  if (!value) {
    return 0;
  }

  char* end    = NULL;
  long  parsed = strtol(value, &end, 10);
  if (end == value || !end || *end != '\0') {
    return 0;
  }

  *out = (int)parsed;
  return 1;
}

static int osi_weather_to_json(const Weather* weather, int forecast, char** body_out) {
  if (!weather || !body_out) {
    return ERR_INVALID_ARG;
  }

  *body_out     = NULL;
  cJSON* root   = cJSON_CreateObject();
  cJSON* meta   = cJSON_CreateObject();
  cJSON* values = cJSON_CreateArray();
  if (!root || !meta || !values) {
    cJSON_Delete(root);
    cJSON_Delete(meta);
    cJSON_Delete(values);
    return ERR_NO_MEMORY;
  }

  cJSON_AddNumberToObject(meta, "count", weather->count);
  cJSON_AddNumberToObject(meta, "forecast", forecast);
  cJSON_AddNumberToObject(meta, "interval_minutes", weather->update_interval);
  cJSON_AddNumberToObject(meta, "latitude", weather->latitude);
  cJSON_AddNumberToObject(meta, "longitude", weather->longitude);
  cJSON_AddNumberToObject(meta, "solar_panel_tilt", weather->panel_tilt);
  cJSON_AddNumberToObject(meta, "solar_panel_azimuth", weather->panel_azimuth);
  cJSON_AddStringToObject(meta, "temperature_unit",
                          weather->temperature_unit ? weather->temperature_unit : "");
  cJSON_AddStringToObject(meta, "windspeed_unit",
                          weather->windspeed_unit ? weather->windspeed_unit : "");
  cJSON_AddStringToObject(meta, "precipitation_unit",
                          weather->precipitation_unit ? weather->precipitation_unit : "");
  cJSON_AddStringToObject(meta, "winddirection_unit",
                          weather->winddirection_unit ? weather->winddirection_unit : "");
  cJSON_AddStringToObject(meta, "radiation_unit",
                          weather->radiation_unit ? weather->radiation_unit : "");
  cJSON_AddItemToObject(root, "meta", meta);

  for (unsigned int i = 0; i < weather->count; ++i) {
    const Weather_Values* value = &weather->values[i];
    cJSON*                item  = cJSON_CreateObject();
    if (!item) {
      cJSON_Delete(root);
      return ERR_NO_MEMORY;
    }

    cJSON_AddNumberToObject(item, "timestamp", (double)value->timestamp);
    char* timestamp_iso = parse_epoch_to_iso_full_datetime_string(&value->timestamp, 0);
    if (timestamp_iso) {
      cJSON_AddStringToObject(item, "timestamp_iso", timestamp_iso);
      free(timestamp_iso);
    }
    cJSON_AddNumberToObject(item, "temperature", value->temperature);
    cJSON_AddNumberToObject(item, "precipitation", value->precipitation);
    cJSON_AddNumberToObject(item, "windspeed", value->windspeed);
    cJSON_AddNumberToObject(item, "winddirection", value->winddirection_azimuth);
    cJSON_AddNumberToObject(item, "radiation_direct", value->radiation_direct);
    cJSON_AddNumberToObject(item, "radiation_direct_n", value->radiation_direct_n);
    cJSON_AddNumberToObject(item, "radiation_diffuse", value->radiation_diffuse);
    cJSON_AddNumberToObject(item, "radiation_shortwave", value->radiation_shortwave);
    cJSON_AddNumberToObject(item, "radiation_tilted", value->radiation_tilted);
    cJSON_AddNumberToObject(item, "sun_duration", value->sun_duration);
    cJSON_AddItemToArray(values, item);
  }

  cJSON_AddItemToObject(root, "values", values);
  *body_out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return *body_out ? SUCCESS : ERR_NO_MEMORY;
}

static int osi_spots_to_json(const Electricity_Spots* spots, char** body_out) {
  if (!spots || !body_out) {
    return ERR_INVALID_ARG;
  }

  *body_out     = NULL;
  cJSON* root   = cJSON_CreateObject();
  cJSON* prices = cJSON_CreateArray();
  if (!root || !prices) {
    cJSON_Delete(root);
    cJSON_Delete(prices);
    return ERR_NO_MEMORY;
  }

  cJSON_AddNumberToObject(root, "energy_zone", ((int)spots->price_class) + 1);
  cJSON_AddStringToObject(root, "unit", spots->unit ? spots->unit : "SEK/kWh");
  cJSON_AddNumberToObject(root, "interval_minutes", spots->interval);
  cJSON_AddNumberToObject(root, "count", spots->price_count);

  for (unsigned int i = 0; i < spots->price_count; ++i) {
    const Electricity_Spot_Price* price = &spots->prices[i];
    cJSON*                        item  = cJSON_CreateObject();
    if (!item) {
      cJSON_Delete(root);
      return ERR_NO_MEMORY;
    }

    cJSON_AddNumberToObject(item, "time_start", (double)price->time_start);
    cJSON_AddNumberToObject(item, "time_end", (double)price->time_end);
    cJSON_AddNumberToObject(item, "spot_price", price->spot_price);
    cJSON_AddItemToArray(prices, item);
  }

  cJSON_AddItemToObject(root, "prices", prices);
  *body_out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return *body_out ? SUCCESS : ERR_NO_MEMORY;
}

static void osi_weather_dispose(Weather* weather) {
  if (!weather) {
    return;
  }

  free(weather->values);
  free((void*)weather->temperature_unit);
  free((void*)weather->windspeed_unit);
  free((void*)weather->precipitation_unit);
  free((void*)weather->winddirection_unit);
  free((void*)weather->radiation_unit);
  memset(weather, 0, sizeof(Weather));
}

static void osi_spots_dispose(Electricity_Spots* spots) {
  if (!spots) {
    return;
  }

  free(spots->prices);
  memset(spots, 0, sizeof(Electricity_Spots));
}

static cJSON* osi_get_json_item_any(cJSON* root, const char* const* keys) {
  if (!root || !keys) {
    return NULL;
  }

  for (int i = 0; keys[i] != NULL; ++i) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, keys[i]);
    if (item) {
      return item;
    }
  }

  return NULL;
}

static int osi_json_read_double_any(cJSON* root, const char* const* keys, double* out) {
  cJSON* item = osi_get_json_item_any(root, keys);
  if (!item || !cJSON_IsNumber(item) || !out) {
    return 0;
  }

  *out = item->valuedouble;
  return 1;
}

static int osi_json_read_uint32_any(cJSON* root, const char* const* keys, uint32_t* out) {
  cJSON* item = osi_get_json_item_any(root, keys);
  if (!item || !cJSON_IsNumber(item) || !out) {
    return 0;
  }

  *out = (uint32_t)item->valuedouble;
  return 1;
}

static int osi_json_read_uint16_any(cJSON* root, const char* const* keys, uint16_t* out) {
  cJSON* item = osi_get_json_item_any(root, keys);
  if (!item || !cJSON_IsNumber(item) || !out) {
    return 0;
  }

  *out = (uint16_t)item->valuedouble;
  return 1;
}

static int osi_json_read_uint8_any(cJSON* root, const char* const* keys, uint8_t* out) {
  cJSON* item = osi_get_json_item_any(root, keys);
  if (!item || !cJSON_IsNumber(item) || !out) {
    return 0;
  }

  *out = (uint8_t)item->valuedouble;
  return 1;
}

static int osi_json_read_string_any(cJSON* root, const char* const* keys, char* out,
                                    size_t out_size) {
  cJSON* item = osi_get_json_item_any(root, keys);
  if (!item || !cJSON_IsString(item) || !item->valuestring || !out || out_size == 0) {
    return 0;
  }

  snprintf(out, out_size, "%s", item->valuestring);
  return 1;
}

static void osi_json_add_string_if_present(cJSON* root, uint64_t flags, uint64_t flag,
                                           const char* key, const char* value) {
  if ((flags & flag) != 0) {
    cJSON_AddStringToObject(root, key, value ? value : "");
  }
}

static void osi_json_add_number_if_present(cJSON* root, uint64_t flags, uint64_t flag,
                                           const char* key, double value) {
  if ((flags & flag) != 0) {
    cJSON_AddNumberToObject(root, key, value);
  }
}

static void osi_json_add_uint_if_present(cJSON* root, uint64_t flags, uint64_t flag,
                                         const char* key, uint32_t value) {
  if ((flags & flag) != 0) {
    cJSON_AddNumberToObject(root, key, value);
  }
}

static int osi_parse_meter_reading_json(const char* body, int body_len, Meter_Reading* reading) {
  if (!body || body_len <= 0 || !reading) {
    return ERR_INVALID_ARG;
  }

  cJSON* root = cJSON_ParseWithLength(body, (size_t)body_len);
  if (!root) {
    return ERR_INVALID_ARG;
  }

  memset(reading, 0, sizeof(Meter_Reading));

  const char* id_keys[]               = {"unique_id", "id", NULL};
  const char* model_keys[]            = {"meter_model", "model", NULL};
  const char* timestamp_keys[]        = {"timestamp", NULL};
  const char* protocol_keys[]         = {"protocol_version", NULL};
  const char* tariff_keys[]           = {"tariff", "active_tariff", NULL};
  const char* energy_import_keys[]    = {"energy_import_kwh", "total_power_import_kwh", NULL};
  const char* energy_import_t1_keys[] = {"energy_import_t1_kwh", "total_power_import_t1_kwh", NULL};
  const char* energy_import_t2_keys[] = {"energy_import_t2_kwh", "total_power_import_t2_kwh", NULL};
  const char* energy_import_t3_keys[] = {"energy_import_t3_kwh", "total_power_import_t3_kwh", NULL};
  const char* energy_import_t4_keys[] = {"energy_import_t4_kwh", "total_power_import_t4_kwh", NULL};
  const char* energy_export_keys[]    = {"energy_export_kwh", "total_power_export_kwh", NULL};
  const char* energy_export_t1_keys[] = {"energy_export_t1_kwh", "total_power_export_t1_kwh", NULL};
  const char* energy_export_t2_keys[] = {"energy_export_t2_kwh", "total_power_export_t2_kwh", NULL};
  const char* energy_export_t3_keys[] = {"energy_export_t3_kwh", "total_power_export_t3_kwh", NULL};
  const char* energy_export_t4_keys[] = {"energy_export_t4_kwh", "total_power_export_t4_kwh", NULL};
  const char* power_keys[]            = {"power_w", "active_power_w", NULL};
  const char* power_l1_keys[]         = {"power_l1_w", "active_power_l1_w", NULL};
  const char* power_l2_keys[]         = {"power_l2_w", "active_power_l2_w", NULL};
  const char* power_l3_keys[]         = {"power_l3_w", "active_power_l3_w", NULL};
  const char* voltage_keys[]          = {"voltage_v", "active_voltage_v", NULL};
  const char* voltage_l1_keys[]       = {"voltage_l1_v", "active_voltage_l1_v", NULL};
  const char* voltage_l2_keys[]       = {"voltage_l2_v", "active_voltage_l2_v", NULL};
  const char* voltage_l3_keys[]       = {"voltage_l3_v", "active_voltage_l3_v", NULL};
  const char* current_keys[]          = {"current_a", "active_current_a", NULL};
  const char* current_l1_keys[]       = {"current_l1_a", "active_current_l1_a", NULL};
  const char* current_l2_keys[]       = {"current_l2_a", "active_current_l2_a", NULL};
  const char* current_l3_keys[]       = {"current_l3_a", "active_current_l3_a", NULL};
  const char* frequency_keys[]        = {"frequency_hz", "active_frequency_hz", NULL};
  const char* voltage_sag_l1_keys[]   = {"voltage_sag_l1_count", NULL};
  const char* voltage_sag_l2_keys[]   = {"voltage_sag_l2_count", NULL};
  const char* voltage_sag_l3_keys[]   = {"voltage_sag_l3_count", NULL};
  const char* voltage_swell_l1_keys[] = {"voltage_swell_l1_count", NULL};
  const char* voltage_swell_l2_keys[] = {"voltage_swell_l2_count", NULL};
  const char* voltage_swell_l3_keys[] = {"voltage_swell_l3_count", NULL};
  const char* any_power_fail_keys[]   = {"any_power_fail_count", "any_power_failure_count", NULL};
  const char* long_power_fail_keys[]  = {"long_power_fail_count", "long_power_failure_count", NULL};
  const char* average_power_15m_keys[]            = {"average_power_15m_w", NULL};
  const char* monthly_power_peak_keys[]           = {"monthly_power_peak_w", NULL};
  const char* monthly_power_peak_timestamp_keys[] = {"monthly_power_peak_timestamp", NULL};

  if (osi_json_read_string_any(root, id_keys, reading->unique_id, sizeof(reading->unique_id))) {
    reading->present_flags |= METER_READING_PRESENT_ID;
  }
  if (osi_json_read_string_any(root, model_keys, reading->meter_model,
                               sizeof(reading->meter_model))) {
    reading->present_flags |= METER_READING_PRESENT_MODEL;
  }
  if (osi_json_read_string_any(root, timestamp_keys, reading->timestamp,
                               sizeof(reading->timestamp))) {
    reading->present_flags |= METER_READING_PRESENT_TIMESTAMP;
  }
  if (osi_json_read_uint16_any(root, protocol_keys, &reading->protocol_version)) {
    reading->present_flags |= METER_READING_PRESENT_PROTOCOL_VERSION;
  }
  if (osi_json_read_uint8_any(root, tariff_keys, &reading->tariff)) {
    reading->present_flags |= METER_READING_PRESENT_TARIFF;
  }
  if (osi_json_read_double_any(root, energy_import_keys, &reading->energy_import_kwh)) {
    reading->present_flags |= METER_READING_PRESENT_ENERGY_IMPORT;
  }
  if (osi_json_read_double_any(root, energy_import_t1_keys, &reading->energy_import_t1_kwh)) {
    reading->present_flags |= METER_READING_PRESENT_ENERGY_IMPORT_T1;
  }
  if (osi_json_read_double_any(root, energy_import_t2_keys, &reading->energy_import_t2_kwh)) {
    reading->present_flags |= METER_READING_PRESENT_ENERGY_IMPORT_T2;
  }
  if (osi_json_read_double_any(root, energy_import_t3_keys, &reading->energy_import_t3_kwh)) {
    reading->present_flags |= METER_READING_PRESENT_ENERGY_IMPORT_T3;
  }
  if (osi_json_read_double_any(root, energy_import_t4_keys, &reading->energy_import_t4_kwh)) {
    reading->present_flags |= METER_READING_PRESENT_ENERGY_IMPORT_T4;
  }
  if (osi_json_read_double_any(root, energy_export_keys, &reading->energy_export_kwh)) {
    reading->present_flags |= METER_READING_PRESENT_ENERGY_EXPORT;
  }
  if (osi_json_read_double_any(root, energy_export_t1_keys, &reading->energy_export_t1_kwh)) {
    reading->present_flags |= METER_READING_PRESENT_ENERGY_EXPORT_T1;
  }
  if (osi_json_read_double_any(root, energy_export_t2_keys, &reading->energy_export_t2_kwh)) {
    reading->present_flags |= METER_READING_PRESENT_ENERGY_EXPORT_T2;
  }
  if (osi_json_read_double_any(root, energy_export_t3_keys, &reading->energy_export_t3_kwh)) {
    reading->present_flags |= METER_READING_PRESENT_ENERGY_EXPORT_T3;
  }
  if (osi_json_read_double_any(root, energy_export_t4_keys, &reading->energy_export_t4_kwh)) {
    reading->present_flags |= METER_READING_PRESENT_ENERGY_EXPORT_T4;
  }
  if (osi_json_read_double_any(root, power_keys, &reading->power_w)) {
    reading->present_flags |= METER_READING_PRESENT_POWER;
  }
  if (osi_json_read_double_any(root, power_l1_keys, &reading->power_l1_w)) {
    reading->present_flags |= METER_READING_PRESENT_POWER_L1;
  }
  if (osi_json_read_double_any(root, power_l2_keys, &reading->power_l2_w)) {
    reading->present_flags |= METER_READING_PRESENT_POWER_L2;
  }
  if (osi_json_read_double_any(root, power_l3_keys, &reading->power_l3_w)) {
    reading->present_flags |= METER_READING_PRESENT_POWER_L3;
  }
  if (osi_json_read_double_any(root, voltage_keys, &reading->voltage_v)) {
    reading->present_flags |= METER_READING_PRESENT_VOLTAGE;
  }
  if (osi_json_read_double_any(root, voltage_l1_keys, &reading->voltage_l1_v)) {
    reading->present_flags |= METER_READING_PRESENT_VOLTAGE_L1;
  }
  if (osi_json_read_double_any(root, voltage_l2_keys, &reading->voltage_l2_v)) {
    reading->present_flags |= METER_READING_PRESENT_VOLTAGE_L2;
  }
  if (osi_json_read_double_any(root, voltage_l3_keys, &reading->voltage_l3_v)) {
    reading->present_flags |= METER_READING_PRESENT_VOLTAGE_L3;
  }
  if (osi_json_read_double_any(root, current_keys, &reading->current_a)) {
    reading->present_flags |= METER_READING_PRESENT_CURRENT;
  }
  if (osi_json_read_double_any(root, current_l1_keys, &reading->current_l1_a)) {
    reading->present_flags |= METER_READING_PRESENT_CURRENT_L1;
  }
  if (osi_json_read_double_any(root, current_l2_keys, &reading->current_l2_a)) {
    reading->present_flags |= METER_READING_PRESENT_CURRENT_L2;
  }
  if (osi_json_read_double_any(root, current_l3_keys, &reading->current_l3_a)) {
    reading->present_flags |= METER_READING_PRESENT_CURRENT_L3;
  }
  if (osi_json_read_double_any(root, frequency_keys, &reading->frequency_hz)) {
    reading->present_flags |= METER_READING_PRESENT_FREQUENCY;
  }
  if (osi_json_read_uint32_any(root, voltage_sag_l1_keys, &reading->voltage_sag_l1_count) ||
      osi_json_read_uint32_any(root, voltage_sag_l2_keys, &reading->voltage_sag_l2_count) ||
      osi_json_read_uint32_any(root, voltage_sag_l3_keys, &reading->voltage_sag_l3_count)) {
    reading->present_flags |= METER_READING_PRESENT_VOLTAGE_SAG;
  }
  if (osi_json_read_uint32_any(root, voltage_swell_l1_keys, &reading->voltage_swell_l1_count) ||
      osi_json_read_uint32_any(root, voltage_swell_l2_keys, &reading->voltage_swell_l2_count) ||
      osi_json_read_uint32_any(root, voltage_swell_l3_keys, &reading->voltage_swell_l3_count)) {
    reading->present_flags |= METER_READING_PRESENT_VOLTAGE_SWELL;
  }
  if (osi_json_read_uint32_any(root, any_power_fail_keys, &reading->any_power_fail_count) ||
      osi_json_read_uint32_any(root, long_power_fail_keys, &reading->long_power_fail_count)) {
    reading->present_flags |= METER_READING_PRESENT_POWER_FAIL;
  }
  if (osi_json_read_double_any(root, average_power_15m_keys, &reading->average_power_15m_w)) {
    reading->present_flags |= METER_READING_PRESENT_AVERAGE_POWER_15M;
  }
  if (osi_json_read_double_any(root, monthly_power_peak_keys, &reading->monthly_power_peak_w) ||
      osi_json_read_string_any(root, monthly_power_peak_timestamp_keys,
                               reading->monthly_power_peak_timestamp,
                               sizeof(reading->monthly_power_peak_timestamp))) {
    reading->present_flags |= METER_READING_PRESENT_MONTHLY_POWER_PEAK;
  }

  cJSON_Delete(root);

  if ((reading->present_flags & METER_READING_PRESENT_POWER) == 0) {
    return ERR_INVALID_ARG;
  }

  return SUCCESS;
}

static int osi_meter_reading_to_json(const Meter_Reading* reading, time_t received_at,
                                     char** body_out) {
  if (!reading || !body_out) {
    return ERR_INVALID_ARG;
  }

  *body_out   = NULL;
  cJSON* root = cJSON_CreateObject();
  if (!root) {
    return ERR_NO_MEMORY;
  }

  cJSON_AddNumberToObject(root, "received_at", (double)received_at);
  cJSON_AddNumberToObject(root, "present_flags", (double)reading->present_flags);

  osi_json_add_string_if_present(root, reading->present_flags, METER_READING_PRESENT_ID,
                                 "unique_id", reading->unique_id);
  osi_json_add_string_if_present(root, reading->present_flags, METER_READING_PRESENT_MODEL,
                                 "meter_model", reading->meter_model);
  osi_json_add_string_if_present(root, reading->present_flags, METER_READING_PRESENT_TIMESTAMP,
                                 "timestamp", reading->timestamp);
  osi_json_add_uint_if_present(root, reading->present_flags, METER_READING_PRESENT_PROTOCOL_VERSION,
                               "protocol_version", reading->protocol_version);
  osi_json_add_uint_if_present(root, reading->present_flags, METER_READING_PRESENT_TARIFF, "tariff",
                               reading->tariff);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_ENERGY_IMPORT,
                                 "energy_import_kwh", reading->energy_import_kwh);
  osi_json_add_number_if_present(root, reading->present_flags,
                                 METER_READING_PRESENT_ENERGY_IMPORT_T1, "energy_import_t1_kwh",
                                 reading->energy_import_t1_kwh);
  osi_json_add_number_if_present(root, reading->present_flags,
                                 METER_READING_PRESENT_ENERGY_IMPORT_T2, "energy_import_t2_kwh",
                                 reading->energy_import_t2_kwh);
  osi_json_add_number_if_present(root, reading->present_flags,
                                 METER_READING_PRESENT_ENERGY_IMPORT_T3, "energy_import_t3_kwh",
                                 reading->energy_import_t3_kwh);
  osi_json_add_number_if_present(root, reading->present_flags,
                                 METER_READING_PRESENT_ENERGY_IMPORT_T4, "energy_import_t4_kwh",
                                 reading->energy_import_t4_kwh);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_ENERGY_EXPORT,
                                 "energy_export_kwh", reading->energy_export_kwh);
  osi_json_add_number_if_present(root, reading->present_flags,
                                 METER_READING_PRESENT_ENERGY_EXPORT_T1, "energy_export_t1_kwh",
                                 reading->energy_export_t1_kwh);
  osi_json_add_number_if_present(root, reading->present_flags,
                                 METER_READING_PRESENT_ENERGY_EXPORT_T2, "energy_export_t2_kwh",
                                 reading->energy_export_t2_kwh);
  osi_json_add_number_if_present(root, reading->present_flags,
                                 METER_READING_PRESENT_ENERGY_EXPORT_T3, "energy_export_t3_kwh",
                                 reading->energy_export_t3_kwh);
  osi_json_add_number_if_present(root, reading->present_flags,
                                 METER_READING_PRESENT_ENERGY_EXPORT_T4, "energy_export_t4_kwh",
                                 reading->energy_export_t4_kwh);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_POWER,
                                 "power_w", reading->power_w);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_POWER_L1,
                                 "power_l1_w", reading->power_l1_w);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_POWER_L2,
                                 "power_l2_w", reading->power_l2_w);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_POWER_L3,
                                 "power_l3_w", reading->power_l3_w);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_VOLTAGE,
                                 "voltage_v", reading->voltage_v);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_VOLTAGE_L1,
                                 "voltage_l1_v", reading->voltage_l1_v);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_VOLTAGE_L2,
                                 "voltage_l2_v", reading->voltage_l2_v);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_VOLTAGE_L3,
                                 "voltage_l3_v", reading->voltage_l3_v);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_CURRENT,
                                 "current_a", reading->current_a);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_CURRENT_L1,
                                 "current_l1_a", reading->current_l1_a);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_CURRENT_L2,
                                 "current_l2_a", reading->current_l2_a);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_CURRENT_L3,
                                 "current_l3_a", reading->current_l3_a);
  osi_json_add_number_if_present(root, reading->present_flags, METER_READING_PRESENT_FREQUENCY,
                                 "frequency_hz", reading->frequency_hz);
  osi_json_add_uint_if_present(root, reading->present_flags, METER_READING_PRESENT_VOLTAGE_SAG,
                               "voltage_sag_l1_count", reading->voltage_sag_l1_count);
  osi_json_add_uint_if_present(root, reading->present_flags, METER_READING_PRESENT_VOLTAGE_SAG,
                               "voltage_sag_l2_count", reading->voltage_sag_l2_count);
  osi_json_add_uint_if_present(root, reading->present_flags, METER_READING_PRESENT_VOLTAGE_SAG,
                               "voltage_sag_l3_count", reading->voltage_sag_l3_count);
  osi_json_add_uint_if_present(root, reading->present_flags, METER_READING_PRESENT_VOLTAGE_SWELL,
                               "voltage_swell_l1_count", reading->voltage_swell_l1_count);
  osi_json_add_uint_if_present(root, reading->present_flags, METER_READING_PRESENT_VOLTAGE_SWELL,
                               "voltage_swell_l2_count", reading->voltage_swell_l2_count);
  osi_json_add_uint_if_present(root, reading->present_flags, METER_READING_PRESENT_VOLTAGE_SWELL,
                               "voltage_swell_l3_count", reading->voltage_swell_l3_count);
  osi_json_add_uint_if_present(root, reading->present_flags, METER_READING_PRESENT_POWER_FAIL,
                               "any_power_fail_count", reading->any_power_fail_count);
  osi_json_add_uint_if_present(root, reading->present_flags, METER_READING_PRESENT_POWER_FAIL,
                               "long_power_fail_count", reading->long_power_fail_count);
  osi_json_add_number_if_present(root, reading->present_flags,
                                 METER_READING_PRESENT_AVERAGE_POWER_15M, "average_power_15m_w",
                                 reading->average_power_15m_w);
  osi_json_add_number_if_present(root, reading->present_flags,
                                 METER_READING_PRESENT_MONTHLY_POWER_PEAK, "monthly_power_peak_w",
                                 reading->monthly_power_peak_w);
  osi_json_add_string_if_present(
      root, reading->present_flags, METER_READING_PRESENT_MONTHLY_POWER_PEAK,
      "monthly_power_peak_timestamp", reading->monthly_power_peak_timestamp);

  *body_out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  if (!*body_out) {
    return ERR_NO_MEMORY;
  }

  return SUCCESS;
}

static int osi_copy_config_value(const char* text, const char* key, char* value_out,
                                 size_t value_out_size) {
  if (!text || !key || !value_out || value_out_size == 0) {
    return ERR_INVALID_ARG;
  }

  value_out[0]        = '\0';
  size_t      key_len = strlen(key);
  const char* cursor  = text;

  while (*cursor != '\0') {
    const char* line_start = cursor;
    const char* line_end   = strchr(cursor, '\n');
    if (!line_end) {
      line_end = cursor + strlen(cursor);
      cursor   = line_end;
    } else {
      cursor = line_end + 1;
    }

    size_t line_len = (size_t)(line_end - line_start);
    if (line_len > 0 && line_start[line_len - 1] == '\r') {
      --line_len;
    }

    if (line_len <= key_len + 1) {
      continue;
    }

    if (strncmp(line_start, key, key_len) != 0 || line_start[key_len] != '=') {
      continue;
    }

    size_t value_len = line_len - key_len - 1;
    if (value_len >= value_out_size) {
      return ERR_INVALID_ARG;
    }

    memcpy(value_out, line_start + key_len + 1, value_len);
    value_out[value_len] = '\0';
    return SUCCESS;
  }

  return ERR_NOT_FOUND;
}

static int osi_get_facility_config_dir(char* dir_out, size_t dir_out_size) {
  if (!dir_out || dir_out_size == 0) {
    return ERR_INVALID_ARG;
  }

  const char* optimizer_config = read_file_to_string(OPTI_CONFIG_PATH);
  if (!optimizer_config) {
    snprintf(dir_out, dir_out_size, "%s", OPTI_FACILITY_CONF_DIR_FALLBACK);
    return SUCCESS;
  }

  int result = osi_copy_config_value(optimizer_config, "facility.conf.dir", dir_out, dir_out_size);
  free((void*)optimizer_config);

  if (result != SUCCESS || dir_out[0] == '\0') {
    snprintf(dir_out, dir_out_size, "%s", OPTI_FACILITY_CONF_DIR_FALLBACK);
  }

  return SUCCESS;
}

static int osi_get_calcs_dir(char* dir_out, size_t dir_out_size) {
  if (!dir_out || dir_out_size == 0) {
    return ERR_INVALID_ARG;
  }

  const char* optimizer_config = read_file_to_string(OPTI_CONFIG_PATH);
  if (!optimizer_config) {
    snprintf(dir_out, dir_out_size, "%s", CALCS_DEFAULT_DIRECTORY);
    return SUCCESS;
  }

  int result = osi_copy_config_value(optimizer_config, "data.calcs.dir", dir_out, dir_out_size);
  free((void*)optimizer_config);

  if (result != SUCCESS || dir_out[0] == '\0') {
    snprintf(dir_out, dir_out_size, "%s", CALCS_DEFAULT_DIRECTORY);
  }

  return SUCCESS;
}

static void osi_build_facility_filename(const char* facility_name, char* filename_out,
                                        size_t filename_out_size) {
  if (!facility_name || !filename_out || filename_out_size == 0) {
    return;
  }

  size_t write_index = 0;
  for (size_t read_index = 0;
       facility_name[read_index] != '\0' && write_index + 6 < filename_out_size; ++read_index) {
    unsigned char ch = (unsigned char)facility_name[read_index];
    if (ch == '/' || ch == '\\' || ch < 32 || ch == ':') {
      filename_out[write_index++] = '_';
    } else {
      filename_out[write_index++] = (char)ch;
    }
  }

  if (write_index == 0) {
    snprintf(filename_out, filename_out_size, "facility.conf");
    return;
  }

  snprintf(filename_out + write_index, filename_out_size - write_index, ".conf");
}

static int osi_find_facility_config_path(const char* facility_name, int create_if_missing,
                                         char* path_out, size_t path_out_size) {
  if (!facility_name || !path_out || path_out_size == 0) {
    return ERR_INVALID_ARG;
  }

  char facility_dir[256] = {0};
  int  result            = osi_get_facility_config_dir(facility_dir, sizeof(facility_dir));
  if (result != SUCCESS) {
    return result;
  }

  DIR* directory = opendir(facility_dir);
  if (directory) {
    struct dirent* entry = NULL;
    while ((entry = readdir(directory)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      size_t name_len = strlen(entry->d_name);
      if (name_len < 5 || strcmp(entry->d_name + name_len - 5, ".conf") != 0) {
        continue;
      }

      char filepath[512] = {0};
      snprintf(filepath, sizeof(filepath), "%s/%s", facility_dir, entry->d_name);

      const char* file_content = read_file_to_string(filepath);
      if (!file_content) {
        continue;
      }

      char existing_name[128] = {0};
      int  copy_result =
          osi_copy_config_value(file_content, "name", existing_name, sizeof(existing_name));
      free((void*)file_content);

      if (copy_result == SUCCESS && strcmp(existing_name, facility_name) == 0) {
        closedir(directory);
        snprintf(path_out, path_out_size, "%s", filepath);
        return SUCCESS;
      }
    }

    closedir(directory);
  }

  if (!create_if_missing) {
    return ERR_NOT_FOUND;
  }

  char filename[256] = {0};
  osi_build_facility_filename(facility_name, filename, sizeof(filename));
  snprintf(path_out, path_out_size, "%s/%s", facility_dir, filename);
  return SUCCESS;
}

static int osi_get_default_facility_name(char* name_out, size_t name_out_size) {
  if (!name_out || name_out_size == 0) {
    return ERR_INVALID_ARG;
  }

  name_out[0]            = '\0';
  char facility_dir[256] = {0};
  int  dir_result        = osi_get_facility_config_dir(facility_dir, sizeof(facility_dir));
  if (dir_result != SUCCESS) {
    return dir_result;
  }

  DIR* directory = opendir(facility_dir);
  if (!directory) {
    return ERR_NOT_FOUND;
  }

  struct dirent* entry = NULL;
  while ((entry = readdir(directory)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
        strcmp(entry->d_name, "example.conf") == 0) {
      continue;
    }

    size_t name_len = strlen(entry->d_name);
    if (name_len < 5 || strcmp(entry->d_name + name_len - 5, ".conf") != 0) {
      continue;
    }

    char filepath[512] = {0};
    snprintf(filepath, sizeof(filepath), "%s/%s", facility_dir, entry->d_name);

    const char* file_content = read_file_to_string(filepath);
    if (!file_content) {
      continue;
    }

    int copy_result = osi_copy_config_value(file_content, "name", name_out, name_out_size);
    free((void*)file_content);

    if (copy_result == SUCCESS && name_out[0] != '\0') {
      closedir(directory);
      return SUCCESS;
    }
  }

  closedir(directory);
  return ERR_NOT_FOUND;
}

static int osi_collect_facility_names(char** body_out) {
  if (!body_out) {
    return ERR_INVALID_ARG;
  }

  *body_out              = NULL;
  char facility_dir[256] = {0};
  int  dir_result        = osi_get_facility_config_dir(facility_dir, sizeof(facility_dir));
  if (dir_result != SUCCESS) {
    return dir_result;
  }

  DIR* directory = opendir(facility_dir);
  if (!directory) {
    return ERR_NOT_FOUND;
  }

  char*          body     = NULL;
  size_t         used     = 0;
  size_t         capacity = 0;
  struct dirent* entry    = NULL;

  while ((entry = readdir(directory)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
        strcmp(entry->d_name, "example.conf") == 0) {
      continue;
    }

    size_t name_len = strlen(entry->d_name);
    if (name_len < 5 || strcmp(entry->d_name + name_len - 5, ".conf") != 0) {
      continue;
    }

    char filepath[512] = {0};
    snprintf(filepath, sizeof(filepath), "%s/%s", facility_dir, entry->d_name);

    const char* file_content = read_file_to_string(filepath);
    if (!file_content) {
      continue;
    }

    char facility_name[128] = {0};
    int  copy_result =
        osi_copy_config_value(file_content, "name", facility_name, sizeof(facility_name));
    free((void*)file_content);

    if (copy_result != SUCCESS || facility_name[0] == '\0') {
      continue;
    }

    int append_result =
        osi_append_text(&body, &used, &capacity, facility_name, strlen(facility_name));
    if (append_result != SUCCESS) {
      free(body);
      closedir(directory);
      return append_result;
    }

    append_result = osi_append_text(&body, &used, &capacity, "\n", 1);
    if (append_result != SUCCESS) {
      free(body);
      closedir(directory);
      return append_result;
    }
  }

  closedir(directory);

  if (!body) {
    body = (char*)calloc(1, 1);
    if (!body) {
      return ERR_NO_MEMORY;
    }
  }

  *body_out = body;
  return SUCCESS;
}

static int osi_load_template_config(char** template_out) {
  if (!template_out) {
    return ERR_INVALID_ARG;
  }

  *template_out          = NULL;
  char facility_dir[256] = {0};
  int  dir_result        = osi_get_facility_config_dir(facility_dir, sizeof(facility_dir));
  if (dir_result != SUCCESS) {
    return dir_result;
  }

  char template_path[512] = {0};
  snprintf(template_path, sizeof(template_path), "%s/example.conf", facility_dir);

  const char* file_content = read_file_to_string(template_path);
  if (!file_content) {
    return ERR_NOT_FOUND;
  }

  *template_out = (char*)file_content;
  return SUCCESS;
}

typedef struct
{
  const char* key;
  char        value[128];
  int         has_value;
  int         was_written;
} Config_Update;

static Config_Update osi_config_updates[OPTI_CONFIG_EDITABLE_COUNT] = {
    {"name", "", 0, 0},          {"currency", "", 0, 0},      {"energy_zone", "", 0, 0},
    {"latitude", "", 0, 0},      {"longitude", "", 0, 0},     {"panel.tilt", "", 0, 0},
    {"panel.azimuth", "", 0, 0}, {"panel.m2_size", "", 0, 0},
};

static void osi_reset_config_updates(void) {
  for (int i = 0; i < OPTI_CONFIG_EDITABLE_COUNT; ++i) {
    osi_config_updates[i].value[0]    = '\0';
    osi_config_updates[i].has_value   = 0;
    osi_config_updates[i].was_written = 0;
  }
}

static Config_Update* osi_find_config_update(const char* key, size_t key_len) {
  for (int i = 0; i < OPTI_CONFIG_EDITABLE_COUNT; ++i) {
    if (strlen(osi_config_updates[i].key) == key_len &&
        strncmp(osi_config_updates[i].key, key, key_len) == 0) {
      return &osi_config_updates[i];
    }
  }

  return NULL;
}

static int osi_append_text(char** buffer, size_t* used, size_t* capacity, const char* text,
                           size_t text_len) {
  if (!buffer || !used || !capacity || !text) {
    return ERR_INVALID_ARG;
  }

  if (*buffer == NULL || *capacity < *used + text_len + 1) {
    size_t new_capacity = (*capacity == 0) ? 512 : *capacity;
    while (new_capacity < *used + text_len + 1) {
      new_capacity *= 2;
    }

    char* new_buffer = (char*)realloc(*buffer, new_capacity);
    if (!new_buffer) {
      return ERR_NO_MEMORY;
    }

    *buffer   = new_buffer;
    *capacity = new_capacity;
  }

  memcpy(*buffer + *used, text, text_len);
  *used += text_len;
  (*buffer)[*used] = '\0';

  return SUCCESS;
}

static int osi_parse_config_updates(const char* body, int body_len, char* filename) {
  if (!body || body_len <= 0) {
    return ERR_INVALID_ARG;
  }

  osi_reset_config_updates();

  int offset = 0;
  while (offset < body_len) {
    int line_start = offset;
    while (offset < body_len && body[offset] != '\n') {
      ++offset;
    }

    int line_end = offset;
    if (line_end > line_start && body[line_end - 1] == '\r') {
      --line_end;
    }

    if (offset < body_len && body[offset] == '\n') {
      ++offset;
    }

    if (line_end <= line_start) {
      continue;
    }

    const char* line      = body + line_start;
    const char* separator = memchr(line, '=', (size_t)(line_end - line_start));
    if (!separator) {
      continue;
    }

    size_t key_len   = (size_t)(separator - line);
    size_t value_len = (size_t)((body + line_end) - separator - 1);

    Config_Update* update = osi_find_config_update(line, key_len);
    if (!update) {
      return ERR_INVALID_ARG;
    }

    if (value_len >= sizeof(update->value)) {
      return ERR_INVALID_ARG;
    }

    memcpy(update->value, separator + 1, value_len);
    update->value[value_len] = '\0';
    update->has_value        = 1;

    if (strcmp(update->key, "name") == 0) {
      const char* prefix  = "/etc/maestro/facility/";
      const char* postfix = ".conf";
      size_t      len     = value_len + strlen(prefix) + strlen(postfix) + 1;
      filename            = malloc(len);
      if (filename) {
        snprintf(filename, len, "%s%s%s", prefix, update->value, postfix);
        filename[len] = '\0';
      }
    }
  }

  return SUCCESS;
}

static int osi_merge_config_updates(const char* existing_config, char** merged_config_out) {
  if (!existing_config || !merged_config_out) {
    return ERR_INVALID_ARG;
  }

  char*       merged          = NULL;
  size_t      merged_used     = 0;
  size_t      merged_capacity = 0;
  const char* cursor          = existing_config;

  while (*cursor != '\0') {
    const char* line_start  = cursor;
    const char* line_end    = strchr(cursor, '\n');
    size_t      line_len    = 0;
    int         had_newline = 0;

    if (line_end) {
      line_len    = (size_t)(line_end - line_start);
      cursor      = line_end + 1;
      had_newline = 1;
    } else {
      line_len = strlen(line_start);
      cursor   = line_start + line_len;
    }

    size_t logical_len = line_len;
    if (logical_len > 0 && line_start[logical_len - 1] == '\r') {
      --logical_len;
    }

    const char*    separator = memchr(line_start, '=', logical_len);
    Config_Update* update    = NULL;
    if (separator) {
      update = osi_find_config_update(line_start, (size_t)(separator - line_start));
    }

    int res;
    if (update && update->has_value) {
      res = osi_append_text(&merged, &merged_used, &merged_capacity, update->key,
                            strlen(update->key));
      if (res != SUCCESS) {
        free(merged);
        return res;
      }
      res = osi_append_text(&merged, &merged_used, &merged_capacity, "=", 1);
      if (res != SUCCESS) {
        free(merged);
        return res;
      }
      res = osi_append_text(&merged, &merged_used, &merged_capacity, update->value,
                            strlen(update->value));
      if (res != SUCCESS) {
        free(merged);
        return res;
      }
      res = osi_append_text(&merged, &merged_used, &merged_capacity, "\n", 1);
      if (res != SUCCESS) {
        free(merged);
        return res;
      }
      update->was_written = 1;
    } else {
      res = osi_append_text(&merged, &merged_used, &merged_capacity, line_start, line_len);
      if (res != SUCCESS) {
        free(merged);
        return res;
      }
      if (had_newline) {
        res = osi_append_text(&merged, &merged_used, &merged_capacity, "\n", 1);
        if (res != SUCCESS) {
          free(merged);
          return res;
        }
      }
    }
  }

  for (int i = 0; i < OPTI_CONFIG_EDITABLE_COUNT; ++i) {
    if (!osi_config_updates[i].has_value || osi_config_updates[i].was_written) {
      continue;
    }

    int res = osi_append_text(&merged, &merged_used, &merged_capacity, osi_config_updates[i].key,
                              strlen(osi_config_updates[i].key));
    if (res != SUCCESS) {
      free(merged);
      return res;
    }
    res = osi_append_text(&merged, &merged_used, &merged_capacity, "=", 1);
    if (res != SUCCESS) {
      free(merged);
      return res;
    }
    res = osi_append_text(&merged, &merged_used, &merged_capacity, osi_config_updates[i].value,
                          strlen(osi_config_updates[i].value));
    if (res != SUCCESS) {
      free(merged);
      return res;
    }
    res = osi_append_text(&merged, &merged_used, &merged_capacity, "\n", 1);
    if (res != SUCCESS) {
      free(merged);
      return res;
    }
  }

  *merged_config_out = merged;
  return SUCCESS;
}
/*******************************ENDPOINT FUNCTIONS************************/
int osi_get_solar_data(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  // Swap body with request for actual data here
  const char* body = "{\"Solar\":\"No sun until April\"}";

  return osi_set_response(_ctx->conn, 200, "application/json", body);
}
int osi_get_temp_1_data(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  // Swap body with request for actual data here
  const char* body = "{\"Temp-Sensor\":\"It's a me, Mario\"}";
  return osi_set_response(_ctx->conn, 200, "application/json", body);
}
int osi_get_jacuzzi_data(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  // Swap body with request for actual data here
  const char* body = "{\"Jacuzzi\":\"Out of water\"}";

  return osi_set_response(_ctx->conn, 200, "application/json", body);
}

int osi_get_overview(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  // Swap body with request for actual data here
  const char* body = "{\"overview\":Yahoooooooo}";
  return osi_set_response(_ctx->conn, 200, "application/json", body);
}

int osi_post_ingest(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  int         body_len = 0;
  const char* body     = osi_get_request_body(_ctx, &body_len);
  if (!body || body_len <= 0) {
    return osi_set_response(_ctx->conn, 400, "application/json",
                            "{\"error\":\"meter reading body missing\"}");
  }

  Meter_Reading reading;
  int           parse_result = osi_parse_meter_reading_json(body, body_len, &reading);
  if (parse_result != SUCCESS) {
    return osi_set_response(_ctx->conn, 400, "application/json",
                            "{\"error\":\"invalid meter reading\"}");
  }

  osi_latest_meter_reading             = reading;
  osi_latest_meter_reading_received_at = time(NULL);
  osi_has_latest_meter_reading         = 1;

  Opti_Server* server = (Opti_Server*)_ctx->ctx;
  if (!server) {
    return ERR_INVALID_ARG;
  }

  int store_result = meter_store_insert_reading(&server->meter_store, &reading);
  if (store_result != SUCCESS) {
    return store_result;
  }

  return osi_set_response(_ctx->conn, 200, "application/json", "{\"status\":\"ok\"}");
}

int osi_get_weather_cache(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  Opti_Server* server = (Opti_Server*)_ctx->ctx;
  if (!server) {
    return ERR_INVALID_ARG;
  }

  time_t start        = 0;
  time_t end          = 0;
  int    range_result = osi_parse_time_range(_ctx->conn->request, &start, &end);
  if (range_result != SUCCESS) {
    return osi_set_response(_ctx->conn, 400, "application/json",
                            "{\"error\":\"invalid time range\"}");
  }

  HTTP_Request*     req            = _ctx->conn->request;
  Facility_Config** configs        = NULL;
  Facility_Config*  facility       = NULL;
  size_t            facility_count = 0;
  float             latitude       = 0.0f;
  float             longitude      = 0.0f;
  int               panel_tilt     = 0;
  unsigned int      panel_azimuth  = 0;
  const char* latitude_keys[]  = {"latitude", "lat", NULL};
  const char* longitude_keys[] = {"longitude", "lon", NULL};
  const char* tilt_keys[]      = {"panel_tilt", "panel.tilt", NULL};
  const char* azimuth_keys[]   = {"panel_azimuth", "panel.azimuth", NULL};
  int facility_result = osi_load_request_facility(req, &configs, &facility_count, &facility);
  if (facility_result == SUCCESS && facility) {
    latitude      = facility->lat;
    longitude     = facility->lon;
    panel_tilt    = facility->panel ? facility->panel->tilt : 0;
    panel_azimuth = facility->panel ? (unsigned int)facility->panel->azimuth : 0;
  } else {
    const char* latitude_param   = osi_get_query_param_any(req, latitude_keys);
    const char* longitude_param  = osi_get_query_param_any(req, longitude_keys);
    double      latitude_value   = 0.0;
    double      longitude_value  = 0.0;
    int         parsed_tilt      = 0;
    int         parsed_azimuth   = 0;

    if (!latitude_param || !longitude_param) {
      return osi_set_response(_ctx->conn, 404, "application/json",
                              "{\"error\":\"facility not found\"}");
    }
    if (!osi_parse_query_double(req, latitude_keys, &latitude_value) ||
        !osi_parse_query_double(req, longitude_keys, &longitude_value)) {
      return osi_set_response(_ctx->conn, 400, "application/json",
                              "{\"error\":\"invalid latitude/longitude\"}");
    }
    if (osi_get_query_param_any(req, tilt_keys) != NULL &&
        !osi_parse_query_int(req, tilt_keys, &parsed_tilt)) {
      return osi_set_response(_ctx->conn, 400, "application/json",
                              "{\"error\":\"invalid panel_tilt\"}");
    }
    if (osi_get_query_param_any(req, azimuth_keys) != NULL &&
        !osi_parse_query_int(req, azimuth_keys, &parsed_azimuth)) {
      return osi_set_response(_ctx->conn, 400, "application/json",
                              "{\"error\":\"invalid panel_azimuth\"}");
    }

    latitude      = (float)latitude_value;
    longitude     = (float)longitude_value;
    panel_tilt    = parsed_tilt;
    panel_azimuth = (unsigned int)(parsed_azimuth < 0 ? 0 : parsed_azimuth);
  }

  double latitude_value  = 0.0;
  double longitude_value = 0.0;
  int    parsed_tilt     = 0;
  int    parsed_azimuth  = 0;
  if (osi_get_query_param_any(req, latitude_keys) != NULL &&
      osi_parse_query_double(req, latitude_keys, &latitude_value)) {
    latitude = (float)latitude_value;
  }
  if (osi_get_query_param_any(req, longitude_keys) != NULL &&
      osi_parse_query_double(req, longitude_keys, &longitude_value)) {
    longitude = (float)longitude_value;
  }
  if (osi_get_query_param_any(req, tilt_keys) != NULL &&
      osi_parse_query_int(req, tilt_keys, &parsed_tilt)) {
    panel_tilt = parsed_tilt;
  }
  if (osi_get_query_param_any(req, azimuth_keys) != NULL &&
      osi_parse_query_int(req, azimuth_keys, &parsed_azimuth)) {
    panel_azimuth = (unsigned int)(parsed_azimuth < 0 ? 0 : parsed_azimuth);
  }

  const char* forecast_param = osi_get_query_param(req, "forecast");
  bool        forecast       = forecast_param && strcmp(forecast_param, "0") != 0;
  if (forecast) {
    osi_default_forecast_range(req, &start, &end);
  }

  Weather weather = {0};
  int read_result = sql_helper_read_weather(&server->optimizer_cache, &weather, latitude, longitude,
                                            panel_tilt, panel_azimuth, forecast, start, end);

  if (read_result == SUCCESS && forecast && weather.count == 0) {
    osi_weather_dispose(&weather);

    time_t fallback_start = time(NULL) - 86400;
    time_t fallback_end   = time(NULL) + (7 * 86400);
    read_result = sql_helper_read_weather(&server->optimizer_cache, &weather, latitude, longitude,
                                          panel_tilt, panel_azimuth, true, fallback_start,
                                          fallback_end);
  }

  if (read_result == SUCCESS && forecast && weather.count == 0) {
    osi_weather_dispose(&weather);

    time_t current_start = time(NULL) - 86400;
    time_t current_end   = time(NULL) + 3600;
    read_result = sql_helper_read_weather(&server->optimizer_cache, &weather, latitude, longitude,
                                          panel_tilt, panel_azimuth, false, current_start,
                                          current_end);
  }

  if (configs) {
    facility_dispose(configs, facility_count);
  }

  if (read_result != SUCCESS) {
    osi_weather_dispose(&weather);
    return read_result;
  }

  char* body = NULL;

  int txt_result = osi_weather_to_txt(&weather, forecast ? 1 : 0, &body);
  // int   json_result = osi_weather_to_json(&weather, forecast ? 1 : 0, &body);
  osi_weather_dispose(&weather);
  if (txt_result != SUCCESS) {
    free(body);
    return txt_result;
  }

  int res = osi_set_response(_ctx->conn, 200, "text/plain", body);
  free(body);
  return res;
}

int osi_get_spot_cache(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  Opti_Server* server = (Opti_Server*)_ctx->ctx;
  if (!server) {
    return ERR_INVALID_ARG;
  }

  time_t start        = 0;
  time_t end          = 0;
  int    range_result = osi_parse_time_range(_ctx->conn->request, &start, &end);
  if (range_result != SUCCESS) {
    return osi_set_response(_ctx->conn, 400, "application/json",
                            "{\"error\":\"invalid time range\"}");
  }

  HTTP_Request* req         = _ctx->conn->request;
  const char*   zone_keys[] = {"energy_zone", "price_class", NULL};
  const char*   zone_param  = osi_get_query_param_any(req, zone_keys);
  int           zone        = 3;
  bool          has_zone    = false;

  if (zone_param) {
    if (!osi_parse_query_int(req, zone_keys, &zone)) {
      return osi_set_response(_ctx->conn, 400, "application/json",
                              "{\"error\":\"invalid energy_zone\"}");
    }
    has_zone = true;
  } else {
    Facility_Config** configs        = NULL;
    Facility_Config*  facility       = NULL;
    size_t            facility_count = 0;
    int facility_result = osi_load_request_facility(req, &configs, &facility_count, &facility);
    if (facility_result == SUCCESS && facility) {
      zone     = ((int)facility->price_class) + 1;
      has_zone = true;
    } else if (osi_get_query_param(req, "name") != NULL) {
      return osi_set_response(_ctx->conn, 404, "application/json",
                              "{\"error\":\"facility not found\"}");
    }

    if (configs) {
      facility_dispose(configs, facility_count);
    }
  }

  if (!has_zone) {
    zone = 3;
  }

  SpotPriceClass price_class = SE3;
  if (zone >= 1 && zone <= 4) {
    price_class = (SpotPriceClass)(zone - 1);
  } else if (zone >= 0 && zone <= 3) {
    price_class = (SpotPriceClass)zone;
  }

  Electricity_Spots spots = {0};
  int               read_result =
      sql_helper_read_spots(&server->optimizer_cache, &spots, price_class, SPOT_SEK, start, end);
  if (read_result != SUCCESS) {
    osi_spots_dispose(&spots);
    return read_result;
  }

  char* body        = NULL;
  int   json_result = osi_spots_to_json(&spots, &body);
  osi_spots_dispose(&spots);
  if (json_result != SUCCESS) {
    free(body);
    return json_result;
  }

  int res = osi_set_response(_ctx->conn, 200, "application/json", body);
  free(body);
  return res;
}

int osi_get_power_current(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  if (!osi_has_latest_meter_reading) {
    return osi_set_response(_ctx->conn, 404, "application/json",
                            "{\"error\":\"no meter reading available\"}");
  }

  char* body = NULL;
  int   res  = osi_meter_reading_to_json(&osi_latest_meter_reading,
                                         osi_latest_meter_reading_received_at, &body);
  if (res != SUCCESS) {
    free(body);
    return res;
  }

  res = osi_set_response(_ctx->conn, 200, "application/json", body);
  free(body);
  return res;
}

int osi_get_display_current(Osi_RequestCtx* _ctx) { return osi_get_power_current(_ctx); }

int osi_get_display_graph_hour(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  HTTP_Request* req                    = _ctx->conn->request;
  const char*   requested_name         = osi_get_query_param(req, "name");
  const char*   range_param            = osi_get_query_param(req, "range");
  char          default_name[128]      = {0};
  char          facility_name_buf[128] = {0};
  const char*   facility_name          = NULL;

  if (!requested_name || requested_name[0] == '\0') {
    int default_result = osi_get_default_facility_name(default_name, sizeof(default_name));
    if (default_result != SUCCESS) {
      return osi_set_response(_ctx->conn, 404, "application/json",
                              "{\"error\":\"no facility available\"}");
    }
    facility_name = default_name;
  } else {
    Facility_Config** configs        = NULL;
    Facility_Config*  facility       = NULL;
    size_t            facility_count = 0;
    int facility_result = osi_load_request_facility(req, &configs, &facility_count, &facility);
    if (facility_result != SUCCESS || !facility || !facility->name || facility->name[0] == '\0') {
      if (configs) {
        facility_dispose(configs, facility_count);
      }
      return osi_set_response(_ctx->conn, 404, "application/json",
                              "{\"error\":\"facility not found\"}");
    }

    snprintf(facility_name_buf, sizeof(facility_name_buf), "%s", facility->name);
    facility_dispose(configs, facility_count);
    facility_name = facility_name_buf;
  }

  char calcs_dir[256] = {0};
  int  dir_result     = osi_get_calcs_dir(calcs_dir, sizeof(calcs_dir));
  if (dir_result != SUCCESS) {
    return osi_set_response(_ctx->conn, 500, "application/json",
                            "{\"error\":\"failed to resolve calcs dir\"}");
  }

  time_t    now = time(NULL);
  struct tm tm  = *localtime(&now);

  char date[11];
  strftime(date, sizeof(date), "%Y-%m-%d", &tm);

  const char* suffix = "";
  if (range_param && strcmp(range_param, "7d") == 0) {
    suffix = "-7d";
  } else if (range_param && strcmp(range_param, "30d") == 0) {
    suffix = "-30d";
  }

  char filename[512];
  int  len = snprintf(filename, sizeof(filename), "%s/%s-Consumption_%s-display%s.json", calcs_dir,
                      facility_name, date, suffix);
  if (len < 0 || (size_t)len >= sizeof(filename)) {
    return osi_set_response(_ctx->conn, 500, "application/json",
                            "{\"error\":\"failed to format display graph filename\"}");
  }

  const char* file_content = read_file_to_string(filename);

  if (!file_content) {
    char response[256];
    len = snprintf(response, sizeof(response), "{\"error\":\"display graph not available (%s)\"}",
                   filename);
    if (len < 0 || (size_t)len >= sizeof(response)) {
      snprintf(response, sizeof(response), "{\"error\":\"display graph not available\"}");
    }
    return osi_set_response(_ctx->conn, 503, "application/json", response);
  }

  int res = osi_set_response(_ctx->conn, 200, "application/json", file_content);
  free((void*)file_content);
  return res;
}

int osi_get_average_daily(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  HTTP_Request* Req = _ctx->conn->request;

  const char* facility_name = NULL;
  const char* type          = NULL;
  int         epd           = 96;

  /* Look for filename vars in request paremeters */
  if (Req->params != NULL) {
    linked_list_foreach(Req->params, node) {
      HTTP_Key_Value* Param = (HTTP_Key_Value*)node->item;
      if (strcmp(Param->key, "name") == 0) {
        facility_name = Param->value;
      }
      if (strcmp(Param->key, "epd") == 0 || strcmp(Param->key, "sp") == 0) {
        epd = atoi(Param->value);
      }
      if (strcmp(Param->key, "type") == 0) {
        type = Param->value;
      }
    }
  }

  if (!facility_name) { // Need name
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"Missing parameter for \'name\'\"}");
  }
  /* Set defaults if no param */
  if (!type) {
    type = "json";
  }

  char* filename =
      calc_name_get_daily(CALCS_DEFAULT_DIRECTORY, facility_name, type, epd, time(NULL));
  if (!filename) {
    return osi_set_response(_ctx->conn, 500, "application/json",
                            "{\"error\":\"Failed to format filename\"}");
  }

  const char* file_content = read_file_to_string(filename);
  if (!file_content) {
    char response[256];
    int len = snprintf(response, sizeof(response), "{\"error\":\"File not found (%s)\"}", filename);
    response[len] = '\0';
    free((void*)file_content);
    free(filename);
    return osi_set_response(_ctx->conn, 503, "application/json", response);
  }
  free(filename);


  int res = osi_set_response(_ctx->conn, 200, "application/json", file_content);

  free((void*)file_content);

  return res;
}

int osi_get_average_hourly(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  time_t    t  = time(NULL);
  struct tm tm = *localtime(&t);

  char today[11]; // YYYY-MM-DD
  strftime(today, sizeof(today), "%Y%m%d", &tm);

  char full_filename[256];

  int res = snprintf(full_filename, sizeof(full_filename), "%s%s-SP24-SE3.json", OPTI_AVERAGE_PATH,
                     today);

  if (res < 0 || (size_t)res >= sizeof(full_filename)) {
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"average.json not available\"}");
  }

  const char* file_content = read_file_to_string((const char*)full_filename);
  if (!file_content) {
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"average.json not available\"}");
  }

  res = osi_set_response(_ctx->conn, 200, "application/json", file_content);

  free((void*)file_content);

  return res;
}

int osi_get_config(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  HTTP_Request* req           = _ctx->conn->request;
  const char*   facility_name = osi_get_query_param(req, "name");
  if (!facility_name || facility_name[0] == '\0') {
    return osi_set_response(_ctx->conn, 400, "application/json",
                            "{\"error\":\"Missing parameter for 'name'\"}");
  }

  char facility_path[512] = {0};
  int  path_result =
      osi_find_facility_config_path(facility_name, 0, facility_path, sizeof(facility_path));
  if (path_result != SUCCESS) {
    return osi_set_response(_ctx->conn, 404, "application/json",
                            "{\"error\":\"facility config not found\"}");
  }

  const char* file_content = read_file_to_string(facility_path);
  if (!file_content) {
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"facility config not available\"}");
  }

  int res = osi_set_response(_ctx->conn, 200, "text/plain", file_content);

  free((void*)file_content);

  return res;
}

int osi_get_facilities(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  char* body   = NULL;
  int   result = osi_collect_facility_names(&body);
  if (result != SUCCESS) {
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"failed to list facilities\"}");
  }

  int res = osi_set_response(_ctx->conn, 200, "text/plain", body);
  free(body);
  return res;
}

int osi_post_config(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  HTTP_Request* req           = _ctx->conn->request;
  const char*   facility_name = osi_get_query_param(req, "name");
  if (!facility_name || facility_name[0] == '\0') {
    return osi_set_response(_ctx->conn, 400, "application/json",
                            "{\"error\":\"Missing parameter for 'name'\"}");
  }

  int         body_len = 0;
  const char* body     = osi_get_request_body(_ctx, &body_len);
  if (!body || body_len <= 0) {
    return osi_set_response(_ctx->conn, 400, "application/json",
                            "{\"error\":\"config body missing\"}");
  }

  char* file         = NULL;
  int   parse_result = osi_parse_config_updates(body, body_len, file);
  if (parse_result == ERR_INVALID_ARG) {
    return osi_set_response(_ctx->conn, 400, "application/json",
                            "{\"error\":\"invalid config key in update\"}");
  }
  if (parse_result != SUCCESS) {
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"failed to parse config update\"}");
  }

  char facility_path[512] = {0};
  int  path_result =
      osi_find_facility_config_path(facility_name, 1, facility_path, sizeof(facility_path));
  if (path_result != SUCCESS) {
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"failed to resolve facility config path\"}");
  }

  const char* existing_config   = read_file_to_string(facility_path);
  char*       template_config   = NULL;
  int         used_blank_config = 0;
  if (!existing_config) {
    if (osi_load_template_config(&template_config) == SUCCESS && template_config) {
      existing_config = template_config;
    } else {
      existing_config   = osi_blank_facility_config;
      used_blank_config = 1;
    }
  }
  char* merged_config = NULL;
  int   merge_result  = osi_merge_config_updates(existing_config, &merged_config);
  if (template_config) {
    free(template_config);
  } else if (existing_config != NULL && !used_blank_config) {
    free((void*)existing_config);
  }
  if (merge_result != SUCCESS || !merged_config) {
    free(merged_config);
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"failed to merge facility config update\"}");
  }

  FILE* config_file = fopen(facility_path, "w");
  if (!config_file) {
    free(merged_config);
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"failed to open facility config\"}");
  }

  size_t merged_len   = strlen(merged_config);
  size_t written      = fwrite(merged_config, 1, merged_len, config_file);
  int    close_result = fclose(config_file);
  free(merged_config);

  if (written != merged_len || close_result != 0) {
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"failed to write facility config\"}");
  }

  int signal_result = system("pkill -USR2 optimizer");
  if (signal_result != 0) {
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"facility config saved but reload signal failed\"}");
  }
  uds_client_send(RELOAD);
  uds_client_send(RUN);

  signal_result = system("pkill -USR1 optimizer");
  if (signal_result != 0) {
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"facility config saved but calculation trigger failed\"}");
  }

  return osi_set_response(_ctx->conn, 200, "application/json",
                          "{\"status\":\"facility config saved\"}");
}

int osi_recalc(Osi_RequestCtx* _ctx) {
  if (!_ctx) {
    return ERR_INVALID_ARG;
  }
  int         res;
  const char* body = NULL;

  res = uds_client_send(RELOAD);
  if (res != SUCCESS) {
    const char* body = "Failed to reload config";
    return osi_set_response(_ctx->conn, 500, "application/json", body);
  }

  res = uds_client_send(RUN);

  if (res != SUCCESS) {
    const char* body = "Failed to run calculations";
    return osi_set_response(_ctx->conn, 500, "application/json", body);
  }

  body = "Reloaded config and ran calculations";
  return osi_set_response(_ctx->conn, 200, "application/json", body);
}

int osi_kill(Osi_RequestCtx* _ctx) {

  if (!_ctx) {
    return ERR_INVALID_ARG;
  }
  int         res;
  const char* body = NULL;

  res = uds_client_send(KILL);
  if (res != SUCCESS) {
    const char* body = "Failed to reload config";
    return osi_set_response(_ctx->conn, 500, "application/json", body);
  }

  body = "Committed first degree murder on optimizer";
  return osi_set_response(_ctx->conn, 200, "application/json", body);
}

/*************************************************************************/
/************************************************************************^**************************************/
int osi_init(void* _context, Opti_Server_Instance* _Instance, HTTP_Server_Connection* _Connection) {
  if (!_Instance || !_Connection) {
    return ERR_INVALID_ARG;
  }

  memset(_Instance, 0, sizeof(Opti_Server_Instance));

  _Instance->context         = _context;
  _Instance->task            = NULL;
  _Instance->http_connection = _Connection;
  http_server_connection_set_callback(_Instance->http_connection, _Instance, osi_on_request,
                                      osi_on_dispose);

  return SUCCESS;
}

int osi_init_ptr(void* _context, HTTP_Server_Connection* _Connection,
                 Opti_Server_Instance** _Instance_Ptr) {

  if (_Instance_Ptr == NULL)
    return ERR_INVALID_ARG;

  Opti_Server_Instance* _Instance = (Opti_Server_Instance*)malloc(sizeof(Opti_Server_Instance));
  if (_Instance == NULL)
    return ERR_NO_MEMORY;

  int result = osi_init(_context, _Instance, _Connection);
  if (result != SUCCESS) {
    free(_Instance);
    return result;
  }

  *(_Instance_Ptr) = _Instance;

  return SUCCESS;
}

int osi_on_request(void* _context) {
  if (!_context) {
    return ERR_INVALID_ARG;
  }

  Opti_Server_Instance* _Instance = (Opti_Server_Instance*)_context;
  _Instance->state                = OPTI_INSTANCE_INITIALIZING;
  _Instance->task                 = scheduler_create_task(_Instance, osi_taskwork);

  return SUCCESS;
}

int osi_on_dispose(void* _context) {
  if (!_context) {
    return ERR_INVALID_ARG;
  }

  Opti_Server_Instance* _Instance = (Opti_Server_Instance*)_context;
  if (_Instance->task) {
    scheduler_destroy_task(_Instance->task);
    _Instance->task = NULL;
  }

  if (_Instance->http_connection) {
    HTTP_Server_Connection* conn = _Instance->http_connection;
    _Instance->http_connection   = NULL;
    http_server_connection_dispose_ptr(&conn);
  }

  if (_Instance->on_finish) {
    _Instance->on_finish(_Instance->context, _Instance);
  }

  return SUCCESS;
}

int osi_on_api_finish(void* _context) {
  if (!_context) {
    return ERR_INVALID_ARG;
  }

  Opti_Server_Instance* Instance = (Opti_Server_Instance*)_context;
  Instance->state = OPTI_INSTANCE_RESPONSE_BUILDING;

  return SUCCESS;
}


/* --------------TASKWORK STATE FUNCTIONS-------------- */

OptiServerInstanceState worktask_request_parse(Opti_Server_Instance* _Instance) {
  if (!_Instance || !_Instance->http_connection->request) {
    return OPTI_INSTANCE_ERROR;
  }

  HTTP_Request* req   = _Instance->http_connection->request;
  _Instance->endpoint = NULL;

  for (int i = 0; i < ENDPOINTS_COUNT; i++) {
    char endpoint_path[128];
    strcpy(endpoint_path, API_ENDPOINT_ROOT);
    strcat(endpoint_path, Endpoints[i].path);

    if (req->method == Endpoints[i].method && strcmp(req->path, endpoint_path) == 0) {
      _Instance->endpoint = &Endpoints[i];
      return OPTI_INSTANCE_RESPONSE_BUILDING;
    }
  }

  // Nothing found, response will build 404
  return OPTI_INSTANCE_RESPONSE_BUILDING;
}

OptiServerInstanceState worktask_response_build(Opti_Server_Instance* _Instance) {
  if (!_Instance || !_Instance->http_connection) {
    return OPTI_INSTANCE_ERROR;
  }

  Osi_RequestCtx ctx = {
      .ctx      = _Instance->context,
      .instance = _Instance,
      .conn     = _Instance->http_connection,
  };

  int res = 0;

  if (_Instance->endpoint == NULL) {
    res = osi_set_response(ctx.conn, 404, "application/json", "{\"error\":\"not found\"}");
  } else {
    res = _Instance->endpoint->endpoint_func(&ctx);
  }

  if (res != SUCCESS) {
    osi_set_response(ctx.conn, 500, "text/plain", "Internal Server Error");
  }

  return OPTI_INSTANCE_RESPONSE_SENDING;
}

void osi_taskwork(void* _context, uint64_t _montime) {

  if (!_context) {
    return;
  }
  (void)_montime; // why?

  Opti_Server_Instance* Instance = (Opti_Server_Instance*)_context;

  switch (Instance->state) {
  case OPTI_INSTANCE_INITIALIZING: {
    Instance->state = OPTI_INSTANCE_REQUEST_PARSING;
  } break;

  case OPTI_INSTANCE_REQUEST_PARSING: {
    Instance->state = worktask_request_parse(Instance);
  } break;

  case OPTI_INSTANCE_IDLING:
    break;

  case OPTI_INSTANCE_RESPONSE_BUILDING: {
    Instance->state = worktask_response_build(Instance);
  } break;

  case OPTI_INSTANCE_RESPONSE_SENDING: {
    Instance->state = OPTI_INSTANCE_DISPOSING;
  } break;

  case OPTI_INSTANCE_DISPOSING: {
    // on_dipose function will dispose
  } break;

  case OPTI_INSTANCE_ERROR: {
    osi_set_response(Instance->http_connection, 500, "text/plain", "Internal Server Error");
    Instance->state = OPTI_INSTANCE_DISPOSING;
  } break;
  }
}

void osi_dispose(Opti_Server_Instance* _Instance) {
  if (!_Instance) {
    return;
  }

  if (_Instance->task) {
    scheduler_destroy_task(_Instance->task);
    _Instance->task = NULL;
  }

  /*
    IMPORTANT:
    HTTP_Server_Connection owns itself and will be disposed from osi_on_dispose().
  */

  _Instance->http_connection = NULL;
  _Instance->endpoint        = NULL;
  _Instance->state           = OPTI_INSTANCE_DISPOSING;
}

void osi_dispose_ptr(Opti_Server_Instance** _Instance_Ptr) {
  if (!_Instance_Ptr || !*(_Instance_Ptr)) {
    return;
  }

  osi_dispose(*(_Instance_Ptr));
  free(*(_Instance_Ptr));
  *_Instance_Ptr = NULL;
}
