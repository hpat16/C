#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int histSize = 5;

typedef struct shellVar {
    char *name;
    char *value;
    struct shellVar *nextVar;
} shellVar;

int parseCommand ( char **argums, char *command ) {
	char *currArg = command;
	char *memptr = command;
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
    shellVar *shellVarsList = NULL;
    char **history = calloc(sizeof(char *), histSize); 

    /*if (argc != 2) {
      fprintf(stderr, "Usage: %s <file>\n", argv[0]);
      exit(EXIT_FAILURE);
    }*/
  
    while (1) {
        printf("wsh> "); 
        if ( (nread = getline(&line, &len, stdin)) != -1 ) {
            line[strcspn(line, "\n")] = '\0';
            /*char *argums[15];
			char *currLine = strdup(line);
			char *currArg = currLine;
			char *memptr = currLine;
			int argcount = 0;
			argums[argcount++] =  strsep(&currLine, " ");
			while ( (currArg = strsep(&currLine, " ")) != NULL ) {
				argums[argcount++] = currArg;
			}

			argums[argcount--] = NULL;*/ 
            // parse user input into command args                                                                                                                                                       
			char *argums[15];
            char *memptr = strdup(line);
		    int argcount = parseCommand ( argums, memptr );
            // handle exit
            if ( strcmp(argums[0], "exit") == 0 ) {
                if ( argcount != 0 ) {
                    printf("error: exit does not support arguments\n");
                } else {
                    // free memory and exit
                    shellVar *currVar = shellVarsList;
                    while ( shellVarsList != NULL ) {
                        shellVarsList = currVar->nextVar;
                        free(currVar->name);
                        free(currVar->value);
                        free(currVar);
                        currVar = shellVarsList;
                    }
                
                    free(line);
                    exit(0);
                }

                continue;
            }

            int status;
	        pid_t rc = fork();
	        if (rc < 0) {
		        printf("fork failed\n");
		        exit(1);
	        } else if (rc == 0) { // child
		        if ( execvp(argums[0], argums) < 0) {
			        exit(1);
		        }
	        } else { // parent                                                                                                                                                                   
		        waitpid(rc, &status, 0);
		        if ( WIFEXITED(status) ) {
			        if ( WEXITSTATUS(status) == 1) {
				        printf("execvp: No such file or directory\n");
			        }
		        } else if (WIFSIGNALED(status)) {
			        printf("Child process terminated by signal %d\n", WTERMSIG(status));
		        }
	        }
        }
    }
}
