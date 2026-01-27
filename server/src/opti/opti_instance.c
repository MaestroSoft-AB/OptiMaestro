#include "opti/opti_instance.h"
#include <stdio.h>

//-----------------Internal Functions-----------------
//
int osi_on_http_connection(void* _context, HTTP_Server_Connection* _Connection);
int osi_on_request(void* _context);
int osi_on_dispose(void* _context);

void osi_taskwork(void* _context, uint64_t _montime);
OptiServerInstanceState worktask_request_parse(Opti_Server_Instance* _Instance);
OptiServerInstanceState worktask_response_build(Opti_Server_Instance* _Instance);
//----------------------------------------------------

/* Functions to be called on specific path from request */
int osi_get_solar_data(Osi_RequestCtx* _ctx);
int osi_get_temp_1_data(Osi_RequestCtx* _ctx);
int osi_get_jacuzzi_data(Osi_RequestCtx* _ctx);
int osi_get_overview(Osi_RequestCtx* _ctx);

/* REMEMBER TO CHANGE COUNT WHEN ADDING ENDPOINT! */
#define ENDPOINTS_COUNT 4

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
                                                        }};

//--------------------------------------------------------------------------//

/*******************SOME RESPONSEBUILDING, MAYBE MOVE THIS?****************/
static int osi_set_response(HTTP_Server_Connection* _Conn, int _status_code,
                            const char* _content_type, const char* _body)
{
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
  _Conn->weather_done = 1; // Change this to a more appropriate name

  return SUCCESS;
}
/*******************************ENDPOINT FUNCTIONS************************/
int osi_get_solar_data(Osi_RequestCtx* _ctx)
{
  if (!_ctx || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  // Swap body with request for actual data here
  const char* body = "{\"Solar\":\"No sun until April\"}";

  return osi_set_response(_ctx->conn, 200, "application/json", body);
}
int osi_get_temp_1_data(Osi_RequestCtx* _ctx)
{
  if (!_ctx || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  // Swap body with request for actual data here
  const char* body = "{\"Temp-Sensor\":\"It's a me, Mario\"}";
  return osi_set_response(_ctx->conn, 200, "application/json", body);
}
int osi_get_jacuzzi_data(Osi_RequestCtx* _ctx)
{
  if (!_ctx || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  // Swap body with request for actual data here
  const char* body = "{\"Jacuzzi\":\"Out of water\"}";

  return osi_set_response(_ctx->conn, 200, "application/json", body);
}

int osi_get_overview(Osi_RequestCtx* _ctx)
{
  if (!_ctx || !_ctx->conn->request) {
    return ERR_INVALID_ARG;
  }

  // Swap body with request for actual data here
  const char* body = "{\"overview\":Yahoooooooo}";
  return osi_set_response(_ctx->conn, 200, "application/json", body);
}
/*************************************************************************/
/***************************************************************************************************************/
int osi_init(void* _context, Opti_Server_Instance* _Instance, HTTP_Server_Connection* _Connection)
{
  if (!_Instance || !_Connection) {
    return ERR_INVALID_ARG;
  }

  memset(_Instance, 0, sizeof(Opti_Server_Instance));

  _Instance->context = _context;
  _Instance->task = NULL;
  _Instance->http_connection = _Connection;
  http_server_connection_set_callback(_Instance->http_connection, _Instance, osi_on_request,
                                      osi_on_dispose);

  return SUCCESS;
}

int osi_init_ptr(void* _context, HTTP_Server_Connection* _Connection,
                 Opti_Server_Instance** _Instance_Ptr)
{

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

int osi_on_request(void* _context)
{
  if (!_context) {
    return ERR_INVALID_ARG;
  }

  Opti_Server_Instance* _Instance = (Opti_Server_Instance*)_context;
  _Instance->state = OPTI_INSTANCE_INITIALIZING;
  _Instance->task = scheduler_create_task(_Instance, osi_taskwork);

  return SUCCESS;
}

int osi_on_dispose(void* _context)
{
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
    _Instance->http_connection = NULL;
    http_server_connection_dispose_ptr(&conn);
  }

  if (_Instance->on_finish) {
    _Instance->on_finish(_Instance->context, _Instance);
  }

  return SUCCESS;
}

int osi_on_api_finish(void* _context)
{
  if (!_context) {
    return ERR_INVALID_ARG;
  }

  Opti_Server_Instance* Instance = (Opti_Server_Instance*)_context;
  printf("opti instance api on finish\n");
  Instance->state = OPTI_INSTANCE_RESPONSE_BUILDING;

  return SUCCESS;
}

/* --------------TASKWORK STATE FUNCTIONS-------------- */

OptiServerInstanceState worktask_request_parse(Opti_Server_Instance* _Instance)
{
  if (!_Instance || !_Instance->http_connection->request) {
    return OPTI_INSTANCE_ERROR;
  }

  HTTP_Request* req = _Instance->http_connection->request;
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

OptiServerInstanceState worktask_response_build(Opti_Server_Instance* _Instance)
{
  if (!_Instance || !_Instance->http_connection) {
    return OPTI_INSTANCE_ERROR;
  }

  Osi_RequestCtx ctx = {
      .ctx = _Instance->context,
      .instance = _Instance,
      .conn = _Instance->http_connection,
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

void osi_taskwork(void* _context, uint64_t _montime)
{

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

void osi_dispose(Opti_Server_Instance* _Instance)
{
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
  _Instance->endpoint = NULL;
  _Instance->state = OPTI_INSTANCE_DISPOSING;
}

void osi_dispose_ptr(Opti_Server_Instance** _Instance_Ptr)
{
  if (!_Instance_Ptr || !*(_Instance_Ptr)) {
    return;
  }

  osi_dispose(*(_Instance_Ptr));
  free(*(_Instance_Ptr));
  *_Instance_Ptr = NULL;
}
