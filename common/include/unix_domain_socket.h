#ifndef __UNIX_DOMAIN_SOCKET_H__
#define __UNIX_DOMAIN_SOCKET_H__
#define SOCKET_PATH "/run/maestro/optimizer.sock"
#define RUN "run"
#define RELOAD "reload"


int uds_client_send(const char* _command);
#endif
