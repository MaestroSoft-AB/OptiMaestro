#include "unix_domain_socket.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <maestroutils/error.h>

int uds_client_send(const char* _command) {
  int                sock;
  struct sockaddr_un addr;

  sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (sock < 0) {
    perror("socket");
    return ERR_FATAL;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("connect");
    return ERR_FATAL;
  }

  write(sock, _command, strlen(_command));

  close(sock);
  return SUCCESS;
}

/* Create and configure the Unix‑domain server socket. */
int uds_server_start(const char* _sock_path, int* _fd_out) {
  struct sockaddr_un addr;
  int                sock;

  sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (sock < 0) {
    perror("socket");
    return ERR_FATAL;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, _sock_path, sizeof(addr.sun_path) - 1);

  unlink(_sock_path); /* Remove stale socket */
  if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(sock);
    return ERR_FATAL;
  }

  if (listen(sock, 5) < 0) {
    perror("listen");
    close(sock);
    return ERR_FATAL;
  }

  *_fd_out = sock;
  // LOG_INFO("Unix socket server listening on %s", _sock_path);
  return SUCCESS;
}
