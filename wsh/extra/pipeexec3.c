#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>

void execPipeCommands ( char **argums, int *commandIndices, int totalCommands ) {
    int pipefd[2];
    int input_fd = STDIN_FILENO; // tracks input from one command to another
    for (int i = 0; i < totalCommands; i++) {
        pipe(pipefd);
        int child_pid = fork();

        if (child_pid == -1) {
            printf("fork failed\n");
		    exit(1);
        } else if (child_pid == 0) { // child
            if (input_fd != STDIN_FILENO) {
                dup2(input_fd, STDIN_FILENO);
                close(input_fd);
            }
            // if not last command, redirect output
            if (i < totalCommands - 1) {
                dup2(pipefd[1], STDOUT_FILENO);
            }

            close(pipefd[0]);
            close(pipefd[1]);
            // execute command
            char **curr_args = argums + commandIndices[i];
            if ( execvp(curr_args[0], curr_args) != 0 ) {
                printf("execvp failed\n");
                exit(1);
            }
        } else { // parent 
            close(pipefd[1]); // Close write end of the pipe
            input_fd = pipefd[0]; // Update input_fd for next command
            waitpid(child_pid, NULL, 0);
        }
    }
}

int parsePipes ( char **argums, int argcount, int *commandIndices ) {
    int totalCommands = 1;
    commandIndices[0] = 0;
    for ( int i = 0; i < argcount + 1; i++ ) {
        if ( strcmp(argums[i], "|") == 0 ) {
            commandIndices[totalCommands++] = i + 1;
            argums[i] = NULL;
        }
    }

    return totalCommands;
}

int parseCommand ( char **argums, char *command ) {
	char *currArg = command;
	int argcount = 0;
	argums[argcount++] =  strsep(&command, " ");
	while ( (currArg = strsep(&command, " ")) != NULL ) {
		argums[argcount++] = currArg;
	}

	argums[argcount--] = NULL;
    return argcount;
}

int main(int argc, char *argv[]) {
    char *line = NULL;
    size_t len = 0;
    ssize_t nread; 
    FILE *input_fp = stdin;
  
    while (1) {
        if ( input_fp == stdin ) {
            printf("wsh> "); 
        }

        if ( (nread = getline(&line, &len, input_fp)) != -1 ) {
            line[strcspn(line, "\n")] = '\0';
            // parse user input into command args                                                                                                                                                       
			char *argums[100];
			char *memptr = strdup(line); // saved to free later
			int argcount = parseCommand( argums, memptr ); 
            // check for pipes in input and handle if any
            int commandIndices[argcount];
            int totalCommands = parsePipes( argums, argcount, commandIndices );
            printf("%d\n", totalCommands);
            for (int i = 0; i < totalCommands; i++ ) {
               printf("%d\n", commandIndices[i]);
            }
            if ( totalCommands != 0 ) {
                execPipeCommands( argums, commandIndices, totalCommands );
            }
            // handle exit
            else if ( strcmp(argums[0], "exit") == 0 ) {
                if ( argcount != 0 ) {
                    printf("error: exit does not support arguments\n");
                } else {
                    exit(0);
                }
            } 
        }
    }
}
