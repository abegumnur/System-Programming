#ifndef __SHELL_H
#define __SHELL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>

#define MAX_ARGS 50 
#define MAX_COMMAND 20 /* number of maximum commands in a single line */
#define MAX_BUFFER 256 /* max buffer size */
#define TOKEN_SEP " \t\n"

volatile int RECEIVED_SIGNAL = 0; /* signal flag */


#endif
