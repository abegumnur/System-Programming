#include "server.h"
/*  fifo strings    */
char server_fifo[SERVER_FIFO_NAME_LENGTH];
char client_fifo[SERVER_FIFO_NAME_LENGTH];
/*  comment mutexes */
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t readwritemutex = PTHREAD_MUTEX_INITIALIZER;

volatile int sigint_flag = -1;

int max_clients;
int pool_size;
/*  directory info  */
char clientDir[BUFFER];
char serverDir[BUFFER];
/*  log file info   */
char logFileName[MAX_INP];
int logFile;
struct flock lock;
/*  main thread info kept for raising signal    */
pthread_t mainThread;
/*  running queue is accessed by the worker threads */
RunningQueue queue;
/*  mutex and condition var for the number of running requests  */
pthread_mutex_t runningReqMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t runningReqCond = PTHREAD_COND_INITIALIZER;
int runningRequests = 0;

/*  constantly checked if true in the thread function */
bool terminateThread = false;

void* handleRequest(void* arg){
    
    int* threadNo = (int *)arg;

    while(true){
    /*  pick a client from the running queue    */
    struct request req_handle = dequeue(max_clients);
    /*  increment the runningRequests variable  */
    pthread_mutex_lock(&runningReqMutex);
    runningRequests++;
    if(runningRequests < max_clients)
        pthread_cond_signal(&runningReqCond);  // Signal the condition variable
    pthread_mutex_unlock(&runningReqMutex);    

    if(terminateThread)
        exit(EXIT_FAILURE);
    
    /*  open the thread fifo reading    */
    char thread_fifo[SERVER_FIFO_NAME_LENGTH];
    snprintf(thread_fifo, SERVER_FIFO_NAME_LENGTH, SERVER_FIFO_NAME, req_handle.pid);

    int thread_fd = open(thread_fifo, O_RDWR);
    if (thread_fd == -1){
        perror("thread fifo open");
        exit(EXIT_FAILURE);
    }            

    //open the client fifo for writing
    int client_id = req_handle.pid;
    int client_no = req_handle.number;

    umask(0); /* So we get the permissions we want */
    snprintf(client_fifo, CLIENT_FIFO_NAME_LENGTH, CLIENT_FIFO_NAME, (int)req_handle.pid);

    int client_fd = open(client_fifo, O_WRONLY);
    if (client_fd == -1){
        perror("thread client fifo open");
        exit(EXIT_FAILURE);
    }        

    if(terminateThread){
        close(client_fd);
        close(thread_fd);
        exit(EXIT_FAILURE);
    }

    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(client_fd, &readFds);

    // Set the timeout value to 1 second
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    // Use select to wait until the FIFO is ready for reading or timeout occurs
    int ready = select(thread_fd + 1, &readFds, NULL, NULL, &timeout);    

    if(ready == -1){
        perror("Error in select");
        close(client_fd);
        close(thread_fd);
        unlink(thread_fifo);
        exit(EXIT_FAILURE);
    }
    else{

    int returnval;
    struct request req;
    while((returnval = read(thread_fd, &req, sizeof(struct request))) == -1 && errno == EINTR);
    if(returnval == -1){
        perror("Reading from client fifo");
        close(client_fd);
        close(thread_fd);
        exit(EXIT_FAILURE);
    }        

    if(terminateThread){
        close(client_fd);
        close(thread_fd);
        exit(EXIT_FAILURE);
    }

    struct response resp;
    resp.end = 0;
    int num_words = countWords(req.command);

    /*  while writing the response is not over  */
    while(!resp.end){
        memset(&resp, 0, sizeof(resp));
        /*  help    */
        if(strcmp(req.command, options[0]) == 0){
            strcpy(resp.reply, "Avaliable comments are:\nhelp, list, readF, writeT, upload, download, quit, killServer\n");
            resp.end = 1;

            while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
            if(returnval < 0){
                perror("Error writing to client fifo");
                close(client_fd);
                close(thread_fd);
                exit(EXIT_FAILURE);
            }
            /*  place the client back to the running queue  */
            enqueue(req_handle, max_clients);
        }

        if(terminateThread){
            close(client_fd);
            close(thread_fd);
            exit(EXIT_FAILURE);
        }

        else if(strcmp(req.command, options[6]) == 0){
            //send write request to log file lock and unlock
            //Sending write request to server log filewaiting for logfile ...
            strcpy(resp.reply, "Sending write request to server log file\nwaiting for log file\n");
            resp.end = 0;
            while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
            if(returnval < 0){
                perror("Error writing to client fifo");
                exit(EXIT_FAILURE);
            }

            time_t rawTime;
            struct tm* timeInfo;
            char timestamp[20];

            time(&rawTime);
            timeInfo = localtime(&rawTime);
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeInfo);
            char logmessage[MAX_INP];
            snprintf(logmessage, MAX_INP, "Client PID#%d handled with the command %s.\n", req_handle.pid, req.command); 
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
                exit(EXIT_FAILURE);
            }

            lock.l_type = F_UNLCK;
            while(((fcntl(logFile, F_SETLKW, &lock)) == -1) && errno == EINTR);

            strcpy(resp.reply, "Byeee\n");
            resp.end = 1;
            resp.connected = -1;
            while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
            if(returnval < 0){
                perror("Error writing to client fifo");
                close(logFile);
                exit(EXIT_FAILURE);
            }                            
            //quit the client
            close(client_fd);

            if(terminateThread){
                close(thread_fd);
                exit(EXIT_FAILURE);
            }

            fprintf(stdout, "client%d disconnected...\n", client_no);
            /*  decrement the runningRequests value */
            pthread_mutex_lock(&runningReqMutex);
            runningRequests--;
            if(runningRequests < max_clients)
                pthread_cond_signal(&runningReqCond);  // Signal the condition variable
            pthread_mutex_unlock(&runningReqMutex);    

        }

        /*  killServer  */
        else if(strcmp(req.command, options[7]) == 0){
            //handle the termination gracefully
            strcpy(resp.reply, "Sending a kill request to the server.\n");
            resp.end = 1;         
            resp.connected = -1;
            while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
            if(returnval < 0){
                perror("Error writing to client fifo");
                close(client_fd);
                close(thread_fd);
                exit(EXIT_FAILURE);
            }              
            /*  raise a SIGINT to the main thread    */
            pthread_kill(mainThread, SIGINT);
        }

        //list command
        else if(strcmp(req.command, options[1]) == 0){   
            struct dirent *entry;
            DIR *dir;

            pthread_mutex_lock(&mutex);  // Acquire the mutex

            dir = opendir(".");
            while ((entry = readdir(dir)) != NULL) {
                strcat(resp.reply, entry->d_name);
                strcat(resp.reply, "\n");
            }

            if(terminateThread){
                close(thread_fd);
                close(client_fd);
                exit(EXIT_FAILURE);
            }
            resp.reply[127] = '\0';   
            closedir(dir);

            pthread_mutex_unlock(&mutex);  // Release the mutex
            resp.end = 1;
            while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
            if(returnval < 0){
                perror("Error writing to client fifo");
                exit(EXIT_FAILURE);
            }

            enqueue(req_handle, max_clients);

        }

        else{

            char cmnd[4][BUFFER];
            char* commanddup = strdup(req.command);

            char* token = strtok(req.command, " ");
            int i = 0;
            while(token != NULL && i < 4){
                strcpy(cmnd[i], token);
                i++;
                token = strtok(NULL, " ");
            }

            if(terminateThread){
                close(thread_fd);
                close(client_fd);
                exit(EXIT_FAILURE);
            }

            //help $
            if(strcmp(cmnd[0], options[0]) == 0){
                free(commanddup);
                if (strcmp(cmnd[1], options[1]) == 0){
                    strcpy(resp.reply, "list\nsends a request to display the list of files in Servers directory.\n");
                    resp.end = 1;                                
                    while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                    if(returnval < 0){
                        perror("Error writing to client fifo");
                        exit(EXIT_FAILURE);
                    }

                }
                
                if(terminateThread){
                    close(thread_fd);
                    close(client_fd);
                    exit(EXIT_FAILURE);
                }

                else if(strcmp(cmnd[1], options[2]) == 0){
                    strcpy(resp.reply, "readF <file> <line#>\ndisplay the #th line of the <file>, returns with an error if <file> does not exists.\n");
                    resp.end = 1;
                    while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                    if(returnval < 0){
                        perror("Error writing to client fifo");
                        exit(EXIT_FAILURE);
                    }

                }
                else if(strcmp(cmnd[1], options[3]) == 0){
                    resp.end = 0;
                    strcpy(resp.reply, "writeT <file> <line #> <string>\nwrite the  content of “string” to the  #th  line the <file>.\n" );
                    while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                    if(returnval < 0){
                        perror("Error writing to client fifo");
                        exit(EXIT_FAILURE);
                    }                            

                    strcpy(resp.reply, "If the file does not exists in Servers directory creates and edits the file at the same time.\n");
                    resp.end = 1;
                    while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                    if(returnval < 0){
                        perror("Error writing to client fifo");
                        exit(EXIT_FAILURE);
                    }     
                if(terminateThread){
                    close(thread_fd);
                    close(client_fd);
                    exit(EXIT_FAILURE);
                }
    
                }
                else if(strcmp(cmnd[1], options[4]) == 0){
                    resp.end = 1;                                    
                    strcpy(resp.reply, "upload <file>\nuploads the file from the current working directory of client to the Servers directory.\n" );
                    while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                    if(returnval < 0){
                        perror("Error writing to client fifo");
                        exit(EXIT_FAILURE);
                    }                                   

                }

                else if(strcmp(cmnd[1], options[5]) == 0){
                    resp.end = 1;        
                    strcpy(resp.reply, "download <file>\nreceive <file> from Servers directory to client side.\n" );
                    while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                    if(returnval < 0){
                        perror("Error writing to client fifo");
                        exit(EXIT_FAILURE);
                    }                                   


                }
                else if(strcmp(cmnd[1], options[6]) == 0){
                    resp.end = 1;        
                    strcpy(resp.reply, "quit\nSend write request to Server side log file and quits.\n" );
                    while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                    if(returnval < 0){
                        perror("Error writing to client fifo");
                        exit(EXIT_FAILURE);
                    }                                   

                }

                if(terminateThread){
                    close(thread_fd);
                    close(client_fd);
                    exit(EXIT_FAILURE);
                }

                else if(strcmp(cmnd[1], options[7]) == 0){
                    resp.end = 1;                                            
                    strcpy(resp.reply, "killServer\nSends a kill request to the Server.\n" );
                    while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
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
                while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                if(returnval < 0){
                    perror("Error writing to client fifo");
                    close(client_fd);
                    close(thread_fd);
                    unlink(thread_fifo);
                    exit(EXIT_FAILURE);
                    }        
                    continue;
                }
                else{

                    if(terminateThread){
                    close(thread_fd);
                    close(client_fd);
                    exit(EXIT_FAILURE);
                    }

                    if(pthread_mutex_lock(&readwritemutex) != 0){
                        perror("Cannot lock read write mutex");
                        exit(EXIT_FAILURE);
                    }

                    if(i == 3){
                        int line = atoi(cmnd[2]);
                        readF(cmnd[1], line, client_fd);
                    }
                    
                    else    
                        readF(cmnd[1], -1, client_fd);

                    pthread_mutex_unlock(&readwritemutex);         

                    resp.end = 1;   
                    strcpy(resp.reply, "Reading the file is done.\n" );
                    while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                    if(returnval < 0){
                        perror("Error writing to client fifo");
                        exit(EXIT_FAILURE);
                    }                                      

                }

            }

            //if the command is writeT
            if(strcmp(cmnd[0], options[3]) == 0){
                resp.end = 0;
                strcpy(resp.reply, "Sending write request to file.\n" );
                while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                if(returnval < 0){
                    perror("Error writing to client fifo");
                    close(client_fd);
                    close(thread_fd);
                    unlink(thread_fifo);
                    exit(EXIT_FAILURE);
                }

                if(terminateThread){
                close(thread_fd);
                close(client_fd);
                exit(EXIT_FAILURE);
                }


                int log = 1;
                //second argument is not the line number append to the file
                if(convertToInt(cmnd[2]) == -1){
                    deleteWords(commanddup, 2);

                    //client is trying to write to log file
                    if(strcmp(cmnd[1], logFileName) == 0){
                        resp.end = 1;   
                        strcpy(resp.reply, "Client cannot write to log file.\n" );
                        while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                        if(returnval < 0){
                            perror("Error writing to client fifo");
                            close(client_fd);
                            close(thread_fd);
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

                    if(terminateThread){
                    close(thread_fd);
                    close(client_fd);
                    exit(EXIT_FAILURE);
                    }

                    if(strcmp(cmnd[1], logFileName) == 0){
                        resp.end = 1;   
                        strcpy(resp.reply, "Client cannot write to log file.\n" );
                        while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
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

                //free(commanddup);

                if(!log){
                    resp.end = 1;   
                    strcpy(resp.reply, "Writing to the file is done.\n" );
                    while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                    if(returnval < 0){
                        perror("Error writing to client fifo");
                        exit(EXIT_FAILURE);
                    }       
                }
            }

            if(terminateThread){
            close(thread_fd);
            close(client_fd);
            exit(EXIT_FAILURE);
            }

            //upload file command
            if(strcmp(cmnd[0], options[4]) == 0){
                free(commanddup);
                upload(cmnd[1], clientDir, client_fd);
                resp.end = 1;
            }

            //download file command
            if(strcmp(cmnd[0], options[5]) == 0){
                free(commanddup);
    
                if(strcmp(cmnd[1], logFileName) == 0){
                    resp.end = 1;   
                    strcpy(resp.reply, "Client cannot download the log file.\n");
                    while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                    if(returnval < 0){
                        perror("Error writing to client fifo");
                        exit(EXIT_FAILURE);
                    }                                    
                }

                if(terminateThread){
                close(thread_fd);
                close(client_fd);
                exit(EXIT_FAILURE);
                }

                else{
                    download(cmnd[1], clientDir, client_fd);
                    resp.end = 1;
                }

            }
       
       /*    else{
                resp.end = 1;   
                strcpy(resp.reply, "Unvalid comment. Please type in \"help\" to see the avaliable comments.\n");
                while(((returnval = write(client_fd, &resp, sizeof(struct response))) == -1) && errno == EINTR);
                if(returnval < 0){
                    perror("Error writing to client fifo");
                    exit(EXIT_FAILURE);
                }                                    
            }   */             
                if(terminateThread){
                close(thread_fd);
                close(client_fd);
                exit(EXIT_FAILURE);
                }

            enqueue(req_handle, max_clients);
            
            }               
        }

    }

    }


}


int main(int argc, char** argv){
    
    initializeQueue();  //initialize the running queue
    RequestQueue request_queue;    
    initializeReq(&request_queue); //initialize the client queue

    setbuf(stdout, NULL);

    /* Initializing the signal handler for SIGINT */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = signalHandler;
    sigaction(SIGINT, &sa, NULL);

    /* get the client directory */
    if(getcwd(clientDir, sizeof(clientDir)) == NULL){
        perror("Error getting current working directory");
        exit(EXIT_FAILURE);
    }

    if(argc != 4){
        fprintf(stderr, "Usage: %s <dirname> <max.#ofClients> <poolSize>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /*Changing the directory to the second argument*/
    int size = strlen(argv[1]) + 1;
    char *directory = (char *)malloc(size * sizeof(char));
    strcpy(directory, argv[1]);
    changeDirectory(directory);
    free(directory);    

    max_clients = atoi(argv[2]);
    pool_size = atoi(argv[3]);

    int server_fd = createFifo();

    /*print the start message   */
    fprintf(stdout, "Server started PID %d.\n", getpid());
    fprintf(stdout, "Waiting for clients...\n");

    /*Open the log file*/
    snprintf(logFileName, MAX_INP, "log%d", getpid());
    logFile = open(logFileName, O_WRONLY | O_CREAT | O_APPEND, 0777);
        if(logFile == -1){
            perror("Error opening log file");
            close(server_fd);
            unlink(server_fifo);
            exit(EXIT_FAILURE);            
        }

    if(sigint_flag == 1){
        printf("SIGINT received before handling any request...\n");
        close(server_fd);
        unlink(server_fifo);
        close(logFile);
    }

    mainThread = pthread_self();

    //create the threads initialize mutex and condition variables
    pthread_t threads[pool_size];
    int* threadArgs = malloc(pool_size * sizeof(int));
    /*  initialize the threads  */
    for(int i = 0; i < pool_size; i++){
        threadArgs[i] = i + 1;
        
        if(pthread_create(&threads[i], NULL, handleRequest, &threadArgs[i]) != 0){
            perror("Error creating threads");
            free(threadArgs);
            close(server_fd);
            unlink(server_fifo);
            close(logFile);
            exit(EXIT_FAILURE);
        }
    }

    struct request serv_req;    //request read from the server fifo
    int returnval;
    int total_client_handled = 0;

    while(true){

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        
        int activity = select(server_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity == -1) {
            if (errno == EINTR) {
            fprintf(stdout, "killServer comment was given...\n");
            terminateThread = true;
             for(int i = 0; i < pool_size; i++)
                pthread_join(threads[i], NULL);                
            close(logFile);
            free(threadArgs);
            close(server_fd);
            unlink(server_fifo);
            exit(EXIT_FAILURE);            
            
            }
            perror("Error in select");
            terminateThread = true;
             for(int i = 0; i < pool_size; i++)
                pthread_join(threads[i], NULL);                
            close(logFile);
            free(threadArgs);
            close(server_fd);
            unlink(server_fifo);
            exit(EXIT_FAILURE);
        }
    
        if (FD_ISSET(server_fd, &read_fds)) {
            // New data is available, read from the server FIFO
            while ((returnval = read(server_fd, &serv_req, sizeof(struct request))) == -1 && errno == EINTR);
            if (returnval == -1) {
                perror("Reading from server fifo");
                terminateThread = true;
                for(int i = 0; i < pool_size; i++)
                    pthread_join(threads[i], NULL);                
                close(logFile);        
                free(threadArgs);        
                close(server_fd);
                unlink(server_fifo);
                exit(EXIT_FAILURE);
            }
                
        }
        /*  request is tryConnect and the running queue is full */
        if(strcmp(serv_req.command, "tryConnect") == 0 && runningRequests > max_clients){
            struct response resconnect;
            strcpy(resconnect.reply, "tryConnect failed... Que FULL..\n");
            resconnect.connected = -1;            

            char clientfifo[CLIENT_FIFO_NAME_LENGTH];
            snprintf(clientfifo, CLIENT_FIFO_NAME_LENGTH, CLIENT_FIFO_NAME, serv_req.pid);
            int client_fifo_fd = open(clientfifo, O_RDWR);
            if(client_fifo_fd < 0 ){
                perror("Error opening client fifo");
                terminateThread = true;
                for(int i = 0; i < pool_size; i++)
                    pthread_join(threads[i], NULL);                
                close(logFile);        
                free(threadArgs);        
                close(server_fd);
                unlink(server_fifo);
                exit(EXIT_FAILURE);
            }

            while(((returnval = write(client_fifo_fd, &resconnect, sizeof(struct response))) == -1) && errno == EINTR);
            if(returnval < 0){
                perror("Error writing to client fifo from server");
                terminateThread = true;
                for(int i = 0; i < pool_size; i++)
                    pthread_join(threads[i], NULL);                
                close(logFile);        
                free(threadArgs);        
                close(server_fd);
                unlink(server_fifo);
                exit(EXIT_FAILURE);
            }        
            close(client_fifo_fd);               
        }

        else{
            struct response resconnect;
            strcpy(resconnect.reply, "Waiting for Que.. ");
            resconnect.connected = -2;

            if(sigint_flag == 1){
                fprintf(stdout, "Server received SIGINT...Terminating..\n");
                terminateThread = true;
                for(int i = 0; i < pool_size; i++)
                    pthread_join(threads[i], NULL);                
                close(logFile);
                free(threadArgs);
                close(server_fd);
                unlink(server_fifo);
                exit(EXIT_FAILURE);
            }

            char clientfifo[CLIENT_FIFO_NAME_LENGTH];
            snprintf(clientfifo, CLIENT_FIFO_NAME_LENGTH, CLIENT_FIFO_NAME, serv_req.pid);
            int client_fifo_fd = open(clientfifo, O_RDWR);
            if(client_fifo_fd < 0 ){
                perror("Error opening client fifo");
                for(int i = 0; i < pool_size; i++)
                    pthread_join(threads[i], NULL);                
                close(logFile);
                free(threadArgs);
                close(server_fd);
                unlink(server_fifo);
                exit(EXIT_FAILURE);
            }

            while(((returnval = write(client_fifo_fd, &resconnect, sizeof(struct response))) == -1) && errno == EINTR);
            if(returnval < 0){
                perror("Error writing to client fifo from server");
                for(int i = 0; i < pool_size; i++)
                    pthread_join(threads[i], NULL);                
                close(logFile);
                free(threadArgs);
                close(server_fd);
                close(client_fifo_fd);
                unlink(server_fifo);
                exit(EXIT_FAILURE);
            }        
            close(client_fifo_fd);   

            int client_pid;
            int client_no;
            char client_command[MAX_COMMAND];

            /*  add the request to the resuest queue    */
            enqueueReq(&request_queue, serv_req.pid, ++total_client_handled, serv_req.command);

            if(sigint_flag == 1){
                fprintf(stdout, "Server received SIGINT...Terminating..\n");
                terminateThread = true;
                for(int i = 0; i < pool_size; i++)
                    pthread_join(threads[i], NULL);                
                close(logFile);
                free(threadArgs);
                close(server_fd);
                unlink(server_fifo);
                int pid, number;
                char command[MAX_COMMAND];
                while(dequeueReq(&request_queue, &pid, &number, command) != -1);
                exit(EXIT_FAILURE);
            }

            struct request new_req;
            // Wait until the counting value reaches the desired count
            pthread_mutex_lock(&runningReqMutex);
            while (runningRequests > max_clients) {
                pthread_cond_wait(&runningReqCond, &runningReqMutex);  // Wait for the condition variable to be signaled
            }
            pthread_mutex_unlock(&runningReqMutex);

            /*  get the front element in the request queue  */
            dequeueReq(&request_queue, &new_req.pid, &new_req.number, new_req.command);            

            /*  write the connection established message    */
            struct response connected;
            connected.end = 1;
            strcpy(connected.reply, "Connection established.\n");
            connected.connected = 1;
            snprintf(clientfifo, CLIENT_FIFO_NAME_LENGTH, CLIENT_FIFO_NAME, new_req.pid);

            int client_fd = open(clientfifo, O_RDWR);

            while(((returnval = write(client_fd, &connected, sizeof(struct response))) == -1) && errno == EINTR);
            if(returnval < 0){
                perror("Error writing to client fifo");
                terminateThread = true;
                for(int i = 0; i < pool_size; i++)
                    pthread_join(threads[i], NULL);          
                close(client_fd);          
                close(logFile);
                free(threadArgs);
                close(server_fd);
                unlink(server_fifo);
                int pid, number;
                char command[MAX_COMMAND];
                while(dequeueReq(&request_queue, &pid, &number, command) != -1);
                exit(EXIT_FAILURE);
            }
            
            if(sigint_flag == 1){
                fprintf(stdout, "Server received SIGINT...Terminating..\n");
                terminateThread = true;
                for(int i = 0; i < pool_size; i++)
                    pthread_join(threads[i], NULL);                
                close(logFile);
                free(threadArgs);
                close(server_fd);
                unlink(server_fifo);
                int pid, number;
                char command[MAX_COMMAND];
                while(dequeueReq(&request_queue, &pid, &number, command) != -1);
                exit(EXIT_FAILURE);
            }

            umask(0);
            char child_thread_fifo[SERVER_FIFO_NAME_LENGTH];
            snprintf(child_thread_fifo, sizeof(child_thread_fifo), SERVER_FIFO_NAME, (int)new_req.pid);

            if(mkfifo(child_thread_fifo, 0666) < 0 && errno != EEXIST){
                perror("server: mkfifo");
                terminateThread = true;
                for(int i = 0; i < pool_size; i++)
                    pthread_join(threads[i], NULL);                
                close(logFile);
                free(threadArgs);
                close(server_fd);
                unlink(server_fifo);
                int pid, number;
                char command[MAX_COMMAND];
                while(dequeueReq(&request_queue, &pid, &number, command) != -1);
                exit(EXIT_FAILURE);
            }

            fprintf(stdout, "Client PID %d connected as \"client%d\" \n",new_req.pid, new_req.number);

            /*  add the new request to the running queue*/
            enqueue(new_req, max_clients);

        }
 
    }

    for(int i = 0; i < pool_size; i++)
        pthread_join(threads[i], NULL);

    free(threadArgs);
    unlink(server_fifo);
    //close(server_fd);

    return 0;
}

/*  running queue functions */
void initializeQueue() {
    queue.front = 0;
    queue.rear = -1;
    queue.count = 0;
    pthread_mutex_init(&queue.mutex, NULL);
    pthread_cond_init(&queue.condition_nonempty, NULL);
    pthread_cond_init(&queue.condition_nonfull, NULL);
}

int isQueueEmpty() {
    return (queue.count == 0);
}

int isQueueFull(int maxSize) {
    return (queue.count == maxSize);
}

void enqueue(struct request request, int maxSize) {
    pthread_mutex_lock(&queue.mutex);
    while (isQueueFull(maxSize)) {
        pthread_cond_wait(&queue.condition_nonfull, &queue.mutex);
    }

    queue.rear = (queue.rear + 1) % maxSize;
    queue.req[queue.rear] = request;
    queue.count++;

    pthread_cond_signal(&queue.condition_nonempty);
    pthread_mutex_unlock(&queue.mutex);
}

struct request dequeue(int maxSize) {
    pthread_mutex_lock(&queue.mutex);
    while (isQueueEmpty()) {
        pthread_cond_wait(&queue.condition_nonempty, &queue.mutex);
    }

    struct request request = queue.req[queue.front];
    queue.front = (queue.front + 1) % maxSize;
    queue.count--;

    pthread_cond_signal(&queue.condition_nonfull);
    pthread_mutex_unlock(&queue.mutex);

    return request;
}

void printqueue(){

    if (isQueueEmpty()) {
        printf("The queue is empty.\n");
        return;
    }

    printf("Requests in the running queue: \n");
    int i = 0;
    while(i < queue.count) {
        printf("number: %-3.d pid: %-3.d command : %s\n", queue.req[i].number, queue.req[i].pid, queue.req[i].command);
        i++;
    }
    printf("\n");
}


// Function to initialize the request queue
void initializeReq(RequestQueue* queue) {
    queue->front = NULL;
    queue->rear = NULL;
}

// Function to enqueue a request
void enqueueReq(RequestQueue* queue, int requestPID, int requestNo, char requestCommand[]) {
    // Create a new request node
    Request* newRequest = (Request*)malloc(sizeof(Request));
    newRequest->pid = requestPID;
    newRequest->number = requestNo;
    strcpy(newRequest->command, requestCommand);
    newRequest->next = NULL;

    if (queue->rear == NULL) {
        // The queue is empty
        queue->front = newRequest;
        queue->rear = newRequest;
    } else {
        // Add the new request to the rear of the queue
        queue->rear->next = newRequest;
        queue->rear = newRequest;
    }
}

// Function to dequeue a request
int dequeueReq(RequestQueue* queue, pid_t* pid, int* no, char* commnd) {
    if (queue->front == NULL) {
        // The queue is empty
        return -1;  // Or any other suitable value indicating an empty queue
    }

    // Get the front request
    Request* frontRequest = queue->front;
    *no = frontRequest->number;
    *pid = frontRequest->pid;
    strcpy(commnd, frontRequest->command);

    // Move the front pointer to the next request
    queue->front = frontRequest->next;

    if (queue->front == NULL) {
        // The queue became empty after dequeuing
        queue->rear = NULL;
    }

    // Free the memory allocated for the front request node
    free(frontRequest);

    return 1;
}

// Function to check if the queue is empty
bool isEmptyReq(RequestQueue* queue) {
    return (queue->front == NULL);
}

// Function to print the requests in the queue
void printQueueReq(RequestQueue* queue) {
    if (isEmptyReq(queue)) {
        printf("The queue is empty.\n");
        return;
    }

    Request* current = queue->front;

    printf("Requests in the queue: \n");
    while (current != NULL) {
        printf("number: %-3.d pid: %-3.d command : %s\n", current->number, current->pid, current->command);
        current = current->next;
    }
    printf("\n");
}

int countWords(const char* str) {
    int count = 0;
    int i = 0;
    int len = strlen(str);
    
    // Skip leading spaces
    while (i < len && str[i] == ' ') {
        i++;
    }
    
    while (i < len) {
        // If current character is a word character
        if (str[i] != ' ' && (i == 0 || str[i-1] == ' ')) {
            count++;
        }
        i++;
    }
    
    return count;
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

void signalHandler(int signal_no, siginfo_t *siginfo, void *fd)
{

    if (signal_no == SIGINT)
        sigint_flag = 1;

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
        pthread_mutex_unlock(&readwritemutex);
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
                    pthread_mutex_unlock(&readwritemutex);
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
                        pthread_mutex_unlock(&readwritemutex);
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

    pthread_mutex_lock(&readwritemutex);

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
            pthread_mutex_unlock(&readwritemutex);
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

    pthread_mutex_unlock(&readwritemutex);

    fclose(fp);
}


void upload(const char* fileName, char* clientDirectory, int client_fd){ 

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

    pthread_mutex_lock(&mutex);

    //send the beggining file upload message to client
    struct response respon;
    strcpy(respon.reply, "Beginning file upload:\n");
    respon.end = 0;
    while(((returnval = write(client_fd, &respon, sizeof(respon))) == -1) && errno == EINTR);
        if(returnval < 0){
            perror("Error writing to client fifo");
            close(source_fd);
            free(sourceF);            
            close(destination_fd);
            pthread_mutex_unlock(&mutex);
            close(client_fd);
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
            pthread_mutex_unlock(&mutex);
            exit(EXIT_FAILURE);
        }
    }


    pthread_mutex_unlock(&mutex);

    close(source_fd);
    close(destination_fd);
    free(sourceF);

    respon.end = 1;
    snprintf(respon.reply, sizeof(respon.reply), "%d bytes uploaded.\n", (int) total_bytes);
    while(((returnval = write(client_fd, &respon, sizeof(respon))) == -1) && errno == EINTR);
        if(returnval < 0){
            perror("Error writing to client fifo");
            exit(EXIT_FAILURE);
    }                      

}


void download(char* fileName, char* clientDirectory, int client_fd){

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

    int returnval;

    if(file_exists(fileName)){
        fprintf(stderr, "File already exists in server directory.\n");
        exit(EXIT_FAILURE);
    }

    pthread_mutex_lock(&readwritemutex);

    int destination_fd = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (destination_fd == -1) {
        perror("open destination file");
        pthread_mutex_unlock(&readwritemutex);
        close(source_fd);
        exit(EXIT_FAILURE);
    }

    struct response message;
    strcpy(message.reply, "Beginning file transfer:\n");
    while(((returnval = write(client_fd, &message, sizeof(message))) == -1) && errno == EINTR);
        if(returnval < 0){
            perror("Error writing to client fifo");
            close(source_fd);
            close(destination_fd);
            pthread_mutex_unlock(&readwritemutex);
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
            pthread_mutex_unlock(&readwritemutex);
            exit(EXIT_FAILURE);
        }
    }

    pthread_mutex_unlock(&readwritemutex);

    close(source_fd);
    close(destination_fd);

    snprintf(message.reply, sizeof(message.reply), "%d bytes transferred.\n", (int) total_bytes);
    message.end = 1;
    while(((returnval = write(client_fd, &message, sizeof(message))) == -1) && errno == EINTR);
        if(returnval < 0){
            perror("Error writing to client fifo");
            close(client_fd);
            exit(EXIT_FAILURE);
    }                      

}





