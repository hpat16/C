#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

void exec_command(char **args, int input_fd, int is_last_command) {
    int pipefd[2];
    if (!is_last_command) {
        pipe(pipefd);
    }

    pid_t pid = fork();

    if (pid == 0) {
        // Child process
        if (input_fd != STDIN_FILENO) {
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }

        if (!is_last_command) {
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[0]);
            close(pipefd[1]);
        }

        execvp(args[0], args);
    } else {
        // Parent process
        if (input_fd != STDIN_FILENO) {
            close(input_fd);
        }

        if (!is_last_command) {
            close(pipefd[1]);
            waitpid(pid, NULL, 0);
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
        } else {
            waitpid(pid, NULL, 0);
        }
    }
}

int main() {
    char *cat_args[] = {"cat", "f.txt", NULL};
    char *gzip_args[] = {"gzip", "-c", NULL};
    char *gunzip_args[] = {"gunzip", "-c", NULL};
    char *tail_args[] = {"tail", "-n", "10", NULL};

    exec_command(cat_args, STDIN_FILENO, 0);
    exec_command(gzip_args, STDIN_FILENO, 0);
    exec_command(gunzip_args, STDIN_FILENO, 0);
    exec_command(tail_args, STDIN_FILENO, 1);

    return 0;
}

