#include "utility.h"

/* the number of active threads is less than or equal   */
/* to the pool size enforced by the active_t_mutex  */
int active_threads = 0;
pthread_mutex_t active_t_mutex = PTHREAD_MUTEX_INITIALIZER;


void* consumer(void* args) {

    pthread_t threadID = pthread_self();

    //printf("Consumer thread %lu has started.\n", threadID);

    char buffer_write[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;
    bufferEntry entry;
    char fileName[MAX_BUFFER];

    if(sigint_flag == 1){
        printf("SIGINT received. Thread ID: %lu is exiting...", threadID);
        pthread_exit(NULL);
    }

    memset(&entry, 0, sizeof(entry));


    pthread_mutex_lock(&active_t_mutex);
    if(isBufferEmpty() && producer_flag == 1){
        --active_threads;
        pthread_mutex_unlock(&active_t_mutex);
        pthread_exit(NULL);        
    }
    
    //printf("Active thread number %d\n",active_threads);

    pthread_mutex_unlock(&active_t_mutex);          
    /* Enqueue an entry from the buffer for copying */
    entry = dequeue();

    strcpy(fileName, entry.fileName);

    if(sigint_flag == 1){
        printf("SIGINT received. Thread PID: %lu is exiting...", threadID);
        pthread_exit(NULL);        
    }

    while ((bytes_read = read(entry.source_fd, buffer_write, BUFFER_SIZE)) > 0) {
        char* write_ptr = buffer_write;
        ssize_t remaining = bytes_read;

        while (remaining > 0) {
            bytes_written = write(entry.destination_fd, write_ptr, remaining);
            
            if(sigint_flag == 1){
                printf("SIGINT received. Thread PID: %lu is exiting...", threadID);
                pthread_exit(NULL);        
            }

            if (bytes_written == -1) {
                if (errno == EINTR)
                    continue;  
                else {
                    perror("write() error");
                    pthread_exit(NULL);
                }
            }

            if(sigint_flag == 1){
                printf("SIGINT received. Thread PID: %lu is exiting...", threadID);
                pthread_exit(NULL);        
            }

            /* add the written bytes to total bytes for file info */
            atomic_fetch_add(&total_bytes_copied, bytes_written);
            write_ptr += bytes_written;
            remaining -= bytes_written;
        }
    }

    if (bytes_read == -1) {
        perror("read() error");
        pthread_exit(NULL);
    }

    close(entry.destination_fd);
    close(entry.source_fd);

    /* print the file name and the thread info  */
    pthread_mutex_lock(&stdout_mutex);
    printf("File %s has been copied by thread %lu successfully.\n", fileName, threadID);
    pthread_mutex_unlock(&stdout_mutex);


    //printf("Thread %lu is exiting\n", threadID);
    /* decrease the number of active threads    */
    pthread_mutex_lock(&active_t_mutex);
    --active_threads;
    pthread_mutex_unlock(&active_t_mutex);

    pthread_exit(NULL);

}



/// @brief creates the worker threads and keeps the number of 
/// active threads a fixed amount (pool size)
/// @param args no argument
/// @return no return value
void* consumerCreate(void* args){

    while(true){
        pthread_t workerThread;

        /* if the producer has exited and the buffer is empty   */
        /* stop creating worker threds  */
        pthread_mutex_lock(&buffer.mutex);
        if(isBufferEmpty() && producer_flag == 1){
            pthread_mutex_unlock(&buffer.mutex);
            break;
        }

        pthread_mutex_unlock(&buffer.mutex);

        /* number of active threads is less than the pool size */
        /* create new worker thread */
        pthread_mutex_lock(&active_t_mutex);   
        if(active_threads < pool_size){
            pthread_create(&workerThread, NULL, consumer, NULL);
            pthread_detach(workerThread);
            ++active_threads; 
        }        
        pthread_mutex_unlock(&active_t_mutex);


    }
    while (true) {
        pthread_mutex_lock(&active_t_mutex);

        //printf("Active threads %d\n",active_threads);
        if (active_threads == 0) {
            pthread_mutex_unlock(&active_t_mutex);
            break;
        }
        pthread_mutex_unlock(&active_t_mutex);
    }
    printf("Manager exiting...\n");

    pthread_exit(NULL);

}
