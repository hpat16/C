#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>

typedef struct shellVar {
    char *name;
    char *value;
    struct shellVar *nextVar;
} shellVar;

// global var
int histSize = 5;

/**
 * This functions resizes the current history log to the requested size
 * It returns a ptr to the resized history list 
 */
char** resizeHistory ( char **hist, int newSize ) {
    // if new size is smaller than current, handle dropping commands
    if ( newSize < histSize ) {
        int entriesToKeep = newSize;
        char **tmp = calloc(sizeof(char *), newSize);
        // store commands to keep in tmp array
		for (int i = histSize - 1; i >= 0; i-- ) {
			if ( hist[i] != NULL && entriesToKeep > 0 ) {
				tmp[--entriesToKeep] = hist[i];
			}
		}
       // shrink array and transfer prev history from tmp array 
        hist = realloc(hist, sizeof(char *) * newSize);
        for (int i = 0; i < newSize; i++) {
            hist[i] = tmp[i];
        }
        
        free(tmp);
    }
    // if new size is larger, simply resize and clear new cells 
    else {
        hist = realloc(hist, sizeof(char *) * newSize);
        for ( int i = histSize; i < newSize; i++ ) {
            hist[i] = NULL;
        }
    }
    // update histSize
    histSize = newSize;
    return hist;
}

/**
 * This inserts the command passed in into the history log while ensuring
 * that not consecutively repeated command is logged (each adjacent command
 * in the log will be different)
 * 
 * The new command is inserted into the first empty cell from the beginning,
 * so the oldest command will be the first item in the list and the most recent
 * command will be the last item in the list
 */
void insertHistory ( char **histList, char *command ) {
    // check that history is being tracked
    if ( histSize > 0 ) {
        if ( histList[0] == NULL ) {
            histList[0] = command;
        }
        // if list is not empty iterate to find first free cell
        else {
            for ( int i = 1; i < histSize; i++ ) { 
                if ( histList[i] == NULL ) {
                    // check to ensure command isn't consecutive repeat
                    if ( strcmp(histList[i-1], command) != 0 ) {
                        histList[i] = command;
                    }
    
                    return;
                }
            }
            // reaching here means hist is full, so shift everything left and insert
            for ( int i = 1; i < histSize; i++ ) {
                histList[i - 1] = histList[i];
            }
            // insert to most recent command
            histList[histSize - 1] = command;
        }
    }
}

/**
 * This function inserts the given shell variable into a list of shell variables 
 */
void insertShellVar (shellVar **varsList, char *name, char *value) {
    // if empty list add new struct
    if ( *varsList == NULL ) {
        *varsList = malloc(sizeof(shellVar));
        (*varsList)->name = name;
        (*varsList)->value = value;
        (*varsList)->nextVar = NULL;
        return;
    } 
    // if non empty, search through or add new element
    else {
        shellVar *currVar = *varsList;
        // if new variable already exists, replace value
        if ( strcmp(currVar->name, name) == 0) {
            currVar->value = value;
            return;
        } 
        else {
            // searching to check is variable already exists
            while ( currVar->nextVar != NULL ) {
                if ( strcmp(currVar->name, name) == 0) {
                    currVar->value = value;
                    return;
                }

                currVar = currVar->nextVar;
            }
            // reaching here means new variable, add and link to list
            shellVar *newVar = malloc(sizeof(shellVar));
            newVar->name = name;
            newVar->value = value;
            newVar->nextVar = NULL;
            currVar->nextVar = newVar;
        }
    }
}

/**
 * This function executes any command that contains pipes
 */
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
            // ensure child reads from the correct fd 
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

/**
 * This function executes any non-builtin command that does not contain pipes
 */
void execCommand ( char **argums ) {
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

/**
 * This function checks if user command contains a variable (argument starting
 * with '$') and substitutes it if valid
 */
void subVariables ( char **argums, int *argcount, shellVar *shellVarsList ) {
    for (int i = 0; i < *argcount + 1; i++) {
		if ( argums[i][0] == '$' ) { // if arg is variable, replace it
			// check environment variables
			char *envValue = getenv(argums[i] + 1); 
			if ( envValue != NULL && envValue[0] != '\0' ) {
				argums[i] = envValue;
			}
			// check shell variables 
			else {
				shellVar *currVar = shellVarsList;
				while ( currVar != NULL ) {
					if ( strcmp(currVar->name, argums[i] + 1) == 0 ) {
						/* if the variable translates to an empty string, remove
                           it from command (handled @ line 225 */
						if ( (currVar->value)[0] == '\0' ) {
							break;
						} else { // replace variable name with actual value
							argums[i] = currVar->value;
						}

						break;
					}

					currVar = currVar->nextVar;
				}
			}
			// if variable doesnt exist or is empty, remove from command
			if ( argums[i][0] == '$' ) {
                // shifts arguments left, overwritting variable
				for ( int j = i; j < *argcount + 1; j++ ) {
					argums[j] = argums[j + 1];
				}
				
				i--;
				(*argcount)--;
			}
		}
	}
}

/**
 * This functions looks through parsed argums and reformats the it, removing
 * all pipes with null
 *
 * It also updates the commandIndices array, which holds the indexes to the 
 * program name of each command (indexes in the argums array)
 *
 * The function returns the total number of commands/programs inputted by user in
 * this line
 */
int parsePipes ( char **argums, int argcount, int *commandIndices ) {
    int totalCommands = 0;
    commandIndices[totalCommands++] = 0;
    for ( int i = 0; i < argcount + 1; i++ ) {
        // if argument is pipe, replace with NULL and update commandIndices
        if ( strcmp(argums[i], "|") == 0 ) {
            commandIndices[totalCommands++] = i + 1;
            argums[i] = NULL;
        }
    }

    // return total number of programs that this user input will run
    return totalCommands;
}

/**
 * This function parses raw user input (using space as delim) and stores it
 * in an array
 *
 * The function returns the total arguments (excluding the program name at 
 * index 0)
 */
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

/**
 * This main method drives the interactive loop, calling the functions defined
 * above depending on user input
 */
int main(int argc, char *argv[]) {
    char *line = NULL;
    size_t len = 0;
    ssize_t nread; 
    shellVar *shellVarsList = NULL; // list of shell variables
    char **history = calloc(sizeof(char *), histSize); // history log/list
    FILE *input_fp = stdin; // default file to read user input from

    // if batch mode, check to ensure valid file, open file to read from
    if (argc == 2) {
        if ( strcmp( (argv[1] + strlen(argv[1]) - 4), ".txt" ) ) {
            input_fp = fopen(argv[1], "r");
			if (input_fp == NULL) { 
				printf("ERROR: Can't open input file\n");
				exit(1); 
			}
        }
    }
  
    while (1) {
        // print only if in interactive mode (not batch)
        if ( input_fp == stdin ) {
            printf("wsh> "); 
        }

        if ( (nread = getline(&line, &len, input_fp)) != -1 ) {
            line[strcspn(line, "\n")] = '\0';
            // parse user input into command args                                                                                                                                                       
			char *argums[100];
			char *memptr = strdup(line); // saved to free later
			int argcount = parseCommand( argums, memptr );
            // handle $ in any args
            subVariables( argums, &argcount, shellVarsList );
            // check for pipes in input and handle if any
            int commandIndices[argcount];
            int totalCommands = parsePipes( argums, argcount, commandIndices );
            // pipe in command only if totalCommands is greater than 1
            if ( totalCommands > 1 ) {
                // insert command into history
                insertHistory( history, strdup(line) );
                // exec pipe properly
                execPipeCommands( argums, commandIndices, totalCommands );
            }
            // handle exit
            else if ( strcmp(argums[0], "exit") == 0 ) {
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

                    for ( int i = 0; i < histSize; i++ ) {
                        if ( history[i] != NULL ) {
                            free(history[i]);
                        }
                    }
                
                    free(history);
                    free(memptr);
                    free(line);
                    if ( input_fp != stdin ) {
                        if ( fclose(input_fp) != 0 ) {
							printf("failed to close input file\n");
							exit(1);
                        }
                    }

                    exit(0);
                }
            } 
            // handle cd
	        else if ( strcmp(argums[0], "cd") == 0 ) {
                if ( argcount != 1 ) {
                    printf("error, expected use: 'cd <path>'\n");
                } else if ( chdir(argums[1]) != 0 ) {
                    printf("error changing directory to %s\n", argums[1]);
                }
            }
            // handle export
            else if ( strcmp(argums[0], "export") == 0 ) {
                if ( argcount != 1 ) {
                    printf("error, expected use: 'export VAR=<value>'\n");
                } else {
                    // separate name and value
                    int index = strcspn(argums[1], "=");
                    argums[1][index] = '\0';
                    char *value = argums[1] + index + 1;         
                    if ( setenv(argums[1], value, 1) != 0 ) {
                        printf("setenv failed\n");
                        exit(1);
                    }
                }
            }
            // handle local
            else if ( strcmp(argums[0], "local") == 0 ) {
                if ( argcount != 1 ) {
                    printf("error, expected use: 'local VAR=<value>'\n");
                } else {
                    // separate name and value
                    int index = strcspn(argums[1], "=");
                    char *value;
                    if ( strlen(argums[1]) ==  index ) {
                        value = "";
                    } else {
                        argums[1][index] = '\0';
                        value = argums[1] + index + 1;
                    }

                    insertShellVar(&shellVarsList, strdup(argums[1]), strdup(value));
                }
            }
            // handle vars
            else if ( strcmp(argums[0], "vars") == 0 ) {
                shellVar *currVar = shellVarsList;
        		while ( currVar != NULL ) {
                    if ( (currVar->value)[0] != '\0' ) {
					    printf("%s=%s\n", currVar->name, currVar->value);
                    }
					currVar = currVar->nextVar;
				}
            }
            // handle history
            else if ( strcmp(argums[0], "history") == 0 ) {
                // print out option
                if ( argcount == 0 ) {
                    int num = 1;
                    for ( int i = histSize - 1; i >= 0; i-- ) {
                        if ( history[i] != NULL ) {
                            printf("%d) %s\n", num++, history[i]);
                        }
                    }
                } 
                // execute command option
                else if ( argcount == 1 ) {
                    int n = atoi(argums[1]);
                    if ( n <= 0 || n > histSize ) {
                        printf("index out of scope of history\n");
                    } else {
                        for ( int i = histSize - 1; i >= 0; i-- ) {
                            // if valid index in history, run command
                            if ( history[i] != NULL && --n == 0 ) {
                                free(memptr);
                                memptr = strdup(history[i]);
                                // parse command => sub variables (if any) => execute
                                argcount = parseCommand( argums, memptr );
                                subVariables ( argums, &argcount, shellVarsList );
                                // check for pipes in input and handle if any
                                int commandIndices_hist[argcount];
                                int totalCommands_hist = parsePipes( argums, argcount, commandIndices_hist );
                                // pipe in command only if totalCommands is greater than 1
                                if ( totalCommands > 1 ) {
                                    // exec pipe properly
                                    execPipeCommands( argums, commandIndices_hist, totalCommands_hist );
                                } else { // regular command
                                    execCommand ( argums );
                                }
                            }
                        }
                    }
                }
                // set history limit option 
                else if ( argcount == 2 && strcmp(argums[1], "set") == 0 ) {
                    int newSize = atoi(argums[2]);
                    if ( newSize < 0 ) { // ensure valid size
                        printf("invalid size to set history\n");
                    } else if ( newSize == 0 ) { 
                        // if set to 0, no need to touch list, 
                        // resizeHistory handles future requesst
                        histSize = 0;
                    } else {
                        history = resizeHistory( history, newSize );
                    }
                } else {
                    printf("invalid argument to history\n");
                }
            }
            // non-built-in commands
            else {
                // insert command into history
                insertHistory( history, strdup(line) );
                // handle exisiting commands 
			    execCommand( argums );
            }

            free(memptr);

        }
        // if EOF, free memory and exit
        if (feof(input_fp)) {
            // free memory and exit
			shellVar *currVar = shellVarsList;
			while ( shellVarsList != NULL ) {
				shellVarsList = currVar->nextVar;
				free(currVar->name);
				free(currVar->value);
				free(currVar);
				currVar = shellVarsList;
			}

			for ( int i = 0; i < histSize; i++ ) {
				if ( history[i] != NULL ) {
					free(history[i]);
				}
			}
		
			free(history);
			free(line);
			if ( input_fp != stdin ) {
				if ( fclose(input_fp) != 0 ) {
					printf("failed to close input file\n");
					exit(1);
				}
			}

            exit(0);
        }
    }
}
