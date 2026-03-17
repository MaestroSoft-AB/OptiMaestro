#ifndef __DAEMON_H__
#define __DAEMON_H__

#include <maestroutils/error.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <stdlib.h>
#include <stdio.h>

static int daemonize(const char* _prog_name) {
  pid_t pid;

  umask(0);

  /* Fork and exit parent */
  pid = fork();
  if (pid < 0) {
    fprintf(stderr, "%s - First fork failed\n", _prog_name); 
    exit(1);
  }
  if (pid > 0) exit(SUCCESS);

  /* Create new session */
  if (setsid() < 0) {
    fprintf(stderr, "%s - setsid failed\n", _prog_name);
    exit(1);
  }

  /* Double fork to ensure running */
  pid = fork();
  if (pid < 0) {
    fprintf(stderr, "%s - Second fork failed\n", _prog_name);
    exit(1);
  }
  if (pid > 0) exit(SUCCESS);

  chdir("/");
  
  /* Close std fds */
  for (int i = 0; i < 3; i++) close(i);
  
  /* Redirect to /dev/null */
  int fd = open("/dev/null", O_RDWR);
  if (fd != 0) exit(1);
  dup2(0, 1); dup2(0, 2); close(fd);

  /* PID file */
  FILE *pidfile = fopen("/run/optimizer.pid", "w");
  if (pidfile) {
    fprintf(pidfile, "%d\n", getpid());
    fclose(pidfile);
  }

  openlog("optimizer", LOG_PID | LOG_CONS, LOG_DAEMON);
  return 0;
}

#endif
