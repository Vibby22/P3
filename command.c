#include "command.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <string.h>

void execute_external_command(char **tokens) {
    int input_fd = -1, output_fd = -1;
    handle_redirection(tokens, &input_fd, &output_fd);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        if (input_fd != -1) {
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }
        if (output_fd != -1) {
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }
        execvp(tokens[0], tokens);
        perror("execvp");
        exit(EXIT_FAILURE);
    } else {
        wait(NULL);
    }
}

void handle_redirection(char **tokens, int *input_fd, int *output_fd) {
    for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens &#8203;:contentReference[oaicite:0]{index=0}&#8203;
