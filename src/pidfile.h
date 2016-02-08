#ifndef STATSRELAY_PIDFILE_H
#define STATSRELAY_PIDFILE_H

#include "log.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <errno.h>

 /**
   * Read the pid from pidfile specified
   * returns 0, iff pidfile doesn't exist or its empty
   * pid, or else
   */
int read_pid(char *pidfile);

/**
  * Remove the pidfile specified
  * returns 0, on success
  * returns -1, on error
  */
int remove_pid(char *pidfile);

 /**
   * Writes the specified pid to pidfile
   * returns pid, if write was successful
   * returns 0,  on failure
   */
int write_pid(char *pidfile, pid_t old_pid);

#endif // STATSRELAY_PIDFILE_H
