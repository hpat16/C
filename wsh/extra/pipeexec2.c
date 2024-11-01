#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main() {
    char *argums[] = {"echo", "this_line_2", NULL, "tee", "-a", "test2", NULL};
    int locs[] = {0, 3};
    int count = 2;
    int pipefd[2];

    // Start the first command
    int input_fd = STDIN_FILENO;
    for (int i = 0; i < count; i++) {
        pipe(pipefd);
        int pid = fork();

        if (pid == -1) {
            perror("fork");
            return 1;
        } else if (pid == 0) {
            // Child process
            if (input_fd != STDIN_FILENO) {
                dup2(input_fd, STDIN_FILENO);
                close(input_fd);
            }

            if (i < count - 1) {
                // Not the last command
                dup2(pipefd[1], STDOUT_FILENO);
            }

            // Close pipe file descriptors
            close(pipefd[0]);
            close(pipefd[1]);

            // Execute command
            char **curr_args = argums + locs[i];
            execvp(curr_args[0], curr_args);
            perror("execvp");
            _exit(1);
        } else {
            // Parent process
            // Close write end of the pipe
            close(pipefd[1]);

            // Update input_fd for next command
            input_fd = pipefd[0];

            // Wait for the child process to finish
            waitpid(pid, NULL, 0);
        }
    }

    return 0;
}

