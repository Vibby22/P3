#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // For isatty(), chdir(), fork(), execvp(), dup2()
#include <fcntl.h>   // For open()
#include <sys/wait.h>  // For wait()
#include <glob.h>    // For wildcard expansion

#define BUFFER_SIZE 1024

// Function prototypes
void handle_cd(char **tokens);
void handle_pwd();
void handle_which(char **tokens);
void handle_exit(char **tokens);
void execute_external_command(char **tokens, int input_fd, int output_fd);
char **tokenize_input(char *input);
void free_tokens(char **tokens);
void handle_pipes(char **tokens);
void expand_wildcards(char ***tokens_ptr);
int handle_redirection(char **tokens, int *output_fd);

// Function to handle changing the current directory
void handle_cd(char **tokens) {
    if (tokens[1] == NULL) {
        fprintf(stderr, "cd: missing argument\n");
    } else if (chdir(tokens[1]) != 0) {
        perror("cd");
    }
    fflush(stdout);
}

// Function to handle printing the current working directory
void handle_pwd() {
    char cwd[BUFFER_SIZE];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("pwd");
    } else {
        printf("%s\n", cwd);
    }
    fflush(stdout);
}

// Function to locate executable files
void handle_which(char **tokens) {
    if (tokens[1] == NULL) {
        fprintf(stderr, "which: missing argument\n");
    } else {
        const char *paths[] = {"/usr/local/bin", "/usr/bin", "/bin"};
        char path[BUFFER_SIZE];
        int found = 0;
        for (int i = 0; i < 3; i++) {
            snprintf(path, sizeof(path), "%s/%s", paths[i], tokens[1]);
            if (access(path, X_OK) == 0) {
                printf("%s\n", path);
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "which: command not found: %s\n", tokens[1]);
        }
    }
    fflush(stdout);
}

// Function to handle exiting the shell
void handle_exit(char **tokens) {
    if (tokens[1] != NULL) {
        printf("Exiting with message: %s\n", tokens[1]);
    }
    fflush(stdout);
    free_tokens(tokens);
    exit(0);
}

// Function to handle I/O redirection
int handle_redirection(char **tokens, int *output_fd) {
    for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i], ">") == 0) {
            if (tokens[i + 1] == NULL) {
                fprintf(stderr, "Syntax error: no file specified for output redirection\n");
                return -1;
            }

            *output_fd = open(tokens[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (*output_fd < 0) {
                perror("open");
                return -1;
            }

            free(tokens[i]);
            free(tokens[i + 1]);
            tokens[i] = NULL;
            tokens[i + 1] = NULL;
            return 1;
        }
    }
    return 0;
}

// Function to execute external commands
void execute_external_command(char **tokens, int input_fd, int output_fd) {
    int redirection = handle_redirection(tokens, &output_fd);
    if (redirection == -1) {
        return; // Error in handling redirection
    }

    // Compact the tokens array to remove NULLs left by redirection removal
    int j = 0;
    for (int i = 0; tokens[i] != NULL; i++) {
        if (tokens[i] != NULL) {
            tokens[j++] = tokens[i];
        }
    }
    tokens[j] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {  // Child process
        if (input_fd != STDIN_FILENO) {
            if (dup2(input_fd, STDIN_FILENO) == -1) {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
            close(input_fd);
        }
        if (output_fd != STDOUT_FILENO) {
            if (dup2(output_fd, STDOUT_FILENO) == -1) {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
            close(output_fd);
        }
        if (execvp(tokens[0], tokens) == -1) {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    } else {  // Parent process
        int status;
        waitpid(pid, &status, 0);  // Wait for the child process to complete
        if (input_fd != STDIN_FILENO) close(input_fd);
        if (output_fd != STDOUT_FILENO) close(output_fd);
    }
}

// Function to expand wildcards in commands
void expand_wildcards(char ***tokens_ptr) {
    char **tokens = *tokens_ptr;
    int new_size = 10, index = 0;
    char **expanded_tokens = malloc(new_size * sizeof(char *));
    if (!expanded_tokens) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    // List of files to exclude during wildcard expansion
    const char *exclusion_list[] = {"Makefile", NULL};

    for (int i = 0; tokens[i] != NULL; i++) {
        if (strchr(tokens[i], '*') || strchr(tokens[i], '?') || strchr(tokens[i], '[')) {
            glob_t glob_result;
            if (glob(tokens[i], GLOB_NOCHECK, NULL, &glob_result) == 0) {
                for (size_t j = 0; j < glob_result.gl_pathc; j++) {
                    int is_excluded = 0;
                    for (int k = 0; exclusion_list[k] != NULL; k++) {
                        if (strcmp(glob_result.gl_pathv[j], exclusion_list[k]) == 0) {
                            is_excluded = 1;
                            break;
                        }
                    }

                    if (!is_excluded) {
                        expanded_tokens[index++] = strdup(glob_result.gl_pathv[j]);
                        if (index >= new_size) {
                            new_size *= 2;
                            expanded_tokens = realloc(expanded_tokens, new_size * sizeof(char *));
                            if (!expanded_tokens) {
                                perror("realloc");
                                exit(EXIT_FAILURE);
                            }
                        }
                    }
                }
                globfree(&glob_result);
            }
        } else {
            expanded_tokens[index++] = strdup(tokens[i]);
            if (index >= new_size) {
                new_size *= 2;
                expanded_tokens = realloc(expanded_tokens, new_size * sizeof(char *));
                if (!expanded_tokens) {
                    perror("realloc");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }

    expanded_tokens[index] = NULL;

    // Free the old tokens
    free_tokens(tokens);

    // Replace the tokens pointer with the expanded version
    *tokens_ptr = expanded_tokens;
}

// Function to free memory allocated for tokens
void free_tokens(char **tokens) {
    if (!tokens) return;

    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
}

// Main function
int main(int argc, char *argv[]) {
    char buffer[BUFFER_SIZE];
    char **tokens;
    FILE *batch_file = NULL;
    int is_interactive = isatty(STDIN_FILENO);

    if (argc == 2) {
        batch_file = fopen(argv[1], "r");
        if (!batch_file) {
            perror("fopen");
            exit(EXIT_FAILURE);
        }
        is_interactive = 0;
    } else if (argc > 2) {
        fprintf(stderr, "Usage: %s [batch_file]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (is_interactive) {
        printf("Welcome to my shell!\n");
        fflush(stdout);
    }

    while (1) {
        if (is_interactive) {
            printf("mysh> ");
            fflush(stdout);
            if (!fgets(buffer, BUFFER_SIZE, stdin)) {
                break;  // End of input
            }
        } else if (batch_file) {
            if (!fgets(buffer, BUFFER_SIZE, batch_file)) {
                break;  // End of file
            }
        }

        buffer[strcspn(buffer, "\n")] = '\0';

        tokens = tokenize_input(buffer);
        expand_wildcards(&tokens);

        if (tokens[0] == NULL) {
            free_tokens(tokens);
            continue;
        }

        int contains_pipe = 0;
        for (int j = 0; tokens[j] != NULL; j++) {
            if (strcmp(tokens[j], "|") == 0) {
                contains_pipe = 1;
                break;
            }
        }

        if (contains_pipe) {
            handle_pipes(tokens);
        } else {
            if (strcmp(tokens[0], "cd") == 0) {
                handle_cd(tokens);
            } else if (strcmp(tokens[0], "pwd") == 0) {
                handle_pwd();
            } else if (strcmp(tokens[0], "which") == 0) {
                handle_which(tokens);
            } else if (strcmp(tokens[0], "exit") == 0) {
                handle_exit(tokens);
            } else {
                execute_external_command(tokens, STDIN_FILENO, STDOUT_FILENO);
            }
        }

        free_tokens(tokens);
    }

    if (batch_file) {
        fclose(batch_file);
        exit(0);
    }

    if (is_interactive) {
        printf("Exiting my shell.\n");
    }
    return 0;
}
