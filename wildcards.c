// wildcards.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>  // For directory operations
#include <fnmatch.h> // For wildcard pattern matching

#include "wildcards.h"

// Expands wildcards in the given tokens array
void expand_wildcards(char ***tokens_ptr) {
    char **tokens = *tokens_ptr;
    char **expanded = NULL;
    int expanded_count = 0;

    for (int i = 0; tokens[i] != NULL; i++) {
        if (strchr(tokens[i], '*')) {  // Check for wildcard
            DIR *dir = opendir(".");
            if (!dir) {
                perror("opendir");
                continue;
            }

            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (fnmatch(tokens[i], entry->d_name, 0) == 0) {  // Match pattern
                    expanded = realloc(expanded, (expanded_count + 1) * sizeof(char *));
                    if (!expanded) {
                        perror("realloc");
                        closedir(dir);
                        return;
                    }
                    expanded[expanded_count++] = strdup(entry->d_name);
                }
            }
            closedir(dir);

            if (expanded_count > 0) {
                free(tokens[i]);  // Free the original wildcard token
                tokens[i] = expanded[0];  // Replace with the first match
                for (int j = 1; j < expanded_count; j++) {
                    tokens = realloc(tokens, (i + j + 1) * sizeof(char *));
                    if (!tokens) {
                        perror("realloc");
                        return;
                    }
                    tokens[i + j] = expanded[j];
                }
                tokens[i + expanded_count] = NULL;  // Null-terminate the array
            } else {
                fprintf(stderr, "No matches for wildcard: %s\n", tokens[i]);
            }
        }
    }

    *tokens_ptr = tokens;  // Update the pointer to the modified array
}
