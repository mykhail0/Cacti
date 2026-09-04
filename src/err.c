#include "err.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int sys_nerr;

void syserr() {
  fprintf(stderr, "ERROR: (%d; %s)\n", errno, strerror(errno));
  exit(EXIT_FAILURE);
}

void exit_with_msg(char const* msg) {
  fprintf(stderr, msg);
  exit(EXIT_FAILURE);
}
