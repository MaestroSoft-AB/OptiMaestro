#define _POSIX_C_SOURCE 200809L

#ifdef DAEMONIZE
  #include "daemon.h"
  #define USE_SYSLOG 1
#else
  #define USE_SYSLOG 0
#endif

#include "optimizer.h"
#include "unix_domain_socket.h"

#include <maestroutils/error.h>
#include <maestroutils/signal_handler.h>
#include <maestromodules/tls_global_ca.h>
#include <maestroutils/file_logging.h>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define OPTIMIZER_LOG_PATH "/var/log/maestro.log"

/* Socket flags */
static volatile sig_atomic_t socket_run_trigger = 0;
static volatile sig_atomic_t socket_reload_trigger = 0;
/* ---------------------------- Signals ----------------------------- */

/* Flags to be activated when signal is recieved */
static volatile sig_atomic_t sig_ignore = 0;
static volatile sig_atomic_t sig_exit = 0;
static volatile sig_atomic_t sig_new_data = 0;
static volatile sig_atomic_t sig_update_config = 0;

/* Sighandlers to activate flag */
void sig_handle_ignore(int _sig) { LOG_INFO("Sig called: %i", _sig); sig_ignore = 1; }
void sig_handle_exit(int _sig) { LOG_INFO("Sig called: %i", _sig); sig_exit = 1; }
void sig_handle_new_data(int _sig) { LOG_INFO("Sig called: %i", _sig); sig_new_data = 1; }
void sig_handle_update_config(int _sig) { LOG_INFO("Sig called: %i", _sig); sig_update_config = 1; }

static Signal_Wrapper Signals[] = {
  {&sig_ignore, &sig_handle_ignore, SIGINT},
  {&sig_exit, &sig_handle_exit, SIGTERM},
  {&sig_new_data, &sig_handle_new_data, SIGUSR1},
  {&sig_update_config, &sig_handle_update_config, SIGUSR2},
};

static const int sigc = sizeof(Signals) / sizeof(Signals[0]);

/* ------------------------------------------------------------------ */

/* Read from socket, 1‑shot per iteration. */
static int uds_server_step(int server_fd, Optimizer* _Opti) {
  struct sockaddr_un client_addr;
  socklen_t          addr_len = sizeof(client_addr);
  char               buf[8];  // RUN or RELOAD plus NULL
  int                client_fd;
  ssize_t            n;

  client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
  if (client_fd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return SUCCESS;   /* nothing waiting */
    }
    perror("accept");
    return ERR_FATAL;
  }

  n = read(client_fd, buf, sizeof(buf) - 1);
  if (n < 0) {
    perror("read");
    close(client_fd);
    return ERR_FATAL;
  }
  buf[n] = '\0';

  if (strcmp(buf, RUN) == 0) {
    LOG_INFO("%s - Triggered by socket: RUN", "_Optimizer");
    optimizer_run(_Opti);
  } else if (strcmp(buf, RELOAD) == 0) {
    LOG_INFO("%s - Triggered by socket: RELOAD", "_Optimizer");
    optimizer_config_set(&_Opti->config);
  } else if (strcmp(buf, KILL) == 0) {
    LOG_INFO("%s - Triggered by socket: KILL", "_Optimizer");
    // printf("%s - Shutdown...\n", _argv[0]);
    optimizer_dispose(_Opti);
    global_tls_ca_dispose();
    log_close();
    exit(SUCCESS);
  } else {
    LOG_INFO("unknown socket command: '%s'", buf);
  }

  close(client_fd);
  return SUCCESS;
}
struct timespec req = {0, 100}; // nanosleep delay, .1 ms

/* ------------------------------------------------------------------ */

int main(int _argc, const char** _argv)
{
  (void)_argc;
#ifdef DAEMONIZE
  if (daemonize(_argv[0], "/run/maestro/optimizer.pid") != SUCCESS) {
    syslog(LOG_ERR, "%s - daemonization failed", _argv[0]);
    exit(1);
  }
  
  if (log_init(OPTIMIZER_LOG_PATH) != 0) {
    syslog(LOG_ERR, "log_init failed for %s", OPTIMIZER_LOG_PATH);
    exit(1);
  }
  LOG_INFO("Daemon fully started (PID %d)", getpid());
#else
  if (log_init(OPTIMIZER_LOG_PATH) != 0) {
    perror("log_init");  // Only non-daemon uses perror
    exit(1);
  }
  LOG_INFO("%s - Started", _argv[0]);
#endif

  if (global_tls_ca_init() != SUCCESS) {
    LOG_INFO("global_tls_ca_init");
    exit(1);
  }

  Optimizer Opti;
  if (optimizer_init(&Opti) != SUCCESS) {
    LOG_INFO("optimizer_init");
    exit(1);
  }

  time_t now = time(NULL);
  time_t next_run = ((now / 900) + 1) * 900;

  /* Start Unix socket server */
  int trigger_fd = -1;
  if (uds_server_start(SOCKET_PATH, &trigger_fd) != SUCCESS) {
    LOG_INFO("uds_server_start failed");
    exit(1);
  }

  /* Process handler, handle signals */
  signal_handlers_install(sigc, Signals);
  while (1) {
    now = time(NULL);

    if (sig_exit) {
      LOG_INFO("%s - Shutting down", _argv[0]);
      // printf("%s - Shutdown...\n", _argv[0]);
      optimizer_dispose(&Opti);
      global_tls_ca_dispose();
      log_close();
      exit(SUCCESS);
    } else if (sig_ignore) {
      // Do absolutely nothing
      // But perhaps log the event if debug is set
      // (should be done in handler though so exact sig can be logged)
      sig_ignore = 0;
    } else if (sig_new_data) {
      LOG_INFO("%s - Get new cache and calculations...\n", _argv[0]);
      optimizer_run(&Opti);
      sig_new_data = 0;
    } else if (sig_update_config) {
      printf("%s - Update config...\n", _argv[0]);
      optimizer_config_set(&Opti.config);
      sig_update_config = 0;
    } else if (now >= next_run) {
      LOG_INFO("%s - Get new cache and calculations...\n", _argv[0]);
      optimizer_run(&Opti);
      sig_new_data = 0;
      next_run += 900;
    }

    /* Poll socket (non‑blocking) */
    if (trigger_fd >= 0) {
      if (uds_server_step(trigger_fd, &Opti) != SUCCESS) {
        LOG_INFO("uds_server_step failed; closing socket");
        close(trigger_fd);
        trigger_fd = -1;
      }
    }
    nanosleep(&req, NULL);
  }

  return SUCCESS;
}
