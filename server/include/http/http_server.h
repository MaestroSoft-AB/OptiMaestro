#ifndef __HTTP_SERVER_H_
#define __HTTP_SERVER_H_


/* ******************************************************************* */
/* ************************** HTTP SERVER **************************** */
/* **** **************************************************************** */
/* This is the main HTTP building block
 * It spawns the TCP Server and a scheduler task for every connection made */

#include "http/http_connection.h"
#include "tcp/tcp_server.h"

typedef enum {
  HTTP_SERVER_ERROR_NONE = 0,
  HTTP_SERVER_ERROR_INVALID_ARGUMENT, /*Null pointers negative fd's*/ // what?
  HTTP_SERVER_ERROR_TCP_INIT_FAILED,
  HTTP_SERVER_ERROR_CREATE_TASK_FAILED,
  HTTP_SERVER_ERROR_ACCEPT_FAILED

}HTTPServerErrorState;

typedef enum {
  HTTP_SERVER_INITIALIZING,
  HTTP_SERVER_IDLING,
  HTTP_SERVER_CONNECTING,
  HTTP_SERVER_CONNECTED,
  HTTP_SERVER_ERROR,
  HTTP_SERVER_DISPOSING

} HTTPServerState;

typedef int (*http_server_on_connection)(void* _Context, HTTP_Server_Connection* _Connection);
typedef int (*http_retry_function)(void *);

typedef struct
{
	http_server_on_connection on_connection;
  void*                     context;
	Scheduler_Task*           task;
	TCP_Server                tcp_server; //Redo as ptr
  char                      port[16];
  HTTPServerState           state;
  HTTPServerErrorState      error_state;
  int                       client_fd;
  int                       error_retries;
  uint64_t                  next_retry_at;
  http_retry_function       retry_function;
  void*                     retry_args;

} HTTP_Server;

typedef struct {
    TCP_Server *tcp_server;
    const char *port;
    tcp_server_on_accept on_accept;
    void *context; /*HTTP_Server*/

} HTTP_Tcp_Init_Args;


int http_server_init(HTTP_Server* _Server, http_server_on_connection _on_connection, void* _context);
int http_server_init_ptr(http_server_on_connection _on_connection, void* _context, HTTP_Server** _Server_Ptr);

int http_server_on_accept(int _fd, void* _context);
void http_server_dispose(HTTP_Server* _Server);
void http_server_dispose_ptr(HTTP_Server** _Server_Ptr);


#endif 
