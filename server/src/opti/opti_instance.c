#include "opti/opti_instance.h"
#include "data/calc_name.h"
#include "maestromodules/http_parser.h"
#include <maestroutils/file_utils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "unix_domain_socket.h"

#define OPTI_AVERAGE_PATH "/var/lib/maestro/calcs/"
#define OPTI_CONFIG_PATH "/etc/maestro/optimizer.conf"
#define OPTI_CONFIG_EDITABLE_COUNT 7

//-----------------Internal Functions-----------------
//
int osi_on_http_connection(void* _context, HTTP_Server_Connection* _Connection);
int osi_on_request(void* _context);
int osi_on_dispose(void* _context);

void                    osi_taskwork(void* _context, uint64_t _montime);
OptiServerInstanceState worktask_request_parse(Opti_Server_Instance* _Instance);
OptiServerInstanceState worktask_response_build(Opti_Server_Instance* _Instance);
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
/* REMEMBER TO CHANGE COUNT WHEN ADDING ENDPOINT! */
#define ENDPOINTS_COUNT 10

const Device_API_Endpoint Endpoints[ENDPOINTS_COUNT] = {{
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
                                                        }


};

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

typedef struct
{
  const char* key;
  char        value[128];
  int         has_value;
  int         was_written;
} Config_Update;

static Config_Update osi_config_updates[OPTI_CONFIG_EDITABLE_COUNT] = {
    {"data.spots.currency", "", 0, 0},    {"data.spots.price_class", "", 0, 0},
    {"facility.latitude", "", 0, 0},      {"facility.longitude", "", 0, 0},
    {"facility.panel.tilt", "", 0, 0},    {"facility.panel.azimuth", "", 0, 0},
    {"facility.panel.m2_size", "", 0, 0},
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

static int osi_parse_config_updates(const char* body, int body_len) {
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
    printf("Failed to format filename\n");
    return osi_set_response(_ctx->conn, 500, "application/json",
                            "{\"error\":\"Failed to format filename\"}");
  }

  printf("Fetching data from: %s\n", filename);

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
    printf("Failed to format filename\n");
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"average.json not available\"}");
  }

  printf("Fetching data from: %s\n", full_filename);

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

  const char* file_content = read_file_to_string(OPTI_CONFIG_PATH);
  if (!file_content) {
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"optimizer.conf not available\"}");
  }

  int res = osi_set_response(_ctx->conn, 200, "text/plain", file_content);

  free((void*)file_content);

  return res;
}

int osi_post_config(Osi_RequestCtx* _ctx) {
  if (!_ctx || !_ctx->conn || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  int         body_len = 0;
  const char* body     = osi_get_request_body(_ctx, &body_len);
  if (!body || body_len <= 0) {
    return osi_set_response(_ctx->conn, 400, "application/json",
                            "{\"error\":\"config body missing\"}");
  }

  int parse_result = osi_parse_config_updates(body, body_len);
  if (parse_result == ERR_INVALID_ARG) {
    return osi_set_response(_ctx->conn, 400, "application/json",
                            "{\"error\":\"invalid config key in update\"}");
  }
  if (parse_result != SUCCESS) {
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"failed to parse config update\"}");
  }

  const char* existing_config = read_file_to_string(OPTI_CONFIG_PATH);
  if (!existing_config) {
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"optimizer.conf not available\"}");
  }

  char* merged_config = NULL;
  int   merge_result  = osi_merge_config_updates(existing_config, &merged_config);
  free((void*)existing_config);
  if (merge_result != SUCCESS || !merged_config) {
    free(merged_config);
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"failed to merge config update\"}");
  }

  FILE* config_file = fopen(OPTI_CONFIG_PATH, "w");
  if (!config_file) {
    free(merged_config);
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"failed to open optimizer.conf\"}");
  }

  size_t merged_len   = strlen(merged_config);
  size_t written      = fwrite(merged_config, 1, merged_len, config_file);
  int    close_result = fclose(config_file);
  free(merged_config);

  if (written != merged_len || close_result != 0) {
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"failed to write optimizer.conf\"}");
  }

  int signal_result = system("pkill -USR2 optimizer");
  if (signal_result != 0) {
    return osi_set_response(_ctx->conn, 503, "application/json",
                            "{\"error\":\"optimizer.conf saved but reload signal failed\"}");
  }

  uds_client_send(RELOAD);
  uds_client_send(RUN);

  return osi_set_response(_ctx->conn, 200, "application/json", "{\"status\":\"config saved\"}");
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

int osi_kill(Osi_RequestCtx* _ctx)
{

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
  printf("opti instance api on finish\n");
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
    printf("OPTI_INSTANCE_INITIALIZING\n");
    Instance->state = OPTI_INSTANCE_REQUEST_PARSING;
  } break;

  case OPTI_INSTANCE_REQUEST_PARSING: {
    printf("OPTI_INSTANCE_REQUEST_PARSING\n");
    Instance->state = worktask_request_parse(Instance);
  } break;

  case OPTI_INSTANCE_IDLING:
    break;

  case OPTI_INSTANCE_RESPONSE_BUILDING: {
    printf("OPTI_INSTANCE_RESPONSE_BUILDING\n");
    Instance->state = worktask_response_build(Instance);
  } break;

  case OPTI_INSTANCE_RESPONSE_SENDING: {
    printf("OPTI_INSTANCE_RESPONSE_SENDING\n");
    Instance->state = OPTI_INSTANCE_DISPOSING;
  } break;

  case OPTI_INSTANCE_DISPOSING: {
    printf("OPTI_INSTANCE_DISPOSING\n");
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
