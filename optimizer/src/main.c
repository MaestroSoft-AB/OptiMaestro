#define _POSIX_C_SOURCE 200809L

#include "signal_handler.h"
#include <time.h>

/* ---------------------- Signals (declared for async signal safety) ---------------------- */

static volatile sig_atomic_t sig_ignore = 0;
static volatile sig_atomic_t sig_exit = 0;
static volatile sig_atomic_t sig_new_data = 0;
static volatile sig_atomic_t sig_clear_cache = 0;

/* Sighandlers */
void sig_handle_ignore(int _sig)      { /* do absolutely nothing */ }
void sig_handle_exit(int _sig)        { sig_exit = 1; }
void sig_handle_new_data(int _sig)    { sig_new_data = 1; }
void sig_handle_clear_cache(int _sig) { sig_clear_cache = 1; }


static Signal_Wrapper Signals[] = {
  { &sig_ignore,      &sig_handle_ignore,      SIGINT },
  { &sig_exit,        &sig_handle_exit,        SIGTERM },
  { &sig_new_data,    &sig_handle_new_data,    SIGUSR1 },
  { &sig_clear_cache, &sig_handle_clear_cache, SIGUSR2 },
};

static const int signals_c = sizeof(Signals) / sizeof(Signals[0]);

/* ---------------------------------------------------------------------------------------- */

struct timespec req = { 0, 100 }; // nanosleep delay, .1 ms

int main(int _argc, const char** _argv)
{
  /* TODO: Start handler threads */
  
  /* install signal handlers */
  install_handlers(signals_c, Signals);

  /* Process handler, handle signals */
  while (1)
  {
    if (sig_exit)
    {
      // TODO: eventual cleanup of running tasks 
      printf("Optimizer exit!\n");
      exit(EXIT_SUCCESS); 
    }
    else if (sig_new_data)
    {
      printf("Get new cache and calulations!\n");

      sig_new_data = 0;
    }
    else if (sig_clear_cache)
    {
      printf("Clear cache!\n");

      sig_clear_cache = 0;
    }

    nanosleep(&req, NULL);
  }

  return 0;
}
