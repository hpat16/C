#include <stdio.h>
#include <unistd.h> 
#include <fcntl.h>
#include <sys/types.h> 
#include <sys/stat.h> 
#include <sys/wait.h>
int main(int argc, char *argv[])
{   
    char *argums[] = {"cat", "scores.txt", NULL, "gzip", "-c", NULL, "gunzip", "-c", NULL, "tail", "-n", "10", NULL};
    int locs[] = {0, 3, 6, 9};
    int count = 4;
    int pipefd[2];
    pipe(pipefd);
    int pid = fork();
    if (pid == 0)
    {
        for (int i = 0; i < count - 1; i++ ) {
            int grandChild = fork();
            if ( grandChild == 0 ) {
                if ( pipefd[0] != STDIN_FILENO ) {
                    dup2(pipefd[0], STDIN_FILENO);
                    close(pipefd[0]);
                }

                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);
                // execute
                char **curr_args = argums + locs[i]; 
                execvp(curr_args[0], curr_args);
            } else {
                close(pipefd[1]);
                dup2(pipefd[0], STDIN_FILENO);
                close(pipefd[0]);
                int status;
                waitpid(grandChild, &status, 0);
            }
        }

        if ( pipefd[0] != STDIN_FILENO ) {
			dup2(pipefd[0], STDIN_FILENO);
			close(pipefd[0]);
		}
        //close(pipefd[1]);
        char **final_args = argums + locs[count - 1];
        execvp(final_args[0], final_args);
    } else {
        close(pipefd[0]);
        close(pipefd[1]);
        int status;
        waitpid(pid, &status, 0);
    }
    
    return 0;
}
