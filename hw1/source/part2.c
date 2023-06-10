#include<fcntl.h>
#include<errno.h>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>

#define MAX 50

int dup(int old_fd){

	/* verify that oldfd is a valid file descriptor */
	if (fcntl(old_fd, F_GETFD) < 0) {
		errno = EBADF;
		return -1;
	}

    /* hand things off to fcntl */
    return fcntl(old_fd, F_DUPFD, 0);
}

int dup2(int oldfd, int newfd)
{
	/* verify that oldfd is a valid file descriptor */
	if (fcntl(oldfd, F_GETFD) < 0) {
		errno = EBADF;
		return -1;
	}

	/* Check to see if oldfd == newfd */
	if (oldfd == newfd) {
		return newfd; /* no need to do anything */
	}

	/* Check to see if newfd is open */
	int ret = fcntl(newfd, F_GETFD);

	if (ret < 0 && errno != EBADF) {
		return -1;
	} 
    else if (ret >= 0) {
		if(close(newfd) == -1){
            perror("Close");
            exit(EXIT_FAILURE);
        } 
	}

	/* Hand things off to fcntl */
	return fcntl(oldfd, F_DUPFD, newfd);
}

int main(int argc, char *argv[])
{
	int fd1, fd2;
    int bytes_written = 0, bytes_to_write = 0;
    int i = 0;
	char buf[MAX];
    char write_buf[MAX] = "This is a test for part 2.\n";
    char out_buf[MAX] = "This will write to file not terminal.\n";

    /* emptying out the buffer */
	memset(buf, '\0', sizeof(buf));

    if(argc != 2){
        printf("Usage: <filename> \n");
        exit(EXIT_FAILURE);
    }

	fd1 = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IXUSR);
	
    if(fd1 == -1){
        perror("Open");
        printf("errno is set to %d",errno);
        exit(EXIT_FAILURE);

    }
    
    bytes_to_write = sizeof(write_buf);
    while(bytes_to_write > 0){
        
        while((bytes_written = write(fd1, &write_buf[i], sizeof(write_buf))) == -1 &&
               ( errno == EINTR )) ; 
        
        if(bytes_written < 0)
            break;

        bytes_to_write -= bytes_written;
        i += bytes_written;

    }
    
    /* Duplicating the file descriptor */
	fd2 = dup(fd1);

    /* Setting the file offset to the beginning of the file */
    off_t lseek_success = lseek(fd2, 0, SEEK_SET);
    if(lseek_success == -1){
        perror("Lseek");
        exit(EXIT_FAILURE);
    }

    /* This will read the original file contents 
        with the help of the duplicated file descriptor */
    int read_success = read(fd2, buf, sizeof(buf));

    if(read_success == -1){
        perror("Read");
        exit(EXIT_FAILURE);
    }

    /* Print the file contents */
    printf("%s", buf);

    /* The new file descriptor is set to STDOUT*/
    int dup2_fd = dup2(fd1, STDOUT_FILENO);

    if(dup2_fd < 0){
        perror("Dup2()");
        exit(EXIT_FAILURE);
    }

    /* This will write to the file instead of the terminal */
    printf("%s", &out_buf[0]);
    

    if(	(close(fd1) == -1 ) || ( close(fd2) == -1 ) || ( close(dup2_fd) == -1 ) ){
        perror("Close");
        exit(EXIT_FAILURE);
    }

	return 0;
}



