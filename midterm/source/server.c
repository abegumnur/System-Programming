#include "server.h"

void signalHandler(int signal_no, siginfo_t *siginfo, void *fd)
{

    if (signal_no == SIGINT)
        sigint_flag = 1;

}

void sigusr1Handler(int signal_no, siginfo_t *si, void *fd){
    
    for(int i = 0; i < max_client; ++i)
        if(child_servers[i].pid == si->si_pid && child_servers[i].avaliable == false)
        {
            child_servers[i].avaliable = true;
            avaliable_server_total++;
        }

}

void sigint_print(int client_no){
    printf("kill signal from client%d, terminating...\n", client_no);
}

void terminateChildren()
{
    for(int i = 0; i < max_client; ++i)
        kill(child_servers[i].pid, SIGINT);

    for(int i = 0; i < max_client; ++i)
        waitpid(-1, NULL, 0);

}


// Function to check if the request queue is empty
bool isRequestQueueEmpty(struct request_queue requestQueue) {
    if(requestQueue.head == -1) return true;
    return false;
}

// Function to check if the request queue is full
bool isRequestQueueFull(struct request_queue requestQueue) {
  if ((requestQueue.head == requestQueue.tail + 1) || (requestQueue.head == 0 && requestQueue.tail == MAX_QUEUE - 1)) return true;
    return false;
}


void enQueue(struct request_queue* queue, struct request req) {
    if (queue->head == -1) {
        queue->head = 0;
        queue->tail = 0;
    } else {
        queue->tail = (queue->tail + 1) % MAX_QUEUE;
        if (queue->tail == queue->head) {
            queue->head = (queue->head + 1) % MAX_QUEUE;
        }
    }

    queue->request_list[queue->tail] = req;
    queue->empty = 0;
    queue->size++;
    if (queue->size == MAX_QUEUE) {
        queue->full = 1;
    }
}

struct request deQueue(struct request_queue* queue) {
    struct request req;

    if (isRequestQueueEmpty(*queue)) {
        req.number = -1; // Indicating the request queue is empty
    } else {
        req = queue->request_list[queue->head];
        if (queue->head == queue->tail) {
            queue->head = -1;
            queue->tail = -1;
        } else {
            queue->head = (queue->head + 1) % MAX_QUEUE;
        }
        queue->size--;
        if (queue->size == 0) {
            queue->empty = 1;
        }
        queue->full = 0;
    }
    return req;
}

int main(int argc, char *argv[]){

    //Checking if there is any other instance of this server already
    while(((serverSem = sem_open("/serverSemap", O_CREAT, 0644, 1)) == NULL) && errno == EINTR);
    if(serverSem == SEM_FAILED){
        perror("Cannot open semaphore");
        exit(EXIT_FAILURE);
    }

    int semVal;
    int returnVal;
    while(((returnVal = sem_getvalue(serverSem, &semVal)) == -1) && errno == EINTR);
    if(returnVal == -1)
    {
        perror("Cannot receive the semaphore value");
        sem_close(serverSem);
        exit(EXIT_FAILURE);
    }
    //if there is already an active server print error and exit
    if(semVal == 0)
    {
        char errorMessage[] = "There is already an active server present. Cannot initiate a second one.\n";
        while((write(STDERR_FILENO, errorMessage, sizeof(errorMessage)-1) == -1) && (errno == EINTR));
        sem_close(serverSem);
        exit(EXIT_FAILURE);
    }

    /* Initializing the signal handler for SIGINT */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = signalHandler;
    sigaction(SIGINT, &sa, NULL);

    /*Stop buffering*/
    setbuf(stdout, NULL);
    
    if (getcwd(clientDirectory, sizeof(clientDirectory)) == NULL){
        perror("Error changing current working directory");
        exit(EXIT_FAILURE);
    }

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <dirname> <max. #ofClients>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /*Changing the directory to the second argument*/
    int size = strlen(argv[1]) + 1;
    char *directory = (char *)malloc(size * sizeof(char));
    strcpy(directory, argv[1]);
    changeDirectory(directory);
    free(directory);

    char newdir[MAX_INP];
    if(getcwd(newdir, sizeof(newdir)) == NULL){
        perror("Error changing directory");
        exit(EXIT_FAILURE);
    }

    //get the maximum number of clients
    max_client = atoi(argv[2]);

    /*Creating the server fifo*/
    int server_fd = createFifo();

    /*Allocating memory for the child servers*/
    child_servers = (struct child_server*)malloc(sizeof(struct child_server) * max_client);
    memset(child_servers, 0, max_client*sizeof(struct child_server));

    /*Setting up the signal handler for SIGUSR1*/
    struct sigaction sa2;
    memset(&sa2, 0, sizeof(sa2));
    sa2.sa_flags = SA_SIGINFO;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_sigaction = sigusr1Handler;

    int returnval;
    while(((returnval = sigaction(SIGUSR1, &sa2, NULL)) == -1) && errno == EINTR);
    if(returnval == -1)
    {
        fprintf(stderr, "Cannot assign signal handler for SIGUSR1.\n");
        close(server_fd);
        unlink(server_fifo);
        exit(EXIT_FAILURE);
    }

    /*print the start message   */
    fprintf(stdout, "Server started PID %d.\n", getpid());
    fprintf(stdout, "Waiting for clients...\n");

    //creating the pipes for the communication
    //between the parent and child servers
    int fds[max_client][2];

    for(int i = 0; i < max_client; i++){
        if(pipe(fds[i]) == -1){
            perror("error: pipe create");
            free(child_servers);
            exit(1);
        }
        child_servers[i].avaliable = true;
        child_servers[i].pipe_read_fd = fds[i][0];
        child_servers[i].pipe_write_fd = fds[i][1];
    }

    /*Creating the semaphore to enforce mutual exclusion*/
    while(((semap = sem_open("/semaphore", O_CREAT, 0644, 1)) == NULL) && errno == EINTR);
    if(semap == SEM_FAILED)
    {
        perror("Cannot open semaphore");
        for(int i = 0; i < max_client; i++){
            close(fds[i][0]);
            close(fds[i][1]);
        }
        free(child_servers);
        close(server_fd); 
        unlink(server_fifo);
        exit(EXIT_FAILURE);
    }

    /*Open the log file*/
    char logFileName[MAX_INP];
    snprintf(logFileName, MAX_INP, "log%d", getpid());
    int logFile = open(logFileName, O_WRONLY | O_CREAT | O_APPEND, 0777);
        if(logFile == -1){
            perror("Error opening log file");
            for(int i = 0; i < max_client; i++){
                close(fds[i][0]);
                close(fds[i][1]);
            }            
            close(server_fd);
            unlink(server_fifo);
            free(child_servers);
            sem_close(semap);
            unlink("/semaphore");
            exit(EXIT_FAILURE);            
        }

    struct flock lock;

    /*Forking the child servers*/
    for(int i = 0; i < max_client; i++){

        int fork_pid = fork();
        /*fork error*/
        if(fork_pid == -1){
            perror("error: fork");
            for(int i = 0; i < max_client; i++){
                close(fds[i][0]);
                close(fds[i][1]);
            }
            free(child_servers);
            close(server_fd);
            sem_close(semap);
            unlink("/semaphore");
            exit(EXIT_FAILURE);
        }
        /*child process */
        else if(fork_pid == 0){
            /*close the write end of the pipe*/
            close(fds[i][1]);
            child_servers[i].pid = getpid();
            avaliable_server_total++;
            close(server_fd);

            int index = i;
            bool disconnected = false;
            //this loop is for the child server to 
            //keep getting new clients and serving them

            if(sigint_flag == 1){
                close(fds[i][0]);
                exit(EXIT_FAILURE);
            }

            while(true){
                int bytes_read;
                struct request req;
                /* Read the first request "Connect/tryConnect" from client through the server pipe*/
                while((bytes_read = read(child_servers[index].pipe_read_fd, &req, sizeof(struct request))) == -1 && errno == EINTR);
                if(bytes_read != sizeof(struct request)){
                    perror("child server cannot read from pipe");
                    exit(EXIT_FAILURE);
                }

                if(sigint_flag == 1){
                    avaliable_server_total--;
                    printf("Child server with PID %d received SIGINT. Terminating...", getpid());
                    exit(EXIT_FAILURE);
                }

                /*Assign the client pid and number*/
                child_servers[index].client_id = req.pid;
                child_servers[index].client_no = req.number;

                char child_server_fifo[SERVER_FIFO_NAME_LENGTH];

                //creating the child server fifo for reading the rest of the requests
                umask(0); /* So we get the permissions we want */
                snprintf(child_server_fifo, SERVER_FIFO_NAME_LENGTH, SERVER_FIFO_NAME, (int)getpid());
                if(mkfifo(child_server_fifo, S_IRUSR | S_IWUSR | S_IWGRP) == -1 && errno != EEXIST){
                    perror("child server connot create fifo");
                    exit(EXIT_FAILURE);
                }

                int child_server_fd = open(child_server_fifo, O_RDWR);
                if (child_server_fd == -1){
                    perror("server open");
                    exit(EXIT_FAILURE);
                }

                if(sigint_flag == 1){
                    avaliable_server_total--;
                    printf("Child server with PID %d received SIGINT. Terminating...", getpid());
                    close(child_server_fd);
                    exit(EXIT_FAILURE);                    
                }

                /* The first response is "connection established" is sent to client*/
                struct response connected;
                connected.child_server_pid = getpid();
                strcpy(connected.reply, "Connection established.\n");
                connected.connected = 1;
                //opening the client fifo for writing the response
                
                //printf("%d\n",connected.child_server_pid);

                char client_fifo[CLIENT_FIFO_NAME_LENGTH];
                snprintf(client_fifo, sizeof(client_fifo), CLIENT_FIFO_NAME, req.pid);
                int client_fd = open(client_fifo, O_WRONLY);
                if(client_fd < 0 ){
                    perror("Error opening client fifo");
                    free(child_servers);
                    exit(EXIT_FAILURE);
                }

                child_servers[index].fifo_write_fd = client_fd;
                while(((returnval = write(child_servers[index].fifo_write_fd, &connected, sizeof(struct response))) == -1) && errno == EINTR);
                if(returnval < 0){
                    perror("Error writing to client fifo");
                    free(child_servers);
                    exit(EXIT_FAILURE);
                }                                    

                if(sigint_flag == 1){
                    avaliable_server_total--;
                    printf("Child server with PID %d received SIGINT. Terminating...", getpid());
                    close(child_server_fd);
                    exit(EXIT_FAILURE);                    
                }

                fprintf(stdout, "Client PID %d connected as \"client%d\" \n",child_servers[index].client_id, child_servers[index].client_no);

                /*Loop untill that client disconnects*/
                while(!disconnected){

                    while((returnval = read(child_server_fd, &req, sizeof(struct request))) == -1 && errno == EINTR);
                    if(returnval == -1){
                        perror("Reading from child server fifo");
                        free(child_servers);
                        close(child_server_fd);
                        exit(EXIT_FAILURE);
                    }

                    struct response resp;
                    resp.connected = 1;
                    resp.end = 0;

                    /* While the response to the request is not done    */
                    /* Keep on writing it to the fifo   */
                    while(!resp.end){

                        memset(&resp, 0, sizeof(resp));  // Set all bytes to 0
                        /* Handles "help" command   */
                        if(strcmp(req.command, options[0]) == 0){
                            strcpy(resp.reply, "Avaliable comments are:\nhelp, list, readF, writeT, upload, download, quit, killServer\n");
                            resp.end = 1;
                            while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                            if(returnval < 0){
                                perror("Error writing to client fifo");
                                //other handlings of fifos and pipes
                                exit(EXIT_FAILURE);
                            }
                        } 

                        else if(strcmp(req.command, options[6]) == 0){
                            //send write request to log file lock and unlock
                            //Sending write request to server log filewaiting for logfile ...
                            strcpy(resp.reply, "Sending write request to server log file\nwaiting for log file\n");
                            resp.end = 0;
                            while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                            if(returnval < 0){
                                perror("Error writing to client fifo");
                                exit(EXIT_FAILURE);
                            }

                            lock.l_type = F_WRLCK;
                            while(((fcntl(logFile, F_SETLKW, &lock)) == -1) && errno == EINTR);                            

                            strcpy(resp.reply, "Logfile write request granted\n");
                            resp.end = 0;
                            while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                            if(returnval < 0){
                                perror("Error writing to client fifo");
                                close(logFile);
                                exit(EXIT_FAILURE);
                            }

                            time_t rawTime;
                            struct tm* timeInfo;
                            char timestamp[20];

                            time(&rawTime);
                            timeInfo = localtime(&rawTime);
                            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeInfo);
                            char logmessage[MAX_INP];
                            snprintf(logmessage, MAX_INP, "Client PID#%d handled by the child server PID#%d.\n", child_servers[index].client_id, child_servers[index].pid); 
                            char output[MAX_COMMAND];
                            snprintf(output, MAX_COMMAND, "[%s] %s", timestamp, logmessage);
                            int byteswritten;
                            while(((byteswritten = write(logFile, output, strlen(output))) == -1) && (errno == EINTR));
                            if(byteswritten == -1)
                            {
                                lock.l_type = F_UNLCK;
                                while(((fcntl(logFile, F_SETLKW, &lock)) == -1) && errno == EINTR);
                                perror("Log write failed.");
                                close(logFile);
                                close(client_fd);
                                close(child_server_fd);
                                exit(EXIT_FAILURE);
                            }

                            lock.l_type = F_UNLCK;
                            while(((fcntl(logFile, F_SETLKW, &lock)) == -1) && errno == EINTR);

                            strcpy(resp.reply, "Byeee\n");
                            resp.end = 1;
                            resp.connected = -1;
                            while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                            if(returnval < 0){
                                perror("Error writing to client fifo");
                                close(logFile);
                                exit(EXIT_FAILURE);
                            }                            
                            //quit the client
                            close(client_fd);
                            disconnected = true;
                            fprintf(stdout, "client%d disconnected...\n", child_servers[index].client_no);

                            /* Signaling that the child process is free now*/
                            kill(getppid(), SIGUSR1);

                        }

                        else if(strcmp(req.command, options[7]) == 0){

                            //handle the termination gracefully
                            strcpy(resp.reply, "Sending a kill request to the server.\n");
                            resp.end = 1;         
                            while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);

                            if(returnval < 0){
                                perror("Error writing to client fifo");
                                //other handlings of fifos and pipes
                                exit(EXIT_FAILURE);
                            }              
                            /*Raise the kill signal to parent*/
                            kill(getppid(), SIGKILL);
                            disconnected = true;                   
                        }
                        //list command
                        else if(strcmp(req.command, options[1]) == 0){   
                            //get the semaphore                         
                            while(((returnval = sem_wait(semap)) == -1) && errno == EINTR);
                            if(returnval == -1){
                                perror("Cannot wait on semaphore");
                                sem_close(semap);
                                exit(EXIT_FAILURE);
                            }

                            struct dirent *entry;
                            DIR *dir;

                            dir = opendir(".");
                            while ((entry = readdir(dir)) != NULL) {
                                strcat(resp.reply, entry->d_name);
                                strcat(resp.reply, "\n");
                            }

                            resp.reply[127] = '\0';   
                            //release the semaphore
                            while((returnval = sem_post(semap)) == -1 && errno == EINTR);
                            if(returnval == -1){
                                perror("Cannot post semaphore");
                                sem_close(semap);
                                exit(EXIT_FAILURE);                                
                            }

                            closedir(dir);
                            resp.end = 1;
                            while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                            if(returnval < 0){
                                perror("Error writing to client fifo");
                                sem_close(semap);
                                exit(EXIT_FAILURE);
                            }
                        }   


                        else{   //other possible comments
                            char cmnd[4][BUFFER];
                            char* commanddup = strdup(req.command);
        
                            char* token = strtok(req.command, " ");
                            int i = 0;
                            while(token != NULL && i < 4){
                                strcpy(cmnd[i], token);
                                i++;
                                token = strtok(NULL, " ");
                            }
                            //help $
                            if(strcmp(cmnd[0], options[0]) == 0){
                                
                                free(commanddup);

                                if (strcmp(cmnd[1], options[1]) == 0){
                                    strcpy(resp.reply, "list\nsends a request to display the list of files in Servers directory.\n");
                                    resp.end = 1;                                
                                    while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                    if(returnval < 0){
                                        perror("Error writing to client fifo");
                                        exit(EXIT_FAILURE);
                                    }

                                }
                                
                                else if(strcmp(cmnd[1], options[2]) == 0){
                                    strcpy(resp.reply, "readF <file> <line#>\ndisplay the #th line of the <file>, returns with an error if <file> does not exists.\n");
                                    resp.end = 1;
                                    while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                    if(returnval < 0){
                                        perror("Error writing to client fifo");
                                        exit(EXIT_FAILURE);
                                    }

                                }
                                else if(strcmp(cmnd[1], options[3]) == 0){
                                    resp.end = 0;
                                    strcpy(resp.reply, "writeT <file> <line #> <string>\nwrite the  content of “string” to the  #th  line the <file>.\n" );
                                    while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                    if(returnval < 0){
                                        perror("Error writing to client fifo");
                                        exit(EXIT_FAILURE);
                                    }                            

                                    strcpy(resp.reply, "If the file does not exists in Servers directory creates and edits the file at the same time.\n");
                                    resp.end = 1;
                                    while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                    if(returnval < 0){
                                        perror("Error writing to client fifo");
                                        exit(EXIT_FAILURE);
                                    }                                           

                                }
                                else if(strcmp(cmnd[1], options[4]) == 0){
                                    resp.end = 1;                                    
                                    strcpy(resp.reply, "upload <file>\nuploads the file from the current working directory of client to the Servers directory.\n" );
                                    while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                    if(returnval < 0){
                                        perror("Error writing to client fifo");
                                        exit(EXIT_FAILURE);
                                    }                                   


                                }

                                else if(strcmp(cmnd[1], options[5]) == 0){
                                    resp.end = 1;        
                                    strcpy(resp.reply, "download <file>\nreceive <file> from Servers directory to client side.\n" );
                                    while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                    if(returnval < 0){
                                        perror("Error writing to client fifo");
                                        exit(EXIT_FAILURE);
                                    }                                   


                                }
                                else if(strcmp(cmnd[1], options[6]) == 0){
                                    resp.end = 1;        
                                    strcpy(resp.reply, "quit\nSend write request to Server side log file and quits.\n" );
                                    while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                    if(returnval < 0){
                                        perror("Error writing to client fifo");
                                        exit(EXIT_FAILURE);
                                    }                                   

                                }

                                else if(strcmp(cmnd[1], options[7]) == 0){
                                    resp.end = 1;                                            
                                    strcpy(resp.reply, "killServer\nSends a kill request to the Server.\n" );
                                    while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                    if(returnval < 0){
                                        perror("Error writing to client fifo");
                                        exit(EXIT_FAILURE);
                                    }                                   

                                }
                            }

                            //if the command is readF
                            if(strcmp(cmnd[0], options[2]) == 0){
                        
                                free(commanddup);
                                //client is trying to access the log file
                                if(strcmp(cmnd[1], logFileName) == 0){
                                resp.end = 1;   
                                strcpy(resp.reply, "Client cannot access the log file.\n" );
                                while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                if(returnval < 0){
                                    perror("Error writing to client fifo");
                                    exit(EXIT_FAILURE);
                                    }        
                                    continue;
                                }
                                else{
                                    //get the semaphore
                                    while(((returnval = sem_wait(semap)) == -1) && errno == EINTR);
                                    if(returnval == -1){
                                        perror("Cannot wait on semaphore");
                                        sem_close(semap);
                                        exit(EXIT_FAILURE);
                                    } 

                                    if(i == 3){
                                        int line = atoi(cmnd[2]);
                                        readF(cmnd[1], line, child_servers[index].fifo_write_fd);
                                    }
                                    
                                    else    
                                        readF(cmnd[1], -1, child_servers[index].fifo_write_fd);

                                    resp.end = 1;   
                                    strcpy(resp.reply, "Reading the file is done.\n" );
                                    while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                    if(returnval < 0){
                                        perror("Error writing to client fifo");
                                        sem_post(semap);
                                        exit(EXIT_FAILURE);
                                    }                                      
                                    while((returnval = sem_post(semap)) == -1 && errno == EINTR);
                                    if(returnval == -1){
                                        perror("Cannot post semaphore");
                                        sem_close(semap);
                                        exit(EXIT_FAILURE);       
                                    }         
                                }

                            }

                            //if the command is writeT
                            if(strcmp(cmnd[0], options[3]) == 0){
                                resp.end = 0;
                                strcpy(resp.reply, "Sending write request to file.\n" );
                                while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                if(returnval < 0){
                                    perror("Error writing to client fifo");
                                    exit(EXIT_FAILURE);
                                }

                                int log = 1;
                                //second argument is not the line number append to the file
                                if(convertToInt(cmnd[2]) == -1){
                                    deleteWords(commanddup, 2);

                                //client is teying to write to log file
                                    if(strcmp(cmnd[1], logFileName) == 0){
                                        resp.end = 1;   
                                        strcpy(resp.reply, "Client cannot write to log file.\n" );
                                        while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                        if(returnval < 0){
                                            perror("Error writing to client fifo");
                                            exit(EXIT_FAILURE);
                                            }        
                                    }                                    
                                    else{
                                        writeF(cmnd[1], NULL, commanddup);
                                        log = 0;
                                    }
                                }
                                else{
                                    deleteWords(commanddup, 3);

                                    if(strcmp(cmnd[1], logFileName) == 0){
                                        resp.end = 1;   
                                        strcpy(resp.reply, "Client cannot write to log file.\n" );
                                        while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                        if(returnval < 0){
                                            perror("Error writing to client fifo");
                                            exit(EXIT_FAILURE);
                                            }        
                                    }     
                                    else{
                                        log = 0;
                                        writeF(cmnd[1], cmnd[2], commanddup);
                                    }                               
                                }

                                free(commanddup);
                                
                                if(!log){
                                    resp.end = 1;   
                                    strcpy(resp.reply, "Writing to the file is done.\n" );
                                    while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                    if(returnval < 0){
                                        perror("Error writing to client fifo");
                                        exit(EXIT_FAILURE);
                                    }       
                                }
                            }
                            //upload file command
                            if(strcmp(cmnd[0], options[4]) == 0){
                                free(commanddup);
                                upload(cmnd[1], clientDirectory, index);
                                resp.end = 1;
                            }
                            //download file command
                            if(strcmp(cmnd[0], options[5]) == 0){
                                free(commanddup);
                                if(strcmp(cmnd[1], logFileName) == 0){
                                    resp.end = 1;   
                                    strcpy(resp.reply, "Client cannot download the log file.\n");
                                    while(((returnval = write(child_servers[index].fifo_write_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                                    if(returnval < 0){
                                        perror("Error writing to client fifo");
                                        exit(EXIT_FAILURE);
                                    }                                    
                                }
                                else{
                                    download(cmnd[1], clientDirectory, index);
                                    resp.end = 1;
                                }

                            }


                        }

                        if(sigint_flag == 1){
                            fprintf(stdout, "Client %d send a sigint signal to server %d.\n",req.pid, child_servers[index].pid);
                            fprintf(stdout, "Terminating the child server...\n");
                            kill(getppid(), SIGINT);
                            close(child_servers[index].pipe_read_fd);
                            exit(EXIT_FAILURE);
                        }

                    }


                }

                
            }

        }

        //parent process
        else{
            child_servers[i].pid = fork_pid;

            if(close(fds[i][0]) == -1){
                perror("Closing the read end of pipe failed");
                for(int i = 0; i < max_client; i++){
                    close(fds[i][1]);
                }
                free(child_servers);
            }
            close(child_servers[i].pipe_read_fd);

        }

        
    }

    struct request_queue requestQueue;
    struct request serv_req;

    requestQueue.head = -1;
    requestQueue.tail = -1;
    requestQueue.empty = 1;
    requestQueue.full = 0;
    requestQueue.size = 0;

    bool first_queue = false;

    //exit the loop only when a sigint is received 
    while(true){

        // Use select to wait for data on the server FIFO
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        
        int activity = select(server_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity == -1) {
            perror("Error in select");
            free(child_servers);
            sem_close(semap);
            exit(EXIT_FAILURE);
        }
    
        if (FD_ISSET(server_fd, &read_fds)) {
            // New data is available, read from the server FIFO
            while ((returnval = read(server_fd, &serv_req, sizeof(struct request))) == -1 && errno == EINTR);
            
            if (returnval == -1) {
                perror("Reading from server fifo");
                free(child_servers);
                terminateChildren();
                close(server_fd);
                sem_close(semap);
                exit(EXIT_FAILURE);
            }
                
        }

        if(sigint_flag == 1){
            char message[] = "Server received SIGINT, terminating the program...\n";
            write(server_fd, message, sizeof(message));
            terminateChildren();
            free(child_servers);  
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        bool avaliable_found = false;
        //request queue is empty find a free child and pass the request
        if(serv_req.command != NULL && isRequestQueueEmpty(requestQueue) && !avaliable_found){

            for(int i = 0; i < max_client; i++){
                if(child_servers[i].avaliable == true){
                    avaliable_server_total--;
                    child_servers[i].avaliable = false;
                    serv_req.number = ++total_client;
                    avaliable_found = true;
                    //write the request to the child pipe
                    while((returnval = write(child_servers[i].pipe_write_fd, &serv_req, sizeof(struct request))) == -1 && errno == EINTR);
                    if(returnval == -1){
                        perror("Server failed to write to the pipe");
                        free(child_servers);  
                        close(server_fd);                        
                        close(child_servers[i].pipe_write_fd);
                    }                    

                    break;

                }
            }

        }
        //request queue is not empty and avaliable child processes are present
        if(serv_req.command != NULL && !isRequestQueueEmpty(requestQueue) && ( avaliable_server_total > 0 )){

            if (requestQueue.size > avaliable_server_total && (strcmp(serv_req.command, "tryConnect") == 0)){
                struct response try_connect;
                strcpy(try_connect.reply, "TryConnect failed. Que FULL...\n");
                try_connect.connected = -1;
                char clientfifo[CLIENT_FIFO_NAME_LENGTH];
                snprintf(clientfifo, CLIENT_FIFO_NAME_LENGTH, CLIENT_FIFO_NAME, serv_req.pid);
                int client_fifo_fd = open(clientfifo, O_WRONLY);
                if(client_fifo_fd < 0 ){
                    perror("Error opening client fifo");
                }

                while(((returnval = write(client_fifo_fd, &try_connect, sizeof(struct response))) == -1) && errno == EINTR);
                    if(returnval < 0){
                        perror("Error writing to client fifo from server");
                        free(child_servers);
                        close(server_fd);
                        exit(EXIT_FAILURE);
                    }        
                close(client_fifo_fd);

            }
            
            else{
                //place the new request to tail of the queue
                enQueue(&requestQueue, serv_req);
                struct response resconnect;
                strcpy(resconnect.reply, "Waiting for Que.. ");
                resconnect.connected = -2;
                char clientfifo[CLIENT_FIFO_NAME_LENGTH];
                snprintf(clientfifo, CLIENT_FIFO_NAME_LENGTH, CLIENT_FIFO_NAME, serv_req.pid);
                int client_fifo_fd = open(clientfifo, O_WRONLY);
                if(client_fifo_fd < 0 ){
                    perror("Error opening client fifo");
                }

                while(((returnval = write(client_fifo_fd, &resconnect, sizeof(struct response))) == -1) && errno == EINTR);
                    if(returnval < 0){
                        perror("Error writing to client fifo from server");
                        free(child_servers);
                        close(server_fd);
                        exit(EXIT_FAILURE);
                    }        
                close(client_fifo_fd);    


                memset(&serv_req, 0, sizeof(serv_req));
                serv_req = deQueue(&requestQueue);

                for(int i = 0; i < max_client; i++){
                    if(child_servers[i].avaliable == true){
                        avaliable_server_total--;
                        child_servers[i].avaliable = false;
                        serv_req.number = ++total_client;

                    while((returnval = write(child_servers[i].pipe_write_fd, &serv_req, sizeof(struct request))) == -1 && errno == EINTR);
                    if(returnval == -1){
                        perror("Server failed to write to the pipe");
                        free(child_servers);  
                        close(server_fd);                        
                        close(child_servers[i].pipe_write_fd);
                    }
                    }

                }
            }
        }  
        //request queue is empty but no child process is avaliable at the moment
        if(isRequestQueueEmpty(requestQueue) && !avaliable_found){
            if (strcmp(serv_req.command, "tryConnect") == 0){
                struct response try_connect;
                strcpy(try_connect.reply, "TryConnect failed. Que FULL...\n");
                try_connect.connected = -1;
                char clientfifo[CLIENT_FIFO_NAME_LENGTH];
                snprintf(clientfifo, CLIENT_FIFO_NAME_LENGTH, CLIENT_FIFO_NAME, serv_req.pid);
                int client_fifo_fd = open(clientfifo, O_WRONLY);
                if(client_fifo_fd < 0 ){
                    perror("Error opening client fifo");
                }

                while(((returnval = write(client_fifo_fd, &try_connect, sizeof(struct response))) == -1) && errno == EINTR);
                    if(returnval < 0){
                        perror("Error writing to client fifo from server");
                        free(child_servers);
                        close(server_fd);
                        exit(EXIT_FAILURE);
                    }        
                close(client_fifo_fd);

            }            
            else{
                enQueue(&requestQueue, serv_req);
                struct response resconnect;
                strcpy(resconnect.reply, "Waiting for Que.. ");
                resconnect.connected = -2;
                char clientfifo[CLIENT_FIFO_NAME_LENGTH];
                snprintf(clientfifo, CLIENT_FIFO_NAME_LENGTH, CLIENT_FIFO_NAME, serv_req.pid);
                int client_fifo_fd = open(clientfifo, O_WRONLY);
                if(client_fifo_fd < 0 ){
                    perror("Error opening client fifo");
                }

                while(((returnval = write(client_fifo_fd, &resconnect, sizeof(struct response))) == -1) && errno == EINTR);
                    if(returnval < 0){
                        perror("Error writing to client fifo from server");
                        free(child_servers);
                        close(server_fd);
                        exit(EXIT_FAILURE);
                    }        
                close(client_fifo_fd);    

            }

        }


    }
                              

        

    if(sigint_flag){
        terminateChildren();
        sem_close(semap);
        sem_close(serverSem);
        close(server_fd);
        

    } 


    //The loop is over closing the server
    
    for(int i = 0; i < max_client; i++)
        close(child_servers[i].fifo_write_fd);

    terminateChildren();
    unlink(server_fifo);
    close(server_fd);
    sem_close(semap);
    sem_unlink("/semaphore");
    sem_close(serverSem);
    sem_unlink("/serverSemap");

}

bool file_exists(const char* filename) {
    struct stat buffer;
    return (stat(filename, &buffer) == 0);
}

int convertToInt(const char* str) {
    char* endptr;
    errno = 0;
    long result = strtol(str, &endptr, 10);

    if (errno != 0 || endptr == str) {
        // Conversion failed
        return -1;
    }

    return (int)result;
}

int createFifo()
{

    umask(0); /* So we get the permissions we want */
    snprintf(server_fifo, SERVER_FIFO_NAME_LENGTH, SERVER_FIFO_NAME, (int)getpid());
    int server_fd;
    /* create the server fifo*/
    if ((mkfifo(server_fifo, S_IRUSR | S_IWUSR | S_IWGRP) == -1) && errno != EEXIST)
    {
        perror("server create");
        exit(EXIT_FAILURE);
    }
    if (chmod(server_fifo, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH) == -1) {
        perror("chmod");
        exit(EXIT_FAILURE);
    }

    /*open it for read and write*/
    server_fd = open(server_fifo, O_RDWR);
    if(server_fd < 0){
        perror("server: open");
        exit(EXIT_FAILURE);
    }

    return server_fd;
}

void changeDirectory(char *directory)
{

    struct stat st = {0};
    // Check if directory exists, create it if it doesn't
    if (stat(directory, &st) == -1)
        mkdir(directory, 0700);

    // Change working directory to the specified directory
    int ret = chdir(directory);

    // Error handling
    if (ret != 0)
    {
        fprintf(stderr, "An error occurred: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void deleteWords(char* string, int numWords) {
    char* p = string;
    int count = 0;

    // Skip leading whitespaces
    while (*p != '\0' && (*p == ' ' || *p == '\t'))
        p++;

    // Count words and find the position to delete from
    while (*p != '\0' && count < numWords) {
        if (*p == ' ' || *p == '\t') {
            count++;
            // Skip consecutive whitespaces
            while (*p != '\0' && (*p == ' ' || *p == '\t'))
                p++;
        } else {
            p++;
        }
    }

    // Copy the remaining part of the string
    memmove(string, p, strlen(p) + 1);
}

void readF(const char* fileName, int lineNo, int clientFifo){

    FILE* file = fopen(fileName, "rb");
    if(file == NULL){
        perror("Error opening file");
        sem_post(semap);
        exit(EXIT_FAILURE);
    }

    struct response read_resp;
    int ret;
    int line_counter = 1;

    //read and write the whole file
    if(lineNo == -1){
        while (fgets(read_resp.reply, sizeof(read_resp.reply), file) != NULL) {
            read_resp.end = 0;
            while((ret = write(clientFifo, &read_resp, sizeof(read_resp))) == -1 && errno == EINTR);
                if(ret == -1){
                    perror("Error writing to client fifo");
                    sem_post(semap);
                    sem_close(semap);
                    fclose(file);
                    exit(EXIT_FAILURE);
                }

        }

    }

    else{
        while (fgets(read_resp.reply, sizeof(read_resp.reply), file) != NULL) {
            if (line_counter == lineNo) {
                read_resp.end = 0;
                while((ret = write(clientFifo, &read_resp, sizeof(read_resp))) == -1 && errno == EINTR);
                    if(ret == -1){
                        perror("Error writing to client fifo");
                        sem_post(semap);
                        sem_close(semap);
                        exit(EXIT_FAILURE);
                    }               

            }
            line_counter++;
        }

    }

    fclose(file);

}

void writeF(const char* fileName, char* lineNoStr, char* new_content){

    int file_created = 0;
    FILE* fp = fopen(fileName, "rb+");
    if (fp == NULL) {
        // File does not exist, create and edit it
        fp = fopen(fileName, "wb+");
        file_created = 1;
        if (fp == NULL) {
            perror("File creation error");
            return;
        }
    }

    int lineCount = 1;
    char buffer[1024];
    int i = 0;

    int returnval;
    while(((returnval = sem_wait(semap)) == -1) && errno == EINTR);
    if(returnval == -1){
        perror("Cannot wait on semaphore");
        sem_close(semap);
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    //file did not exist and no line number is given
    if(file_created && lineNoStr == NULL){
        fprintf(fp, "%s", new_content);
    }

    //file did not exist but line number is given
    else if(file_created && lineNoStr != NULL){
        int lineNo = atoi(lineNoStr);

        for(int i = 1; i < lineNo; i++){
            fprintf(fp, "\n");
        }

        fprintf(fp, "%s\n", new_content);
    }
    //file does exist, no line number is given
    else if(!file_created && lineNoStr == NULL){
        fseek(fp, 0, SEEK_END);  // Move file pointer to the end of the file
        fprintf(fp, "%s", new_content);
    }

    //file does exist and a line number is provided
    else{
        int line_number = atoi(lineNoStr);
        FILE* temp_file = fopen("temp.txt", "w");

        if (temp_file == NULL) {
            perror("Failed to open file.");
            fclose(fp);
            return;
        }

        char buffer[1024];
        int current_line = 1;

        // Read each line from the original file and write it to the temporary file
        while (fgets(buffer, sizeof(buffer), fp)) {
            if (current_line == line_number) {
                // Modify the desired line by writing the new content
                fprintf(temp_file, "%s\n", new_content);
            } else {
                // Write the original line to the temporary file
                fputs(buffer, temp_file);
            }

            current_line++;
        }

        fclose(fp);
        fclose(temp_file);

        // Delete the original file
        remove(fileName);

        // Rename the temporary file to the original file name
        rename("temp.txt", fileName);
    }

    while(((returnval = sem_post(semap)) == -1) && errno == EINTR);
    if(returnval == -1){
        perror("Cannot post on semaphore");
        sem_close(semap);
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    fclose(fp);
}


void upload(const char* fileName, char* clientDirectory, int child_index){ 

    char* sourceF = (char*)malloc((strlen(fileName) + strlen(clientDirectory) + 2)* sizeof(char));
    strcpy(sourceF, clientDirectory);
    strcat(sourceF, "/");
    strcat(sourceF, fileName);
    char buffer[MAX_COMMAND];

    //file does not exist in client directory
    if(!file_exists(sourceF)){
        fprintf(stderr, "File does not exist in client directory.\n");
        free(sourceF);
        return;
    }
    //file with the same name exits in server directory
    if(file_exists(fileName)){
        fprintf(stderr, "File with the same name exists in server directory.\n");
        free(sourceF);
        return;
    }

    int source_fd = open(sourceF, O_RDONLY);
    if (source_fd == -1) {
        perror("open source file");
        free(sourceF);
        exit(EXIT_FAILURE);
    }

    int destination_fd = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (destination_fd == -1) {
        perror("open destination file");
        close(source_fd);
        free(sourceF);
        exit(EXIT_FAILURE);
    }
    // get the semaphore
    int returnval;
    while(((returnval = sem_wait(semap)) == -1) && errno == EINTR);
        if(returnval == -1){
            perror("Cannot wait on semaphore");
            sem_close(semap);
            close(source_fd);
            close(destination_fd);
            free(sourceF);
            exit(EXIT_FAILURE);
    }

    //send the beggining file upload message to client
    struct response respon;
    strcpy(respon.reply, "Beginning file upload:\n");
    respon.end = 0;
    while(((returnval = write(child_servers[child_index].fifo_write_fd, &respon, sizeof(respon))) == -1) && errno == EINTR);
        if(returnval < 0){
            perror("Error writing to client fifo");
            close(source_fd);
            free(sourceF);            
            close(destination_fd);
            sem_post(semap);
            exit(EXIT_FAILURE);
    }

    ssize_t bytes_read, bytes_written, total_bytes = 0;
    //upload the file
    while ((bytes_read = read(source_fd, buffer, MAX_COMMAND)) > 0) {
        bytes_written = write(destination_fd, buffer, bytes_read);
        total_bytes += bytes_written;
        if (bytes_written == -1) {
            perror("write destination file");
            close(source_fd);
            close(destination_fd);
            free(sourceF);
            exit(EXIT_FAILURE);
        }
    }

    //relesase the semaphore
    while((returnval = sem_post(semap)) == -1 && errno == EINTR);
        if(returnval == -1){
            perror("Cannot post semaphore");
            sem_close(semap);
            close(source_fd);
            close(destination_fd);
            free(sourceF);
            exit(EXIT_FAILURE);                                
    }
    close(source_fd);
    close(destination_fd);
    free(sourceF);

    respon.end = 1;
    snprintf(respon.reply, sizeof(respon.reply), "%d bytes uploaded.\n", (int) total_bytes);
    while(((returnval = write(child_servers[child_index].fifo_write_fd, &respon, sizeof(respon))) == -1) && errno == EINTR);
        if(returnval < 0){
            perror("Error writing to client fifo");
            exit(EXIT_FAILURE);
    }                      

}




void download(char* fileName, char* clientDirectory, int child_index){

    char cwd[MAX_INP];
    if(getcwd(cwd, sizeof(cwd)) == NULL){
        perror("Error getting the current working directory\n");
        exit(EXIT_FAILURE);
    }

    if(!file_exists(fileName)){
        fprintf(stderr, "File does not exists in server directory.\n");
        exit(EXIT_FAILURE);
    }

    int source_fd = open(fileName, O_RDONLY);
    if (source_fd == -1) {
        perror("open source file");
        exit(EXIT_FAILURE);
    }
    //change the cwd to clients directory
    if(chdir(clientDirectory) != 0){
        perror("Error changing the working directory.");
        close(source_fd);
        exit(EXIT_FAILURE);
    }

    //get the semaphore
    int returnval;
    while(((returnval = sem_wait(semap)) == -1) && errno == EINTR);
        if(returnval == -1){
            perror("Cannot wait on semaphore");
            sem_close(semap);
            close(source_fd);
            exit(EXIT_FAILURE);
    }

    while(file_exists(fileName)){
        fprintf(stderr, "File already exists in server directory.\n");
        exit(EXIT_FAILURE);
    }

    int destination_fd = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (destination_fd == -1) {
        perror("open destination file");
        sem_post(semap);
        sem_close(semap);
        close(source_fd);
        exit(EXIT_FAILURE);
    }

    struct response message;
    strcpy(message.reply, "Beginning file transfer:\n");
    while(((returnval = write(child_servers[child_index].fifo_write_fd, &message, sizeof(message))) == -1) && errno == EINTR);
        if(returnval < 0){
            perror("Error writing to client fifo");
            close(source_fd);
            close(destination_fd);
            sem_post(semap);
            exit(EXIT_FAILURE);
    }                                    

    ssize_t bytes_read, bytes_written, total_bytes = 0;
    char buffer[MAX_COMMAND];

    while ((bytes_read = read(source_fd, buffer, MAX_COMMAND)) > 0) {
        bytes_written = write(destination_fd, buffer, bytes_read);
        total_bytes += bytes_written;
        if (bytes_written == -1) {
            perror("write destination file");
            close(source_fd);
            close(destination_fd);
            sem_post(semap);
            exit(EXIT_FAILURE);
        }
    }
    //relesase the semaphore
    while((returnval = sem_post(semap)) == -1 && errno == EINTR);
        if(returnval == -1){
            perror("Cannot post semaphore");
            sem_close(semap);
            close(source_fd);
            close(destination_fd);
            exit(EXIT_FAILURE);                                
    }

    close(source_fd);
    close(destination_fd);

    snprintf(message.reply, sizeof(message.reply), "%d bytes transferred.\n", (int) total_bytes);
    message.end = 1;
    while(((returnval = write(child_servers[child_index].fifo_write_fd, &message, sizeof(message))) == -1) && errno == EINTR);
        if(returnval < 0){
            perror("Error writing to client fifo");
            close(child_servers[child_index].fifo_write_fd);
            exit(EXIT_FAILURE);
    }                      

}
