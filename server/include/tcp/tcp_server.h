#ifndef __TCP_SERVER_H__
#define __TCP_SERVER_H__

/* ******************************************************************* */
/* *************************** TCP SERVER **************************** */
/* ******************************************************************* */

#define _POSIX_C_SOURCE 200809L /* This must be defined before netdb.h */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


#include "scheduler.h"

#define BACKLOG 10000
#define MAX_CLIENTS 10000
#define TCP_ACCEPT_NO_CONNECTION -1
#define TCP_ACCEPT_FATAL_ERROR -2

typedef enum
{
  TCP_SERVER_INITIALIZING,
  TCP_SERVER_LISTENING,
  TCP_SERVER_CONNECTING,
  TCP_SERVER_CONNECTED,
  TCP_SERVER_DISPOSING,
  TCP_SERVER_ERROR

} TCPServerState;

typedef int (*tcp_server_on_accept)(int _fd, void* _context);

typedef struct
{
  const char* port;
  tcp_server_on_accept on_accept;
  void* context; /*HTTP_Server*/

} TCP_Init_Args;

typedef struct
{
  tcp_server_on_accept on_accept;
  void* context;
  int fd;
  const char* port;
  TCPServerState state;
  Scheduler_Task* task;
  int client_fd;
  TCP_Init_Args* args;

} TCP_Server;

int tcp_server_init(TCP_Server* _Server, const char* _port, tcp_server_on_accept _on_accept,
                    void* _context);
int tcp_server_init_ptr(TCP_Server** _Server_Ptr, const char* _port,
                        tcp_server_on_accept _on_accept, void* _context);

int tcp_server_accept(TCP_Server* _Server);

void tcp_server_dispose(TCP_Server* _Server);
void tcp_server_dispose_ptr(TCP_Server** _Server_Ptr);

void tcp_server_work(TCP_Server* _Server);

#endif /* __TCP_SERVER_H__ */
