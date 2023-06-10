#ifndef __UTILITY_H
#define __UTILITY_H
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>

/*templates and lengths for server and client fifos*/
#define CLIENT_FIFO_NAME "/tmp/client_fifo%d"
#define CLIENT_FIFO_NAME_LENGTH (sizeof(CLIENT_FIFO_NAME) + 20)
#define SERVER_FIFO_NAME "/tmp/server_fifo%d"
#define SERVER_FIFO_NAME_LENGTH (sizeof(SERVER_FIFO_NAME) + 20)

#define MAX_COMMAND 256
#define MAX_QUEUE 50
#define MAX_RESPONSE 500
#define MAX_INP 128
#define BUFFER 50

struct request{
    int number;
    pid_t pid;
    char command[BUFFER];
};

struct request_queue{
    struct request request_list[MAX_QUEUE];
    int head;
    int tail;
    int full;
    int empty;
    int size;
};

struct response{
    int connected; 
    char reply[MAX_RESPONSE];
    int child_server_pid;
    int end; //indicates the end of the response for a single comment
};

struct child_server{
    int client_id;
    int client_no;
    pid_t pid;
    bool avaliable;
    int pipe_read_fd;
    int pipe_write_fd;
    int fifo_write_fd;
};


#endif