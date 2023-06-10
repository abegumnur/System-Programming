#include "utility.h"

int traverseDirectory(char* sourceDir, char* destinationDir);

void* producer(void* args){

    printf("Producer started...\n");

    // get the thread argumens (directories)
    threadArgs* arguments = (threadArgs* ) args;
    char originalDir[MAX_BUFFER];
    char destCopy[500];

    getcwd(originalDir, sizeof(originalDir));  // Save the original working directory

    // Check if the directory exists
    struct stat st;
    strncpy(destCopy, arguments->destinationDir, sizeof(destCopy));

    //if exists create the source directory inside it
    if (stat(arguments->destinationDir, &st) == 0 && S_ISDIR(st.st_mode)) {
        char sourceName[MAX_BUFFER];
        char *res = (strrchr(arguments->sourceDir, '/') + 1);
        
        strcat(destCopy, "/");

        if(res != NULL){
            strncpy(sourceName, res, MAX_BUFFER);
            strncat(destCopy, sourceName, sizeof(destCopy) - strlen(destCopy) - 1);
        }

        else
            strncat(destCopy, arguments->sourceDir, sizeof(destCopy) - strlen(destCopy) - 1);
        
        if(mkdir(destCopy, 0700) != 0){
            printf("Failed to create the directory.\n");
            exit(EXIT_FAILURE);
        }
    } 
    else {
        // Directory does not exist, so create it
        if (mkdir(arguments->destinationDir, 0700) != 0) {
            printf("Failed to create the destination directory.\n");
            exit(EXIT_FAILURE);
        }
    }

    // change the cwd to source directory
    chdir(arguments->sourceDir);

    if(sigint_flag == 1){
        printf("SIGINT received. Terminating the program...\n");
        chdir(originalDir);
        pthread_exit(NULL);
    }

    // recursive function for traversing the directory entries
    if(!traverseDirectory(arguments->sourceDir, destCopy)){
        fprintf(stdout, "Error copying directory.\n");
        pthread_exit(NULL);
    }

    chdir(originalDir);  // Change back to the original working directory

    // set the producer flag for the consumer threads
    producer_flag = 1;

    printf("Producer exiting..\n");

    pthread_exit(NULL);

}



int traverseDirectory(char* sourceDir, char* destinationDir){


    DIR* directory = opendir(sourceDir);

    
    if (directory == NULL) {
        fprintf(stderr, "Failed to open source directory.\n");
        return 0;
    }

    struct dirent* entry;
    char destSubDir[MAX_BUFFER];
    char sourceSubDir[MAX_BUFFER];
    char fileName[MAX_BUFFER];
    char destPath[MAX_BUFFER];

    if(sigint_flag == 1){
        closedir(directory);
        return 0;
    }

    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_type == DT_DIR) { // Directory

            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            snprintf(destSubDir, sizeof(destSubDir), "%s/%s", destinationDir, entry->d_name);

            if(mkdir(destSubDir, 0777) != 0){
                pthread_mutex_lock(&stdout_mutex);
                perror("Error creating directory");
                pthread_mutex_unlock(&stdout_mutex);
                return 0;
            }
            
            snprintf(sourceSubDir, sizeof(sourceSubDir), "%s/%s", sourceDir, entry->d_name);
            char cwd[MAX_BUFFER];

            changeDirectory(sourceSubDir, cwd);
            char newDestinationDir[MAX_BUFFER + 100];

            if(sigint_flag == 1){
                closedir(directory);
                return 0;
            }

            snprintf(newDestinationDir, sizeof(newDestinationDir), "../%s", destSubDir);  
            /*  recursive call, get inside the subdirectory */
            traverseDirectory(".", newDestinationDir);
            chdir(cwd);

        } 
        else if (entry->d_type == DT_REG || entry->d_type == DT_FIFO) { // Regular file

            bufferEntry newEntry;
            char sourcePath[MAX_BUFFER];
            snprintf(sourcePath, sizeof(sourcePath), "%s/%s", sourceDir, entry->d_name);

            newEntry.source_fd = open(sourcePath, O_RDONLY | O_NONBLOCK);
            if(newEntry.source_fd == -1){
                pthread_mutex_lock(&stdout_mutex);
                fprintf(stdout, "Error opening source file: %s.\n", entry->d_name);
                pthread_mutex_unlock(&stdout_mutex);
                continue;
            }

            if(sigint_flag == 1){
                closedir(directory);
                return 0;
            }

            snprintf(destPath, sizeof(destPath), "%s/%s", destinationDir, entry->d_name);

            if(entry->d_type == DT_REG){
                newEntry.destination_fd = open(destPath, O_WRONLY | O_TRUNC | O_CREAT, 0644);
                if(newEntry.destination_fd == -1){

                    pthread_mutex_lock(&stdout_mutex);
                    fprintf(stdout, "Error opening destinaton file: %s.\n", destPath);
                    pthread_mutex_unlock(&stdout_mutex);
                    close(newEntry.source_fd);
                    continue;
                }

            }
            else if(entry->d_type == DT_FIFO){

                // Create the FIFO
                printf("Name of the fifo is %s\n", entry->d_name);

                int result = mkfifo(destPath, 0666);
                if (result == -1) {
                    pthread_mutex_lock(&stdout_mutex);
                    perror("Error creating named FIFO");
                    pthread_mutex_unlock(&stdout_mutex);
                    continue;
                }

                if(sigint_flag == 1) {
                    closedir(directory);
                    return 0;
                }

                // Open the FIFO for writing
                newEntry.destination_fd = open(destPath, O_RDWR | O_NONBLOCK);
                if (newEntry.destination_fd == -1) {
                    pthread_mutex_lock(&stdout_mutex);
                    perror("Error opening named FIFO for writing");
                    pthread_mutex_unlock(&stdout_mutex);
                    continue;
                }
            }

            /*  get the file extension information  */
            char extension[MAX_EXTENSION];
            char * ext = strrchr(entry->d_name, '.');

            /*  no info, keep it as unknown */
            if(ext == NULL){
                strcpy(extension, "unknown");
            }
            else 
                strcpy(extension, ext + 1);
                        
            updateFileInfo(extension);

            snprintf(fileName, sizeof(fileName), "%s/%s", sourceDir, entry->d_name);
            strncpy(newEntry.fileName, fileName, sizeof(newEntry.fileName));
            
            enqueue(&newEntry);

        }
    }

    closedir(directory);
    return 1;


}
