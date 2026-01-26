#include "tcp/tcp_server.h"

/* -----------------Internal Functions----------------- */

void tcp_server_taskwork(void* _Context, uint64_t _MonTime);
TCPServerState tcp_server_handle_listening(TCP_Server* _Server, uint64_t _montime);
TCPServerState tcp_server_connection_handover(TCP_Server* _Server);

/* ---------------------------------------------------- */

int tcp_server_set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return ERR_IO;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return ERR_IO;
  }

  return SUCCESS;
}

int tcp_server_init(TCP_Server* _Server, const char* _port, tcp_server_on_accept _on_accept, void* _context) {
  if (!_Server || !_port || !_on_accept) {
    return ERR_INVALID_ARG;
  }
  _Server->context = _context;
  _Server->on_accept = _on_accept; 
  _Server->context = _context;
  _Server->fd = -1;
  _Server->port = _port;
  _Server->state = TCP_SERVER_INITIALIZING;
  _Server->task = NULL;

  struct addrinfo addresses;
  memset(&addresses, 0, sizeof(addresses));
  addresses.ai_family = AF_UNSPEC; /*Both IPV4 & IPV6*/
  addresses.ai_socktype = SOCK_STREAM; /*TCP*/
  addresses.ai_flags = AI_PASSIVE; /*All network interfaces*/

  struct addrinfo* res = NULL;
  int getAddressInfo = getaddrinfo(NULL, _Server->port, &addresses, &res);
  if (getAddressInfo != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getAddressInfo));
    return ERR_IO;
  }

  int fd = -1;
  
  for (struct addrinfo* addressInfo = res; addressInfo != NULL; addressInfo = addressInfo->ai_next) {
    fd = socket(addressInfo->ai_family, addressInfo->ai_socktype, addressInfo->ai_protocol);
    if (fd < 0) continue; /*Try next*/
    
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
      perror("setsockopt");
    }
  
    if (bind(fd, addressInfo->ai_addr, addressInfo->ai_addrlen) != 0) {
      perror("bind");
      close(fd);
      fd = -1;
      continue; /*try next*/
    }

    if (listen(fd, BACKLOG) != 0) {
      perror("listen");
      close(fd);
      fd = -1;
      continue;
    }

    _Server->fd = fd;

    if (tcp_server_set_nonblocking(_Server->fd) != SUCCESS) {
      _Server->fd = -1;
      fd = -1;
      continue;
    }

    printf("Server listening on port: %s\n", _Server->port);
    freeaddrinfo(res);
	  
    _Server->state = TCP_SERVER_LISTENING;
    _Server->task = scheduler_create_task(_Server, tcp_server_taskwork);

    return SUCCESS;
  }
  
  /*No addresses found*/
  freeaddrinfo(res);
  return ERR_IO;
}


int tcp_server_init_ptr(TCP_Server** _Server_Ptr, const char* _port, tcp_server_on_accept _on_accept, void* _context) {
  if (!_Server_Ptr) {
    return ERR_INVALID_ARG;
  }

  TCP_Server* server = (TCP_Server*)malloc(sizeof(TCP_Server));
  if (!server) {
    perror("malloc");
    return ERR_NO_MEMORY;
  }
  int result = tcp_server_init(server, _port, _on_accept, _context);
  if (result != SUCCESS) {
    /*tcp_server_init already set errno*/
    free(server);
    return result;
  }
  
  *(_Server_Ptr) = server;

  return SUCCESS;
}

int tcp_server_accept(TCP_Server *_Server) {
  if (!_Server) {
    return ERR_INVALID_ARG;
  }

  struct sockaddr_storage address; /*Works for both ipv4 & ipv6*/
  socklen_t addressLength = sizeof(address);

  /* int client_fd = accept(_Server->fd, (struct sockaddr*)&address, &addressLength); */
  int client_fd = accept(_Server->fd, (struct sockaddr*)&address, &addressLength);
  if (client_fd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return ERR_WOULD_BLOCK;
    }
    perror("accept");
    return ERR_CONNECTION_FAIL;
  }

  if (tcp_server_set_nonblocking(client_fd) != SUCCESS) {
    perror("tcp_server_set_nonblocking");
    close(client_fd);
    return ERR_IO;
  }

  _Server->client_fd = client_fd;
  return SUCCESS;
}

void tcp_server_taskwork(void* _Context, uint64_t _MonTime)
{
  if (!_Context) 
    return;
  TCP_Server* server = (TCP_Server*)_Context;
  
  TCPServerState next_state = server->state;

  switch(server->state) {
    case TCP_SERVER_INITIALIZING:
      break;
    case TCP_SERVER_LISTENING:
      next_state = tcp_server_handle_listening(server, _MonTime);
      break;
    case TCP_SERVER_CONNECTING:
      next_state = tcp_server_connection_handover(server);
      break;
    case TCP_SERVER_CONNECTED:
      next_state = TCP_SERVER_LISTENING;
      break;
    case TCP_SERVER_ERROR:
      break;
    case TCP_SERVER_DISPOSING:
      tcp_server_dispose(server);
      return;
  }
  server->state = next_state;
}

TCPServerState tcp_server_handle_listening(TCP_Server* _Server, uint64_t _montime) {
  
  if (!_Server) {
    return TCP_SERVER_ERROR;
  }

  int result = tcp_server_accept(_Server);
      
  if (result == SUCCESS) {
    /*Connection accepted*/
    return TCP_SERVER_CONNECTING;
  }

  if (result == ERR_WOULD_BLOCK) {
    /*No connection yet*/
    return TCP_SERVER_LISTENING;
  } 

  if (result == ERR_CONNECTION_FAIL || result == ERR_FATAL) {

    /* _Server->args = (TCP_Init_Args*)malloc(sizeof(TCP_Init_Args)); */

    /* if (!_Server->args) {
      errno = ENOMEM;
      return TCP_SERVER_ERROR;
    } */
    // Why do we malloc at fatal error? I commented it out because it runs AFTER dispose because of taskwork so no chance of freeing
    
    /*    args->port = "58080";
    args->on_accept = http_server_on_accept;
    args->context = _Server;

    _Server->error_retries = 0;
    _Server->retry_function = http_retry_tcp_init;
    _Server->retry_args = args;
    _Server->next_retry_at = SystemMonotonicMS() + 30000;
    _Server->error_state = TCP_SERVER_ERROR_ACCEPT_FAILED;

      */
    return TCP_SERVER_ERROR;
  }
  return TCP_SERVER_ERROR;
}

TCPServerState tcp_server_connection_handover(TCP_Server* _Server) 
{
  if (!_Server) {
    return TCP_SERVER_ERROR;
  }
  int result = _Server->on_accept(_Server->client_fd, _Server->context);
  if (result != SUCCESS) {
    return TCP_SERVER_ERROR;
  }

  _Server->client_fd = -1;
  return TCP_SERVER_CONNECTED;
}

void tcp_server_dispose(TCP_Server *_Server) 
{
  if (!_Server) return;

  if (_Server->task) {
    scheduler_destroy_task(_Server->task);
    _Server->task = NULL;
  }

  printf("Args freed!\n");
  if (_Server->args != NULL)
  {
    free(_Server->args);
    _Server->args = NULL;
  }

  if (_Server->fd >= 0) {
    printf("Dispose FD %i CLOSED\n", _Server->fd);
    close(_Server->fd);
    _Server->fd = -1;
  }

  memset(_Server, 0, sizeof(TCP_Server));

  _Server = NULL;
}

void tcp_server_dispose_ptr(TCP_Server** _ServerPtr) 
{
  if (_ServerPtr == NULL || *(_ServerPtr) == NULL) {
    return;
  }
  tcp_server_dispose(*(_ServerPtr));
  free(*(_ServerPtr));
  *(_ServerPtr) = NULL;
}

