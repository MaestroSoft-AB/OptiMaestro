#include "http/http_server.h"

#include <maestroutils/config_handler.h>

#include <stdio.h>
#include <string.h>

/* -----------------Internal Functions----------------- */
void http_server_taskwork(void* _context, uint64_t _montime);
int http_server_on_accept(int _fd, void* _context);
int http_retry_tcp_init(void* _args);
HTTPServerState http_server_retry_work(HTTP_Server* _Server);
HTTPServerState http_server_error_work(HTTP_Server* _Server);
/* ---------------------------------------------------- */

int http_server_init(HTTP_Server* _HTTPServer, http_server_on_connection _Callback,
                     void* _ContextServer)
{
  if (!_HTTPServer || !_Callback) {
    return ERR_INVALID_ARG;
  }

  /* Default port (previous behavior) */
  snprintf(_HTTPServer->port, sizeof(_HTTPServer->port), "%s", "10580");

  /* Prefer http.port from config if present.
     When started via server/Makefile, CWD is typically server/, so this resolves to server/config/system.conf. */
  {
    const char* keys[] = {"http.port"};
    char port_buf[16] = {0};
    char* values[] = {port_buf};
    if (config_get_value("config/system.conf", keys, values, sizeof(port_buf), 1) == 0) {
      if (port_buf[0] != 0) {
        snprintf(_HTTPServer->port, sizeof(_HTTPServer->port), "%s", port_buf);
      }
    }
  }

  _HTTPServer->context = _ContextServer;
  _HTTPServer->on_connection = _Callback;
  _HTTPServer->state = HTTP_SERVER_INITIALIZING;
  _HTTPServer->error_state = HTTP_SERVER_ERROR_NONE;
  _HTTPServer->task = NULL;
  _HTTPServer->client_fd = -1;
  _HTTPServer->error_retries = 0;
  _HTTPServer->next_retry_at = 0;
  _HTTPServer->retry_args = NULL;
  _HTTPServer->retry_function = NULL;

  int result = tcp_server_init(&_HTTPServer->tcp_server, _HTTPServer->port, http_server_on_accept,
                               _HTTPServer);
  if (result != SUCCESS) {
    _HTTPServer->state = HTTP_SERVER_ERROR;
    _HTTPServer->error_state = HTTP_SERVER_ERROR_TCP_INIT_FAILED;

    HTTP_Tcp_Init_Args* args = (HTTP_Tcp_Init_Args*)malloc(sizeof(HTTP_Tcp_Init_Args));
    if (!args) {
      return ERR_NO_MEMORY;
    }

    /*Pack the arguments needed for retrying tcp_init*/
    args->tcp_server = &_HTTPServer->tcp_server;
    args->port = _HTTPServer->port;
    args->on_accept = http_server_on_accept;
    args->context = _HTTPServer;

    _HTTPServer->error_retries = 0;
    _HTTPServer->retry_function = http_retry_tcp_init;
    _HTTPServer->retry_args = args;
    _HTTPServer->next_retry_at = SystemMonotonicMS() + 30000; /*30s timeout between retries*/

    _HTTPServer->task = scheduler_create_task(_HTTPServer, http_server_taskwork);

    return result;
  }

  /*This wont matter as if there is no task nothing will execute error code*/
  _HTTPServer->task = scheduler_create_task(_HTTPServer, http_server_taskwork);
  if (!_HTTPServer->task) {
    fprintf(stderr, "scheduler_create_task failed\n");
    tcp_server_dispose(&_HTTPServer->tcp_server);
    _HTTPServer->state = HTTP_SERVER_ERROR;
    return ERR_FATAL;
  }

  _HTTPServer->state = HTTP_SERVER_IDLING;
  return SUCCESS;
}

int http_server_initiate_ptr(http_server_on_connection _callback, void* _context,
                             HTTP_Server** _ServerPtr)
{
  if (_ServerPtr == NULL) {
    return ERR_INVALID_ARG;
  }

  HTTP_Server* _Server = (HTTP_Server*)malloc(sizeof(HTTP_Server));
  if (_Server == NULL) {
    return ERR_NO_MEMORY;
  }

  int result = http_server_init(_Server, _callback, _context);
  if (result != SUCCESS) {
    perror("http_server_init");
    free(_Server);
    return result;
  }

  *(_ServerPtr) = _Server;

  return SUCCESS;
}

HTTPServerState http_server_connection_handover(int _fd, void* _Context)
{
  if (!_Context || _fd < 0) {
    return HTTP_SERVER_ERROR;
  }

  HTTP_Server* Server = (HTTP_Server*)_Context;

  /*TCP Server has accepted client*/

  HTTP_Server_Connection* Connection = NULL;
  int result = http_server_connection_init_ptr(_fd, &Connection);
  if (result != SUCCESS) {
    perror("http_server_connection_init_ptr");
    close(_fd);
    Server->client_fd = -1;
    return HTTP_SERVER_IDLING;
  }

  result = Server->on_connection(Server, Connection);
  if (result != SUCCESS) {
    perror("Server->on_connection");
    close(_fd);

    Server->client_fd = -1;
    return HTTP_SERVER_IDLING;
  }

  /*IF httpserverconnection is initialized succesfully*/
  return HTTP_SERVER_CONNECTED;
}

int http_server_on_accept(int _fd, void* _Context)
{
  if (!_Context || _fd < 0) {
    return ERR_INVALID_ARG;
  }

  printf("on accept fd: %i\n", _fd);
  HTTP_Server* Server = (HTTP_Server*)_Context;
  Server->client_fd = _fd;
  Server->state = HTTP_SERVER_CONNECTING;

  return SUCCESS;
}

void http_server_taskwork(void* _context, uint64_t _montime)
{
  if (!_context) {
    return;
  }

  HTTP_Server* server = (HTTP_Server*)_context;

  HTTPServerState next_state = server->state;

  switch (server->state) {
  case HTTP_SERVER_INITIALIZING:
    break;

  case HTTP_SERVER_IDLING: {
    break;
  }

  case HTTP_SERVER_CONNECTING: {
    printf("HTTP_SERVER_CONNECTING\n");
    next_state = http_server_connection_handover(server->client_fd, server);
    break;
  }
  case HTTP_SERVER_CONNECTED: {
    printf("HTTP_SERVER_CONNECTED\n");
    next_state = HTTP_SERVER_IDLING;
    break;
  }
  case HTTP_SERVER_ERROR:
    // printf("HTTP_SERVER_ERROR\n");
    next_state = http_server_error_work(server);
    break;

  case HTTP_SERVER_DISPOSING: {
    printf("HTTP_SERVER_DISPOSING\n");
    http_server_dispose(server);
    return;
  }

  default:
    break;
  }

  server->state = next_state;
}

int http_retry_tcp_init(void* _args)
{
  HTTP_Tcp_Init_Args* args = (HTTP_Tcp_Init_Args*)_args;
  return tcp_server_init(args->tcp_server, args->port, args->on_accept, args->context);
}

HTTPServerState http_server_error_work(HTTP_Server* _Server)
{

  switch (_Server->error_state) {

  case HTTP_SERVER_ERROR_NONE:
    return HTTP_SERVER_IDLING;

  case HTTP_SERVER_ERROR_INVALID_ARGUMENT:
    /*Errors not solved by retry*/
    return HTTP_SERVER_DISPOSING;

  case HTTP_SERVER_ERROR_ACCEPT_FAILED:
    return http_server_retry_work(_Server);

  case HTTP_SERVER_ERROR_TCP_INIT_FAILED:
    return http_server_retry_work(_Server);

  default:
    return HTTP_SERVER_DISPOSING;
  }

  return HTTP_SERVER_DISPOSING;
}

HTTPServerState http_server_retry_work(HTTP_Server* _Server)
{
  if (!_Server) {
    return HTTP_SERVER_DISPOSING;
  }

  uint64_t now = SystemMonotonicMS();

  if (now < _Server->next_retry_at)
    return HTTP_SERVER_ERROR;

  if (!_Server->retry_function) {
    return HTTP_SERVER_DISPOSING;
  }

  int result = _Server->retry_function(_Server->retry_args);

  if (result == SUCCESS) {
    free(_Server->retry_args);

    _Server->retry_args = NULL;
    _Server->retry_function = NULL;
    _Server->error_state = HTTP_SERVER_ERROR_NONE;
    _Server->error_retries = 0;
    return HTTP_SERVER_IDLING;

  } else {
    _Server->error_retries++;

    if (_Server->error_retries > 3) {

      free(_Server->retry_args);
      _Server->retry_args = NULL;
      _Server->retry_function = NULL;
      return HTTP_SERVER_DISPOSING;

    } else {
      _Server->next_retry_at = now + 30000;
      return HTTP_SERVER_ERROR;
    }
  }
}

void http_server_dispose(HTTP_Server* _Server)
{
  if (!_Server)
    return;

  if (_Server->task) {
    scheduler_destroy_task(_Server->task);
    _Server->task = NULL;
  }

  tcp_server_dispose(&_Server->tcp_server);

  memset(_Server, 0, sizeof(HTTP_Server));

  _Server = NULL;
}

void http_server_dispose_ptr(HTTP_Server** _ServerPtr)
{
  if (_ServerPtr == NULL || *(_ServerPtr) == NULL)
    return;

  http_server_dispose(*(_ServerPtr));
  free(*(_ServerPtr));
  *(_ServerPtr) = NULL;
}
