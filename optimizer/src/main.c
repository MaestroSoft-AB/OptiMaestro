#define _POSIX_C_SOURCE 200809L

#include "optimizer.h"
#include "signal_handler.h"
#include "error.h"
#include <time.h>

/* ---------------------------- Signals ----------------------------- */

/* Flags to be activated when signal is recieved */
static volatile sig_atomic_t sig_ignore = 0;
static volatile sig_atomic_t sig_exit = 0;
static volatile sig_atomic_t sig_new_data = 0;
static volatile sig_atomic_t sig_update_config = 0;

/* Sighandlers to activate flag */
void sig_handle_ignore(int _sig)        { sig_ignore = 1; }
void sig_handle_exit(int _sig)          { sig_exit = 1; }
void sig_handle_new_data(int _sig)      { sig_new_data = 1; }
void sig_handle_update_config(int _sig) { sig_update_config = 1; }

static Signal_Wrapper Signals[] = {
  { &sig_ignore,        &sig_handle_ignore,        SIGINT  },
  { &sig_exit,          &sig_handle_exit,          SIGTERM },
  { &sig_new_data,      &sig_handle_new_data,      SIGUSR1 },
  { &sig_update_config, &sig_handle_update_config, SIGUSR2 },
};

static const int sigc = sizeof(Signals) / sizeof(Signals[0]);

/* ------------------------------------------------------------------ */

struct timespec req = { 0, 100 }; // nanosleep delay, .1 ms

int main(int _argc, const char** _argv)
{
  signal_handlers_install(sigc, Signals);
  
  Optimizer Opti;
  if (optimizer_init(&Opti) != SUCCESS) {
    perror("optimizer_init");
    exit(1);
  }

  /* Process handler, handle signals */
  while (1)
  {
    /* TODO: Switch out printf statements for logger functions */
    if (sig_exit) {
      printf("%s - Shutdown...\n", _argv[0]);
      // TODO: eventual cleanup of running tasks 
      exit(SUCCESS); 
    } else if (sig_ignore) {
      // Do absolutely nothing
      // But perhaps log the event if debug is set 
      // (should be done in handler though so exact sig can be logged)
      sig_ignore = 0;
    } else if (sig_new_data) {
      printf("%s - Get new cache and calculations...\n", _argv[0]);
      optimizer_run(&Opti);
      sig_new_data = 0;
    } else if (sig_update_config) {
      printf("%s - Update config...\n", _argv[0]);
      optimizer_config_set(&Opti, OPTI_CONFIG_PATH);
      sig_update_config = 0;
    }

    nanosleep(&req, NULL);
  }

  return SUCCESS;
}
