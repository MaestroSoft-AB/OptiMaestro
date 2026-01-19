#ifndef __HTTP_SERVER_CONNECTION_H_
#define __HTTP_SERVER_CONNECTION_H_

/* ******************************************************************* */
/* ************************ HTTP CONNECTION ************************** */
/* ******************************************************************* */
/* This is the individual connection made between server and client
 * It handles the tasks between recieving a request and send a response*/

#include "http_parser.h"
#include "scheduler.h"
#include "tcp_client.h"

#define TCP_MESSAGE_BUFFER_MAX_SIZE                                                                \
  128 // Size of initial tcp_read buffer without reallocating more mem
#define HTTP_SERVER_CONNECTION_FIRSTLINE_MAXLEN 1024 // Maximum length of http request's first line
#define HTTP_SERVER_CONNECTION_HEADERS_MAXLEN 1024   // Maximum length of http request's first line
#define HTTP_SERVER_CONNECTION_MAX_RETRIES                                                         \
  20 // max amount of times to loop parse functions before erroring
#define HTTP_REQUEST_MAX_PARAMS 10

/* The usecase of the function pointer is to let the connection instance point back to a server
 * nstance's function without knowing exactly what it is or needs */
typedef int (*http_server_connection_on_request)(void* _context);
/* The usecase of the function pointer is to let the instance know the response has been sent and
 * the connection+instance should dispose */
typedef int (*http_server_connection_on_dispose)(void* _context);

typedef enum
{
  HTTP_SERVER_CONNECTION_INITIALIZING,
  HTTP_SERVER_CONNECTION_READING_FIRSTLINE,
  HTTP_SERVER_CONNECTION_READING_HEADERS,
  HTTP_SERVER_CONNECTION_READING_BODY,
  HTTP_SERVER_CONNECTION_VALIDATING,
  HTTP_SERVER_CONNECTION_WEATHER_HANDOVER,
  HTTP_SERVER_CONNECTION_RESPONDING,
  HTTP_SERVER_CONNECTION_DISPOSING,
  HTTP_SERVER_CONNECTION_ERROR,

} HTTPServerConnectionState;

typedef struct
{
  HTTPServerConnectionState state;
  Scheduler_Task* task;
  void* context;

  uint8_t line_buf[HTTP_SERVER_CONNECTION_FIRSTLINE_MAXLEN];
  int line_buf_len;
  int retries; // counter for parsing to avoid infinite loop on lost tcp connection

  TCP_Client tcp_client;
  HTTP_Request* request;
  HTTP_Response* response;

  http_server_connection_on_request on_request;
  http_server_connection_on_dispose on_dispose;
  int content_length;
  int weather_done;

} HTTP_Server_Connection;

int http_server_connection_init(HTTP_Server_Connection* _Connection, int _fd);
int http_server_connection_init_ptr(int _fd, HTTP_Server_Connection** _Connection_Ptr);

/* To be called by the dependent module to define what to run when request is succesfully validated
 * and parsed, and what when response has been sent */
void http_server_connection_set_callback(HTTP_Server_Connection* _Connection, void* _Context,
                                         http_server_connection_on_request _on_request,
                                         http_server_connection_on_dispose _on_dispose);

void http_server_connection_dispose(HTTP_Server_Connection* _Connection);
void http_server_connection_dispose_ptr(HTTP_Server_Connection** _Connection_Ptr);
void http_handle_request(const uint8_t* _data, size_t _len);

#endif
