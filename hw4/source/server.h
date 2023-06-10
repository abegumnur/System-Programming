#ifndef __SERVER_H
#define __SERVER_H
#include "utility.h"
#include <pthread.h>
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


/*  Utility functions */
int countWords(const char* str);
void changeDirectory(char *directory);
int createFifo();
bool file_exists(const char* filename);
int convertToInt(const char* str);
void deleteWords(char* string, int numWords);
void signalHandler(int signal_no, siginfo_t *siginfo, void *fd);

/* Command functions    */
void readF(const char* fileName, int lineNo, int clientFifo);
void writeF(const char* fileName, char* lineNoStr, char* new_content);
void upload(const char* fileName, char* clientDirectory, int client_fd);
void download(char* fileName, char* clientDirectory, int client_fd);

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

/*  Running queue   */
typedef struct {
    struct request req[MAX_QUEUE];
    int front;
    int rear;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t condition_nonempty;
    pthread_cond_t condition_nonfull;
} RunningQueue;

/*  Running queue functions   */
void initializeQueue();
int isQueueEmpty();
int isQueueFull(int maxSize);
void enqueue(struct request request, int maxSize);
struct request dequeue(int maxSize);
void printqueue();

// Structure representing a request node
typedef struct Request {
    int number;
    pid_t pid;
    char command[BUFFER];               // Request data
    struct Request* next;   // Pointer to the next request
} Request;

// Structure representing the request queue
typedef struct RequestQueue {
    Request* front;         // Front of the queue
    Request* rear;          // Rear of the queue
} RequestQueue;

/*  Request queue functions */
void initializeReq(RequestQueue* queue);
void enqueueReq(RequestQueue* queue, int requestPID, int requestNo, char requestCommand[]);
int dequeueReq(RequestQueue* queue, pid_t* pid, int* no, char* commnd);
bool isEmptyReq(RequestQueue* queue);
void printQueueReq(RequestQueue* queue);

#endif