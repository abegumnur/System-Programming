#ifndef __SERVER_H
#define __SERVER_H

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
#include <time.h>
#include "utility.h"

/* signal flags*/
volatile int sigint_flag = -1;  
volatile int sigusr1_flag = -1; 

char server_fifo[SERVER_FIFO_NAME_LENGTH];  /* name of the server fifo*/

int avaliable_server_total = 0; /*number of avaliable servers at the moment*/
int max_client = 0; /* maximum number of clients that can be served at the same time*/

struct child_server* child_servers; 

sem_t* semap;   
sem_t* serverSem;

int total_client = 0;   /*total clients handled*/

char clientDirectory[MAX_INP];  /*directory of the client process*/

/* commends avaliable*/
char* options[8] = {
    "help", 
    "list",
    "readF",
    "writeT",
    "upload",
    "download",
    "quit",
    "killServer"
};

//checks if the file exists and returns true/false accordingly
bool file_exists(const char* filename);

//creates the parent server fifo and returns its file descriptor 
int createFifo();

//change the current working directory according to the string directory
void changeDirectory(char *directory);

//converts the given string to an integer, -1 on fail
int convertToInt(const char* str);

//delete the first given number of words from the string
void deleteWords(char* string, int numWords);

//terminate all child processes
void terminateChildren();

/// @brief requests to display the # line of the <file>
/// @param fileName file to open for reading
/// @param lineNo   the line value to read, if -1 read the whole file
/// @param clientFifo file descriptor of the client, since the read values will
/// need to be sent through the fifo to be printed on the clients screen
void readF(const char* fileName, int lineNo, int clientFifo);


/// @brief request to write the  content of “string” to the  #th  line the <file>
/// @param fileName the file to open for writing, if file does not exist create it
/// @param lineNoStr string that containts the line number to write, if NULL write to end
/// @param new_content new content to write 
void writeF(const char* fileName, char* lineNoStr, char* new_content);


void upload(const char* fileName, char* clientDirectory, int child_index);

void download(char* fileName, char* clientDirectory, int child_index);


#endif