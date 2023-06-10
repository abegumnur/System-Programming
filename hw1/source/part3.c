#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#define SET 15

int main(int argc, char** argv){

    int fd1, fd2;
    off_t off_set;
    off_t off_set_fd2;

    if(argc != 2){
        printf("Usage: %s <filename>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    fd1 = open(argv[1], O_RDWR | O_CREAT);

    if(fd1 == -1){
        perror("Open");
        exit(EXIT_FAILURE);
    }

    fd2 = dup(fd1);

    off_set = lseek(fd1, SET, SEEK_SET);
    if(off_set == -1){
        perror("Lseek");
        exit(EXIT_FAILURE);
    }

    off_set_fd2 = lseek(fd2, 0, SEEK_CUR);

    if(off_set == off_set_fd2){
        printf("The offsets are the same.\n");
    } else{
        printf("The offsets are different.\n");
    }

    int fd1_flags = fcntl(fd1, F_GETFL);
    int fd2_flags = fcntl(fd2, F_GETFL);

    if(fd1_flags == fd2_flags){
        printf("Open file status are the same.\n");
        printf("The file status flags are %o.\n", fd2_flags);
    } else{
        printf("Open file status flags are different.\n");
        printf("The file status flags for descriptor %d are %o.\n", fd1, fd1_flags);
        printf("The file status flags for descriptor %d are %o.\n", fd2, fd2_flags);

    }


    if( (close(fd1) == 1) || (close(fd2) == -1)){
        perror("Close");
        exit(EXIT_FAILURE);
    }


    return 0;

}




