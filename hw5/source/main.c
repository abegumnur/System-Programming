#include "utility.h"

/// @brief join all the threads created so far 
/// @param producerThread one producer thread
/// @param consumerThreads pool size number of consumer threads
/// @param pool_size command line argument indicating the number of consumer threads
void joinAllThreads(pthread_t producerThread, pthread_t consumerThread);

void* consumer(void* args);
void* producer(void* args);
void* consumerCreate(void* args);

///volatile sig_atomic_t terminateThreads = 0;

void sigint_handler(int signal){
    
    if(signal == SIGINT)
        sigint_flag = 1;

}

int main(int argc, char** argv){

    setbuf(stdout, NULL);

    // setting up the signal handler for SIGINT
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    if(sigaction(SIGINT, &sa, NULL) == -1){
        perror("Error initializing the SIGINT handler. Terminating...\n");
        exit(EXIT_FAILURE);
    }

    // check the validity of the number of arguments entered
    if(argc != 5){
        fprintf(stderr, "Usage: <program> <#buffer_size> <#pool_size> <source_Dir> <destination_Dir>.\n");
        exit(EXIT_FAILURE);
    }


    if(sigint_flag == 1){
        printf("SIGINT received before creating any threads. Terminating the program...\n");
        exit(EXIT_FAILURE);
    }

    buffer_size = atoi(argv[1]);
    pool_size = atoi(argv[2]);

    struct stat st;
    if (stat(argv[3], &st) != 0 || !S_ISDIR(st.st_mode)){
        printf("Source directory does not exists! Terminating the program...\n");
        exit(EXIT_FAILURE);
    }


    // initialize the buffer that holds the file names/descriptors
    initializeBuffer();

    // initialize the file types struct
    initializeFileTypes();

    // create the producer thread and consumer threads
    pthread_t producerThread;
    pthread_t consumerThreads[pool_size];
    threadArgs args;

    strncpy(args.sourceDir, argv[3], sizeof(args.sourceDir));
    strncpy(args.destinationDir, argv[4], sizeof(args.destinationDir));    

    if(sigint_flag == 1){
        printf("SIGINT received before creating any threads. Terminating the program...\n");
        freeBuffer();
        exit(EXIT_FAILURE);
    }

    // get the current time before creating the first thread
    struct timeval startTime;
    gettimeofday(&startTime, NULL);

    pthread_create(&producerThread, NULL, producer, (void*)&args);

    if(sigint_flag == 1){
        printf("SIGINT received. Terminating the program...\n");
        pthread_cancel(producerThread);
        pthread_join(producerThread, NULL);
        if(!isBufferEmpty()){
            for(int i = buffer.front; i != buffer.rear; i = (i + 1) % buffer_size){
                close(buffer.entry[i].destination_fd);
                close(buffer.entry[i].source_fd);
            }
        }
        freeBuffer();
        exit(EXIT_FAILURE);
    }

    // create the consumer manager thread
    // which will manage the consumer threads and keep the
    // number of active threads under control
    pthread_t consumerManager;
    pthread_create(&consumerManager, NULL, consumerCreate, NULL);

    if(sigint_flag == 1){
        printf("SIGINT received. Terminating the program...\n");
       // pthread_kill(producerThread, SIGINT);
        pthread_cancel(producerThread);
        pthread_join(producerThread, NULL);
        for(int i = 0; i < pool_size; i++){
            pthread_cancel(consumerThreads[i]);
        }
        for(int i = 0; i < pool_size; i++){
            pthread_join(consumerThreads[i], NULL);
        }
        if(!isBufferEmpty()){
            for(int i = buffer.front; i != buffer.rear; i = (i + 1) % buffer_size){
                close(buffer.entry[i].destination_fd);
                close(buffer.entry[i].source_fd);
            }
        }
        freeBuffer();
        exit(EXIT_FAILURE);        
    }

    joinAllThreads(producerThread, consumerManager);

    // get the current time after joining the last thread
    struct timeval endTime;
    gettimeofday(&endTime, NULL);

    // calculate and print the total time
    long int seconds = endTime.tv_sec - startTime.tv_sec;
    long int microseconds = endTime.tv_usec - startTime.tv_usec;
    double elapsed = seconds + microseconds / 1000000.0;

    printf("Successfully copied directory %s at %.6f seconds.\n", args.sourceDir, elapsed);
    printFileStatistics();
    
    freeBuffer();
}


void joinAllThreads(pthread_t producerThread, pthread_t consumerThread){

    pthread_join(producerThread, NULL);
    pthread_join(consumerThread, NULL);

}



