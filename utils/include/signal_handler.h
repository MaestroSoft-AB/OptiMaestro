#ifndef __SIGNAL_HANDLER_H__
#define __SIGNAL_HANDLER_H__

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
  volatile sig_atomic_t* flag; 
  __sighandler_t         handler;
  int                    sig;

} Signal_Wrapper;

/* Install signal overrides for array of wrapper structs */
static void signal_handlers_install(int _c, Signal_Wrapper* _S) {
  for (int i = 0; i < _c; i++) {
    struct sigaction sa;
    sa.sa_handler = _S[i].handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(_S[i].sig, &sa, NULL) == -1) {
      perror("sigaction");
      exit(EXIT_FAILURE);
    }
  }
}

#endif
