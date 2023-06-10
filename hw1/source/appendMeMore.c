#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include<errno.h>

int main(int argc, char *argv[]) {

    int flags;
    int use_lseek = 0;
    off_t offset;
    char *filename;
    char byte = 'B';
    long long int num_bytes;

    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: %s filename num-bytes [x]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    filename = argv[1];
    num_bytes = atoll(argv[2]);
    use_lseek = argc == 4;

    /*set the flags accorrding to the argument count */
    flags = use_lseek ? O_WRONLY : O_WRONLY | O_APPEND;

    int fd = open(filename, flags | O_CREAT, S_IRUSR | S_IWUSR);

    if (fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    

    for(long long int i = 0; i < num_bytes; ){

        if(use_lseek){

            offset = lseek(fd, 0, SEEK_END);
            if(offset == -1){
                perror("Lseek");
                exit(EXIT_FAILURE);
            }
        }

        int bytes_written = write(fd, &byte, 1);
        if (bytes_written == -1) {
            if (errno == EINTR) {
                continue; // write was interrupted, try again
            }

            perror("Write failed");
            exit(EXIT_FAILURE);
        }

        i += bytes_written;
    }
    


    

    if(close(fd) == -1){
        perror("Close");
        exit(EXIT_FAILURE);
    }

    return 0;
}