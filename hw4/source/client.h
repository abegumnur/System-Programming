#ifndef __CLIENT_H
#define __CLIENT_H
#include "utility.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/time.h>
#include <dirent.h>
#include <ctype.h>

char client_fifo[CLIENT_FIFO_NAME_LENGTH];  /*client fifo string*/

volatile int signal_flag = 0;   /*sigint singal flag*/

//remove the newline at the end from a given string
void remove_newline(char *str);

//signal handler for SIGINT that sets the signal_flag to 1
void signalHandler(int signo, siginfo_t* si, void* s);

//opens the client fifo and returns the file descriptor
int openFifo();



#endif