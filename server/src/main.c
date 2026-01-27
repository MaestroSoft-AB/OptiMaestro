#define _POSIX_C_SOURCE 200809L
#include "opti/opti_server.h"
#include <maestromodules/scheduler.h>
#include <maestroutils/signal_handler.h>

volatile sig_atomic_t stop = 0;
Opti_Server Server;

/* Graceful exit */
void handle_sigint(int sig)
{
  (void)sig;
  printf("\r\nShutting down server gracefully...\r\n");
  opti_s_dispose(&Server);

#ifdef CURL_GLOBAL_DEFAULT
  curl_global_cleanup();
#endif

  stop = 1;
}

int main(void)
{

  scheduler_init();
  opti_s_init(&Server);

#ifdef CURL_GLOBAL_DEFAULT
  curl_global_init(
      CURL_GLOBAL_DEFAULT); // To avoid as little still reachable memory reallocs we only run this
                            // once, this should be removed when we switch to http_client
#endif

  signal(SIGINT, handle_sigint);

  while (!stop) {
    scheduler_work(SystemMonotonicMS());
  }

  handle_sigint(SIGINT);
  return 0;
}
