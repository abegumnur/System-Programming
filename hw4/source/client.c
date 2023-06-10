#include "client.h"

int main(int argc, char** argv){
    
    setbuf(stdout, NULL);

    if(argc != 3){
        fprintf(stderr, "Usage: %s <Connect/tryConnect> ServerPID\n", argv[0]);
        exit(EXIT_FAILURE);        
    }

    //setting the signal handler for SIGINT
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = signalHandler;
    sigaction(SIGINT, &sa, NULL);

    if(strcmp(argv[1], "tryConnect") != 0 && strcmp(argv[1], "Connect") != 0){
        fprintf(stderr, "You must connect to a server before sending a request.\n");
        fprintf(stderr, "Terminating the program...\n");
        exit(EXIT_FAILURE);
    }


    //create and open the client fifo for reading
    //open the server fifo for writing the request
    char server_fifo[SERVER_FIFO_NAME_LENGTH];
    int client_fd, server_fd;

    int client_pid = getpid();
    int server_pid = atoi(argv[2]);

    snprintf(server_fifo, sizeof(server_fifo), SERVER_FIFO_NAME, server_pid);

    if((server_fd = open(server_fifo, O_WRONLY)) < 0){
        perror("server: open");
        exit(EXIT_FAILURE);
    }


    if(signal_flag == 1){
        fprintf(stdout, "SIGINT signal received, terminating client..\n");
        close(server_fd);
        unlink(server_fifo);
        exit(EXIT_FAILURE);
    }


    struct request req;
    struct response resp;

    req.pid = getpid();
    strncpy(req.command, argv[1], MAX_COMMAND);

    //opening the client fifo for read and write
    client_fd = openFifo();

    //writing the Connect/tryConnect request to server fifo
    ssize_t bytes_written = write(server_fd, &req, sizeof(struct request));
    
    if(bytes_written != sizeof(struct request)){
        perror("cannot write to server");
        close(server_fd);
        unlink(server_fifo);
        exit(EXIT_FAILURE);
    }


    if(read(client_fd, &resp, sizeof(struct response)) != sizeof(struct response)){
        perror("cannot read response from server");
        close(client_fd);
        close(server_fd);
        exit(EXIT_FAILURE); 
    }

    if(signal_flag == 1){
        fprintf(stdout, "SIGINT signal received, terminating client..\n");
        close(server_fd);
        unlink(server_fifo);
        close(client_fd);
        unlink(client_fifo);
        exit(EXIT_FAILURE);
    }

    if(strcmp(req.command, "tryConnect") == 0 && resp.connected == -1){
        fprintf(stdout, "%s", resp.reply);
        close(client_fd);
        close(server_fd);
        unlink(client_fifo);
        unlink(server_fifo);
        exit(EXIT_FAILURE);
    }

    if(resp.connected == -2)
        fprintf(stdout, "%s", resp.reply);


    if(read(client_fd, &resp, sizeof(struct response)) != sizeof(struct response)){
        perror("cannot read response from server here");
        close(client_fd);
        close(server_fd);
        unlink(server_fifo);
        unlink(client_fifo);
        exit(EXIT_FAILURE); 
    }            

    if(resp.connected != 1){
        exit(EXIT_FAILURE);
    }

    int returnval;
    close(server_fd);

    fprintf(stdout, "%s", resp.reply);

    char thread_fifo[SERVER_FIFO_NAME_LENGTH];
    snprintf(thread_fifo, SERVER_FIFO_NAME_LENGTH, SERVER_FIFO_NAME, (int)getpid());

    int thread_fd;
    if((thread_fd = open(thread_fifo, O_WRONLY)) < 0){
        perror("client: open thread fifo");
        close(client_fd);
        unlink(client_fifo);
        exit(EXIT_FAILURE);        
    }

    if(signal_flag == 1){
        fprintf(stdout, "SIGINT signal received, terminating client..\n");
        close(thread_fd);
        close(client_fd);
        unlink(client_fifo);
        unlink(thread_fifo);
        exit(EXIT_FAILURE);
    }

    //if connection is established
    while(true){
        printf("Enter comment: ");
        //get the request and write it to thread fifo
        char input[MAX_COMMAND];
        fgets(input, sizeof(input), stdin);

        remove_newline(input);
        req.pid = client_pid;

        strncpy(req.command, input, sizeof(input));
        //if sigint received, request to quit 
        if(signal_flag == 1){    
            fprintf(stdout, "SIGINT signal received, terminating client..\n");
            strcpy(req.command, "quit");
            while(((returnval = write(thread_fd, &req, sizeof(struct request))) == -1) && errno == EINTR);
            if(returnval < 0){
                perror("error writing the request to client fifo");
                close(client_fd);
                exit(EXIT_FAILURE);
            }           
            close(thread_fd);
            close(client_fd);
            unlink(thread_fifo);
            unlink(client_fifo);        
            exit(EXIT_FAILURE);
        }

        while(((returnval = write(thread_fd, &req, sizeof(struct request))) == -1) && errno == EINTR);
        if(returnval < 0){
            perror("error writing the request to client fifo");
            close(client_fd);
            exit(EXIT_FAILURE);
        }           
        //while the response is not over
        int end_of_response = 0;
        while(!end_of_response){
            //read the response from client fifo and write it to stdout
            if(read(client_fd, &resp, sizeof(struct response)) != sizeof(struct response)){
                perror("error reading the response from the server");
                exit(EXIT_FAILURE);
            }
            end_of_response = resp.end;
            fprintf(stdout, "%s", resp.reply);
        }        

        if(resp.connected == -1){
            break;
        }

    }

    unlink(client_fifo);
    close(client_fd);
    close(thread_fd);
    unlink(thread_fifo);

    return 0;

}

void signalHandler(int signo, siginfo_t* si, void* s){

    if(signo == SIGINT){
        signal_flag = 1;
    }
}

void remove_newline(char *str) {
    int i = 0;
    while (str[i] != '\0') {
        if (str[i] == '\n') {
            str[i] = '\0';
            return;
        }
        i++;
    }
}

int openFifo(){

    umask(0);
    snprintf(client_fifo, sizeof(client_fifo), CLIENT_FIFO_NAME, (int)getpid());

    if(mkfifo(client_fifo, 0666) < 0 && errno != EEXIST){
        perror("client: mkfifo");
        exit(EXIT_FAILURE);
    }

    int client_fd;
    if((client_fd = open(client_fifo, O_RDWR)) < 0){
        perror("client: open");
        exit(EXIT_FAILURE);        
    }
    return client_fd;

}