#include "client.h"

int main(int argc, char* argv[]){
    
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
    char server_fifo[SERVER_FIFO_NAME_LENGTH], child_server_fifo[SERVER_FIFO_NAME_LENGTH];
    int child_server_pid;
    int client_fd, server_fd, child_server_fd;
    int server_pid = atoi(argv[2]);

    snprintf(server_fifo, sizeof(server_fifo), SERVER_FIFO_NAME, server_pid);

    if((server_fd = open(server_fifo, O_WRONLY)) < 0){
        perror("server: open");
        exit(EXIT_FAILURE);
    }

    struct request req;
    struct response resp;
    
    req.pid = getpid();
    strncpy(req.command, argv[1], MAX_COMMAND);

    ssize_t bytes_written = write(server_fd, &req, sizeof(struct request));
    
    if(bytes_written != sizeof(struct request)){
        perror("cannot write to server");
        exit(EXIT_FAILURE);
    }

    client_fd = openFifo();

    if(read(client_fd, &resp, sizeof(struct response)) != sizeof(struct response)){
        perror("cannot read response from server");
        close(client_fd);
        close(server_fd);
        exit(EXIT_FAILURE); 
    }

    if(strcmp(req.command, "tryConnect") == 0 && resp.connected == -1){
        fprintf(stdout, "%s", resp.reply);
        close(client_fd);
        close(server_fd);
        exit(EXIT_SUCCESS);
    }

    if(resp.connected == -2){
        fprintf(stdout, "%s", resp.reply);
        if(read(client_fd, &resp, sizeof(struct response)) != sizeof(struct response)){
            perror("cannot read response from server");
            close(client_fd);
            close(server_fd);
            exit(EXIT_FAILURE); 
        }        

    }

    int returnval;
    if(resp.connected){
            close(server_fd);
            child_server_pid = resp.child_server_pid;    
            fprintf(stdout, "%s", resp.reply);

            snprintf(child_server_fifo, SERVER_FIFO_NAME_LENGTH, SERVER_FIFO_NAME, child_server_pid);

            child_server_fd = open(child_server_fifo, O_WRONLY);
            if (child_server_fd == -1){
                perror("child server open");
                close(client_fd);
                exit(EXIT_FAILURE);
            }
            //exit only when the client disconnects
            while(true){
                printf("Enter comment:");
                char input[MAX_COMMAND];
                fgets(input, sizeof(input), stdin);

                remove_newline(input);
                req.pid = getpid();
                strncpy(req.command, input, sizeof(input));

                if(signal_flag){    
                    strcpy(req.command, "SIGINT received");
                    exit(1);
                }
                
                while(((returnval = write(child_server_fd, &req, sizeof(struct request))) == -1) && errno == EINTR);
                if(returnval < 0){
                    perror("error writing the request to child server");
                    close(child_server_fd);
                    close(client_fd);
                    exit(EXIT_FAILURE);
                }           

                int end_of_response = 0;
                while(!end_of_response){
                    if(read(client_fd, &resp, sizeof(struct response)) != sizeof(struct response)){
                        perror("error reading the response from the server");
                        exit(1);
                    }
                    end_of_response = resp.end;
                    fprintf(stdout, "%s", resp.reply);
                }

                if(resp.connected == -1){
                    break;
                }

            }

    }


    unlink(client_fifo);
    close(client_fd);
    close(child_server_fd);

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
