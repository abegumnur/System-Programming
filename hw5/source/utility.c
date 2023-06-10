
#include "utility.h"

volatile sig_atomic_t sigint_flag = 0;
pthread_mutex_t stdout_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int producer_flag = 0;
atomic_int total_bytes_copied = 0;
atomic_int total_files = 0;   
atomic_int file_type_count = 0;
int buffer_size;
FileType fileTypes[MAX_FILES];
Buffer buffer;
int pool_size = 0;

/* functions related to fileType    */
void initializeFileTypes(){
    
    for(int i = 0; i < MAX_FILES; i++){
        strcpy(fileTypes[i].extension, "");
        fileTypes[i].count = 0;
    }

}

void updateFileInfo(char* extension){

    // Check if the file extension is already recorded
    int j;
    int found = 0;
    for (j = 0; j < file_type_count; j++) {
        if (strcmp(fileTypes[j].extension, extension) == 0) {
            fileTypes[j].count++;
            found = 1;
            break;
        }
    }

    // If the file extension is not found, add it to the file types dictionary
    if (!found) {
        strcpy(fileTypes[file_type_count].extension, extension);
        fileTypes[file_type_count].count = 1;
        atomic_fetch_add(&file_type_count, 1);
    }
    // Increment the total file counts
    atomic_fetch_add(&total_files, 1);

}

void printFileStatistics() {
    printf("File Statistics:\n");
    printf("Total Files: %d\n", total_files);

    printf("\nFile Types:\n");
    for (int i = 0; i < file_type_count; i++) {
        printf("Extension: %-5s, Count: %d\n", fileTypes[i].extension, fileTypes[i].count);
    }
    printf("Total of %d bytes copied.\n", total_bytes_copied);
}


/* functions related to buffer  */
void initializeBuffer(){    
    buffer.entry = (bufferEntry*)malloc(buffer_size* sizeof(bufferEntry));
    for (int i = 0; i < buffer_size; i++) {
        buffer.entry[i].source_fd = -1;
        buffer.entry[i].destination_fd = -1;
        strcpy(buffer.entry[i].fileName, "");
    }
    pthread_mutex_init(&buffer.mutex, NULL);
    pthread_cond_init(&buffer.empty, NULL);
    pthread_cond_init(&buffer.full, NULL);
    buffer.rear = -1;
    buffer.front = -1;
}

bool isBufferEmpty(){
    return buffer.front == -1;
}

bool isBufferFull(){
    if (buffer.front == -1)
        return false; // Buffer is empty
    else if (buffer.front > buffer.rear)
        return (buffer.front == (buffer.rear + 1) % buffer_size);
    else
        return (buffer.rear - buffer.front) == (buffer_size - 1);
}


void enqueue(bufferEntry* theEntry){

    /*  lock the mutex, if queue is full wait on condition */
    pthread_mutex_lock(&buffer.mutex);

    while(isBufferFull()){
        pthread_cond_wait(&buffer.full, &buffer.mutex);
    }

    if(buffer.front == -1)
        buffer.front = 0;

    buffer.rear = (buffer.rear + 1) % buffer_size;
    buffer.entry[buffer.rear] = *theEntry;

    pthread_cond_signal(&buffer.empty);
    pthread_mutex_unlock(&buffer.mutex);

}

bufferEntry dequeue(){

    /*  lock the mutex, if queue is empty wait on condition */
    pthread_mutex_lock(&buffer.mutex);

    while(isBufferEmpty()){
        pthread_cond_wait(&buffer.empty, &buffer.mutex);
    }

    bufferEntry dequeued = buffer.entry[buffer.front];

    if(buffer.front == buffer.rear){
        buffer.front = -1;
        buffer.rear = -1;
    }
    else{
        buffer.front = (buffer.front + 1) % buffer_size;
    }

    pthread_cond_signal(&buffer.full);
    pthread_mutex_unlock(&buffer.mutex);

    return  dequeued;

}

void freeBuffer(){

    free(buffer.entry);
    pthread_mutex_destroy(&buffer.mutex);
    pthread_cond_destroy(&buffer.empty);
    pthread_cond_destroy(&buffer.full);

} 

void changeDirectory(char* newDir, char* oldDir) {
    // Get the current working directory
    if (getcwd(oldDir, MAX_BUFFER) == NULL) {
        perror("getcwd() error");
        return;
    }

    // Change the directory
    if (chdir(newDir) != 0) {
        perror("chdir() error");
        return;
    }
}