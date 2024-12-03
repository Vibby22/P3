// parser.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

// Tokenizes the input string into an array of tokens
char **tokenize_input(char *input) {
    int size = 10, index = 0;
    char **tokens = malloc(size * sizeof(char *));
    if (!tokens) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    char *token = strtok(input, " \t\n");
    while (token != NULL) {
        tokens[index++] = strdup(token);
        if (index >= size) {
            size *= 2;
            tokens = realloc(tokens, size * sizeof(char *));
            if (!tokens) {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
        }
        token = strtok(NULL, " \t\n");
    }
    tokens[index] = NULL;
    return tokens;
}

// Frees the memory allocated for tokens
void free_tokens(char **tokens) {
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
}
