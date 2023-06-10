#include "shell.h"

/* signal handler for SIGINT and SIGTERM*/
void signalHandler(int signal){ // SET THE SIGNAL HANDLER SLIDES AND SIGNAL INT 0LA

    if(signal == SIGINT || signal == SIGTERM)
        RECEIVED_SIGNAL = signal;

    return;
}

/* logs the child processes to a single file after every given line*/
void log_child_processes(pid_t pids[], char* commands[MAX_ARGS][MAX_ARGS], int num_commands){
    
    //generate timestamp for log filename
    time_t current_time = time(NULL);
    char log_filename[MAX_ARGS];
    strftime(log_filename, sizeof(log_filename), "%Y%m%d%H%M%S.log", localtime(&current_time));

    //open logfile for writing
    FILE* log_file = fopen(log_filename, "w");

    if(log_file == NULL){
        perror("Error opening log file");
        exit(EXIT_FAILURE);
    }
    
    //write the child process information
    fprintf(log_file, "Child process information: \n");
    for(int i = 0; i < num_commands; i++){
        /* write the command of the child process*/
        fprintf(log_file, "PID: %d\tCommand: ", pids[i]);
        for(int k = 0; k < MAX_ARGS && commands[i][k] != NULL; k++){
            fprintf(log_file, " %s ", commands[i][k]);
        }
        fprintf(log_file, "\n");
    }

    //closing the log file
    fclose(log_file);

}

/* free the allocated memory for the commands*/
void free_memory(char* commands[MAX_ARGS][MAX_ARGS], int num_commands){

    for (int i = 0; i < num_commands; i++) 
        for (int k = 0; commands[i][k] != NULL; k++) 
            free(commands[i][k]);

}

/* split the given input to commands*/
/* 
    For example: "ls -l | echo me"
    is split into 2 commands
    command 1: "ls -l"
    command 2: "echo me"

*/
int split_into_commands(char* line, char* command[MAX_ARGS]){

    int num_command = 0;

    char* token = strtok(line, "|");

    while(token != NULL){

        command[num_command] = token;
        num_command++;
        token = strtok(NULL, "|");

    }

    command[num_command] = NULL;

    return num_command;
}


/* execute a single command and handle redirection */
void execute_command(char* command[MAX_ARGS]){

    int input_fd, output_fd; /* file descriptors for (in/out)put redirection*/

    /* handling the redirection part*/
    for(int i = 0; i < MAX_ARGS - 1 && command[i] != NULL; i++){
        if( strcmp(command[i], "<") == 0){
            input_fd = open(command[i + 1], O_RDONLY);
            
            if(input_fd == -1){
                perror("Open");
                exit(EXIT_FAILURE);
            }
            if(dup2(input_fd, STDIN_FILENO) == -1){
                perror("Dup2");
                exit(EXIT_FAILURE);
            }
            /* close the unused file descriptor*/
            close(input_fd);
            command[i] = NULL;
        }
        
        else if( strcmp(command[i], ">") == 0){
            output_fd = open(command[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0666);

            if( output_fd == -1){
                perror("Open");
                exit(EXIT_FAILURE);
            }

            if(dup2(output_fd,  STDOUT_FILENO) == -1){
                perror("Dup2");
                exit(EXIT_FAILURE);
            }

            close(output_fd);
            command[i] = NULL;

        }
    }
    /* hand things off to execvp()*/
    if(execvp(command[0], command) == -1){
        perror("Ecexl");
        exit(EXIT_FAILURE);
    }

}

void execute_pipeline(char* command[MAX_ARGS][MAX_ARGS], int num_pipes){

    pid_t pid[MAX_COMMAND];
    int fd[MAX_COMMAND - 1][2];

    /* create the pipes */
    for(int i = 0; i < num_pipes; i++){
        if(pipe(fd[i]) == -1){
            perror("Pipe");
            exit(EXIT_FAILURE);
        }
    }

    fflush(stdout);

    //fork child processes
    for(int i = 0; i <= num_pipes; i++){

        pid[i] = fork();

        switch (pid[i])
        {
        case -1:
            perror("Fork");
            exit(EXIT_FAILURE);
            break;
        case 0:
                //child process
            if(i > 0)   // not the first command, read the previous pipe
            {
                if(dup2(fd[i - 1][0], STDIN_FILENO) == -1){
                    perror("Dup2");
                    exit(EXIT_FAILURE);
                }
                
                if (close(fd[i - 1][0]) == -1)
                    perror("Close");
                if (close(fd[i - 1][1]) == -1)
                    perror("Close");
            }

            if(i < num_pipes) // if not the last command, write to next pipe
            {
                if(dup2(fd[i][1], STDOUT_FILENO) == -1){
                    perror("Dup2");
                    exit(EXIT_FAILURE);
                }
                if (close(fd[i][0]) == -1)
                    perror("Close");
                if (close(fd[i][1]) == -1)
                    perror("Close");

            }
            
            /* child process executes the given command */
            execute_command(command[i]);
            break;
                
                //parent process
        default:    /* close the pipes  */
            if(i > 0) {
                close(fd[i - 1][0]);
                close(fd[i - 1][1]);
            }
            break;
        }


    }   /* wait for all the child processes to terminate    */
        for(int i = 0; i <= num_pipes; i++){
        int status;

        if(waitpid(pid[i], &status, 0) == -1){
            perror("Waitpid");
            exit(EXIT_FAILURE);
        }

    }

    /*log the commands to a single file*/
    log_child_processes(pid, command, num_pipes + 1);

}

/* prompt and get the input line*/
int getline_prompt(char* line){

    printf(">");
    while(fgets(line, MAX_BUFFER, stdin) == NULL){
        
        if(errno == EINTR){
            /* fgets was interrupted by a signal */
            continue;
        }
        /* other type of error occured*/
        perror("Fgets");
        exit(EXIT_FAILURE);
    }

    line[strcspn(line, "\n")] = '\0';

    return 1;
}


int main(){

    char line[MAX_BUFFER]; 
    char* commands[MAX_ARGS][MAX_ARGS]; /* a line of commands*/
    char* command[MAX_ARGS];    /* a single command */
    int num_commands = 0;

    /* setting the signal handler*/
    struct sigaction sig_action;
    sig_action.sa_handler = signalHandler;
    sigemptyset(&sig_action.sa_mask);
    sig_action.sa_flags = 0;

    
    if(sigaction(SIGINT, &sig_action, NULL) == -1){
        perror("Error: can not add SIGINT to sigaction struct.");
        exit(EXIT_FAILURE);
    }

    if(sigaction(SIGTERM, &sig_action, NULL) == -1){
        perror("Error: can not add SIGTERM to sigaction struct.");
        exit(EXIT_FAILURE);
    }

    // Create new process group
    if (setpgid(0, 0) == -1) {
        perror("setpgid");
        exit(EXIT_FAILURE);
    }

    /* Shell instrutions*/
    printf("----- SHELL EMULATOR -----\n");
    printf("This shell can support bin/sh commands with ");
    printf("pipes and redirection.\n");
    printf("Please type \"[command] --help\" to ");
    printf("learn how to use a specific command.\n");
    printf("Please type \":q\" to exit the program.\n");

    /* shell loop*/
    while(1){

        /* signal is received   */
        if(RECEIVED_SIGNAL == SIGINT){
            printf("SIGINT received.\n");
            printf("Cleaning child processes and emptying the memory...\n");
            // DO THE CLEANING
            killpg(0, SIGTERM);
            free_memory(commands, num_commands);
            RECEIVED_SIGNAL = 0;
            continue;
        }

        if(RECEIVED_SIGNAL == SIGTERM){
            printf("SIGTERM received.\n");
            printf("Cleaning child processes and emptying the memory...\n");
            // DO THE CLEANING
            killpg(0, SIGTERM);
            free_memory(commands, num_commands);
            RECEIVED_SIGNAL = 0;
            continue;
        }

        /* print the prompt and get the input line*/
        getline_prompt(line);

        /*if ":q" is entered, exit the program*/
        if( strcmp(line, ":q") == 0)
            break;

        /*split the line into individual commands*/
        num_commands = split_into_commands(line, command);

        /* split the commands into 2d strings*/
        for(int i = 0; i < num_commands; i++){
            char * arg = strtok(command[i], TOKEN_SEP);
            int k = 0;

            while(arg != NULL){
                commands[i][k] = malloc(strlen(arg) + 1);
                strcpy(commands[i][k], arg);
                k++;
                arg = strtok(NULL, TOKEN_SEP);

            }
            commands[i][k] = NULL;

        }

        fflush(stdin);  
        
        /* number of pipes is 1 less than number of commands*/
        execute_pipeline(commands, num_commands - 1);
        
        /* free the allocated memory after each line of command*/
        free_memory(commands, num_commands);
    }


}


