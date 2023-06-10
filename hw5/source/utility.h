#ifndef UTILITY_H
#define UTILITY_H

#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <signal.h>


#define MAX_FILES 100
#define FILE_NAME_LENGTH 200
#define MAX_BUFFER 300
#define BUFFER_SIZE 516
#define MAX_EXTENSION 12

/* signal flag for SIGINT */
extern volatile sig_atomic_t sigint_flag;// = 0;

/* enforces the synchronization of writes to stdout */
extern pthread_mutex_t stdout_mutex;// = PTHREAD_MUTEX_INITIALIZER;

/* flag is set by the producer when it is done with */
/* writing to the buffer    */
extern volatile int producer_flag;// = 0;

/* keep track of file statistics for later printing */
extern atomic_int total_bytes_copied;// = 0;
extern atomic_int total_files;// = 0;   
extern atomic_int file_type_count;// = 0;

/*  buffer size given as a command line argument */
extern int buffer_size;
/*  pool size given as a command line argument  */
extern int pool_size;

/*  thread arguments for producer   */
typedef struct{
    char sourceDir[MAX_BUFFER];
    char destinationDir[MAX_BUFFER];

} threadArgs;


/*  file type statistics (extension type and its count) */
typedef struct{
    char extension[MAX_EXTENSION];
    int count;
} FileType;

/*  support maximum 100 different file types    */
extern FileType fileTypes[MAX_FILES];

/* functions realted to fileType   */
void initializeFileTypes();

/// @brief update the fileTypes according to the given extension
/// if the extension is already in the fileTypes, find it and increment the count
/// if not found add it to the end and increment the count by one
/// @param extension file type after the last .(dot). ex: zip, pdf etc
void updateFileInfo(char* extension);

/// @brief print the file statistics and infos after the copying is done
void printFileStatistics();

/*  a single buffer entry struct    */
typedef struct{
    int source_fd;
    int destination_fd;
    char fileName[FILE_NAME_LENGTH];
} bufferEntry;

/*  buffer is a circular array  */
typedef struct{
    bufferEntry* entry;
    int size;
    int front;
    int rear;
    /* mutex and cond variables for */
    /* the control of synchronization*/
    pthread_mutex_t mutex;
    pthread_cond_t empty;
    pthread_cond_t full;
} Buffer;

/* global buffer used by the producer thread and consumer threads   */
extern Buffer buffer;

/* functions related to buffer  */

/// @brief initialize buffer entries, mutex and cond variables
/// according to the provided buffer size
void initializeBuffer();

/// @brief check if the buffer is empty, used while dequeueing the buffer
/// @return true if empty, false if not
bool isBufferEmpty();

/// @brief check if the buffer is full, used while enqueueing 
/// @return true if full, false if not
bool isBufferFull();

/// @brief add a new element to the buffer
/// @param theEntry is the element to add to the end of the buffer
void enqueue(bufferEntry* theEntry);

/// @brief delete the first element of the buffer 
/// @return entry at the front of the buffer
bufferEntry dequeue();

/// @brief free the allocated space for the buffer
/// also destroy the mutex and cond variables initialized
void freeBuffer();

/// @brief change the directory to the destination dir, and store the old directory
/// @param newDir destination directory
/// @param oldDir source directory 
void changeDirectory(char* newDir, char* oldDir);


#endif