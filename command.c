#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

void handle_redirection(char **tokens, int *input_fd, int *output_fd) {
    for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i], "<") == 0) {
            if (tokens[i + 1] == NULL) {
                fprintf(stderr, "Syntax error: no file specified for input redirection\n");
                return;
            }
            *input_fd = open(tokens[i + 1], O_RDONLY);
            if (*input_fd < 0) {
                perror("open input file");
                return;
            }
            free(tokens[i]);
            free(tokens[i + 1]);
            tokens[i] = NULL;
            tokens[i + 1] = NULL;
        } else if (strcmp(tokens[i], ">") == 0) {
            if (tokens[i + 1] == NULL) {
                fprintf(stderr, "Syntax error: no file specified for output redirection\n");
                return;
            }
            *output_fd = open(tokens[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (*output_fd < 0) {
                perror("open output file");
                return;
            }
            free(tokens[i]);
            free(tokens[i + 1]);
            tokens[i] = NULL;
            tokens[i + 1] = NULL;
        }
    }
}
