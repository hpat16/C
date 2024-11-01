#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

int main() {
  int pipefd[2];
  pid_t child_pid;

  // Create a pipe
  if (pipe(pipefd) == -1) {
    perror("Pipe creation failed");
    exit(1);
  }

  // Fork a new process
  child_pid = fork();

  if (child_pid == -1) {
    perror("Fork failed");
    exit(1);
  }

  if (child_pid == 0) {
    // This code is executed by the child process

    // Close the write end of the pipe because it's not needed in the child
    close(pipefd[1]);

    char buffer[256];
    // Read data from the pipe
    ssize_t bytes_read = read(pipefd[0], buffer, sizeof(buffer));
    if (bytes_read == -1) {
      perror("Read from pipe failed");
      exit(1);
    }

    // Null-terminate the received data
    buffer[bytes_read] = '\0';

    printf("Child received: %s\n", buffer);

    // Close the read end of the pipe
    close(pipefd[0]);

    exit(0);
  } else {
    // This code is executed by the parent process

    // Close the read end of the pipe because it's not needed in the parent
    close(pipefd[0]);

    char message[] = "Hello, child!";

    // Write data to the pipe
    ssize_t bytes_written = write(pipefd[1], message, sizeof(message));
    if (bytes_written == -1) {
      perror("Write to pipe failed");
      exit(1);
    }

    // Close the write end of the pipe
    close(pipefd[1]);
  }

  return 0;
}

