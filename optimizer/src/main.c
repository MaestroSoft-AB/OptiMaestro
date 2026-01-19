#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdatomic.h>

/* ---------------------- signals (declared for async signal safety) ---------------------- */

static volatile sig_atomic_t sig_exit = 0;
static volatile sig_atomic_t sig_new_cache = 0;
static volatile sig_atomic_t sig_ = 0;

/* ---------------------------------------------------------------------------------------- */

typedef enum
{

} Signals;

static void signal_handler(int _sig) {
  switch (_sig)
  {
    case SIGINT:
      break;
    case SIGTERM:
      sig_exit = 1;
      break;
    case SIGUSR1:
      sig_new_cache = 1;
      break;
    case SIGUSR2:
      break;

  }
}

static void install_handler(int signum) {
  struct sigaction sa;
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;  // or SA_RESTART

  if (sigaction(signum, &sa, NULL) == -1) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }
}

int main(int _argc, const char** _argv)
{
  /* TODO: Start handler threads */

  /* Process handler, handle signals */
  for (;;)
  {
    if (sig_exit)
    {
      // TODO: cleanup
      printf("Shutting down Optimizer.\n");
      return 0;
    }

  }

  return 0;
}
