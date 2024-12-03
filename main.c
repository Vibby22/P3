#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "builtins.h"
#include "command.h"
#include "wildcards.h"
#include "parser.h"

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    char **tokens;
    int is_interactive = isatty(STDIN_FILENO);
    int batch_mode = (argc == 2);
    int input_fd = STDIN_FILENO;

    if (batch_mode) {
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd < 0) {
            perror("open");
            return EXIT_FAILURE;
        }
        is_interactive = 0;
    }

    if (is_interactive) {
        printf("Welcome to my shell!\n");
    }

    while (1) {
        if (is_interactive) {
            printf("mysh> ");
            fflush(stdout);
        }

        bytes_read = read(input_fd, buffer, BUFFER_SIZE - 1);
        if (bytes_read <= 0) break;
        buffer[bytes_read] = '\0';

        tokens = tokenize_input(buffer);
        if (tokens[0] == NULL) {
            free_tokens(tokens);
            continue;
        }

        expand_wildcards(&tokens);

        if (strcmp(tokens[0], "cd") == 0) {
            handle_cd(tokens);
        } else if (strcmp(tokens[0], "pwd") == 0) {
            handle_pwd();
        } else if (strcmp(tokens[0], "which") == 0) {
            handle_which(tokens);
        } else if (strcmp(tokens[0], "exit") == 0) {
            handle_exit(tokens);
        } else {
            execute_external_command(tokens);
        }

        free_tokens(tokens);
    }

    if (batch_mode) {
        close(input_fd);
    }

    if (is_interactive) {
        printf("Exiting my shell.\n");
    }

    return 0;
}
