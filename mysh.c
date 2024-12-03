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
void handle_redirection(char** tokens, int *input_fd, int *output_fd);

void handle_cd(char **tokens) {
    if (tokens[1] == NULL) {
        fprintf(stderr, "cd: missing argument\n");
    } else if (chdir(tokens[1]) != 0) {
        perror("cd");
    }
    printf("\n");
    fflush(stdout);
}

void handle_pwd() {
    char cwd[BUFFER_SIZE];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("pwd");
    } else {
        printf("%s\n", cwd);
    }
    fflush(stdout);
}

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
    printf("\n");
    fflush(stdout);
}

void handle_exit(char **tokens) {
    if (tokens[1] != NULL) {
        printf("Exiting with message: %s\n", tokens[1]);
    }
    fflush(stdout);
    free_tokens(tokens);
    exit(0);
}

void handle_redirection(char **tokens, int *input_fd, int *output_fd) {
    // Iterate through tokens to find redirection symbols
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
            tokens[i] = NULL;  // Mark as removed
            tokens[i + 1] = NULL;
        } else if (strcmp(tokens[i], ">") == 0) {
            if (tokens[i + 1] == NULL) {
                fprintf(stderr, "Syntax error: no file specified for output redirection\n");
                return;
            }
            *output_fd = open(tokens[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0640);
            if (*output_fd < 0) {
                perror("open output file");
                return;
            }
            free(tokens[i]);
            free(tokens[i + 1]);
            tokens[i] = NULL;  // Mark as removed
            tokens[i + 1] = NULL;
        } else if (strcmp(tokens[i], ">>") == 0) {
            if (tokens[i + 1] == NULL) {
                fprintf(stderr, "Syntax error: no file specified for output redirection\n");
                return;
            }
            *output_fd = open(tokens[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0640);
            if (*output_fd < 0) {
                perror("open output file");
                return;
            }
            free(tokens[i]);
            free(tokens[i + 1]);
            tokens[i] = NULL;  // Mark as removed
            tokens[i + 1] = NULL;
        }
    }

    // Compact the tokens array to remove NULLs left by redirection removal
    int j = 0;
    for (int i = 0; tokens[i] != NULL || tokens[i + 1] != NULL; i++) {
        if (tokens[i] != NULL) {
            tokens[j++] = tokens[i];
        }
    }
    tokens[j] = NULL;
}

void execute_external_command(char **tokens, int input_fd, int output_fd) {
    // Initialize file descriptors for redirection
    input_fd = STDIN_FILENO;
    output_fd = STDOUT_FILENO;

    handle_redirection(tokens, &input_fd, &output_fd);

    // Fork a new process to execute the command
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {  // Child process
        if (input_fd != STDIN_FILENO) {  // Redirect input if needed
            if (dup2(input_fd, STDIN_FILENO) == -1) {
                perror("dup2 input");
                exit(EXIT_FAILURE);
            }
            close(input_fd);
        }
        if (output_fd != STDOUT_FILENO) {  // Redirect output if needed
            if (dup2(output_fd, STDOUT_FILENO) == -1) {
                perror("dup2 output");
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
        waitpid(pid, &status, 0);

        // Close the file descriptors only if they were changed
        if (input_fd != STDIN_FILENO) {
            close(input_fd);
        }
        if (output_fd != STDOUT_FILENO) {
            close(output_fd);
        }
    }
}


void handle_pipes(char **tokens) {
    int pipe_fd[2];
    int prev_read_end = -1;
    int i = 0;

    while (i < BUFFER_SIZE && tokens[i] != NULL) {
        // Collect tokens for the current command
        char *current_command[BUFFER_SIZE];
        int cmd_index = 0;

        while (tokens[i] != NULL && strcmp(tokens[i], "|") != 0) {
            current_command[cmd_index++] = tokens[i++];
        }
        current_command[cmd_index] = NULL;

        // Create a pipe if there is a next command
        if (tokens[i] != NULL && strcmp(tokens[i], "|") == 0) {
            if (pipe(pipe_fd) < 0) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }

        // Fork a new process to execute the current command
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {  // Child process
            if (prev_read_end != -1) {  // Redirect input from the previous pipe
                if (dup2(prev_read_end, STDIN_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(prev_read_end);
            }
            if (tokens[i] != NULL && strcmp(tokens[i], "|") == 0) {  // Redirect output to the current pipe
                if (dup2(pipe_fd[1], STDOUT_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(pipe_fd[0]);
                close(pipe_fd[1]);
            }
            if (execvp(current_command[0], current_command) == -1) {
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        } else {  // Parent process
            if (prev_read_end != -1) {
                close(prev_read_end);
            }
            if (tokens[i] != NULL && strcmp(tokens[i], "|") == 0) {
                prev_read_end = pipe_fd[0];
                close(pipe_fd[1]);
            }
        }

        // Move to the next token after '|'
        if (tokens[i] != NULL && strcmp(tokens[i], "|") == 0) {
            i++;
        }
    }

    // Wait for all child processes to complete
    int status;
    while (wait(&status) > 0);
}

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

void expand_wildcards(char ***tokens_ptr) {
    char **tokens = *tokens_ptr;
    int new_size = 10, index = 0;
    char **expanded_tokens = malloc(new_size * sizeof(char *));
    if (!expanded_tokens) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; tokens[i] != NULL; i++) {
        if (strchr(tokens[i], '*') || strchr(tokens[i], '?') || strchr(tokens[i], '[')) {
            glob_t glob_result;
            if (glob(tokens[i], GLOB_NOCHECK, NULL, &glob_result) == 0) {
                for (size_t j = 0; j < glob_result.gl_pathc; j++) {
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

    free_tokens(tokens);
    *tokens_ptr = expanded_tokens;
}

void free_tokens(char **tokens) {
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
}

int main(int argc, char *argv[]) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    char **tokens;
    int is_interactive = isatty(STDIN_FILENO);  // Check if input is from a terminal
    int input_fd = STDIN_FILENO;               // Default to standard input
    int batch_mode = 0;

    // Check for batch file input
    if (argc == 2) {
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd < 0) {
            perror("open");
            exit(EXIT_FAILURE);
        }
        is_interactive = 0;  // Batch mode disables interactive behavior
        batch_mode = 1;      // Set batch mode flag
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
        }

        // Read input line by line in batch mode
        if (batch_mode) {
            bytes_read = read(input_fd, buffer, BUFFER_SIZE - 1);
            if (bytes_read <= 0) {
                break;  // End of input or error
            }

            buffer[bytes_read] = '\0';
            char *line = strtok(buffer, "\n");
            while (line != NULL) {
                tokens = tokenize_input(line);
                expand_wildcards(&tokens);

                if (tokens[0] != NULL) {
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
                }

                free_tokens(tokens);
                line = strtok(NULL, "\n");
                if (batch_mode) {
                    printf("\n");
                    fflush(stdout);
                }
            }
        } else {
            bytes_read = read(input_fd, buffer, BUFFER_SIZE - 1);
            if (bytes_read < 0) {
                perror("read");
                break;
            } else if (bytes_read == 0) {
                // End of input
                break;
            }

            buffer[bytes_read] = '\0';
            tokens = tokenize_input(buffer);

            // Expand any wildcards in the tokens
            expand_wildcards(&tokens);

            if (tokens[0] == NULL) {  // Skip empty input
                free_tokens(tokens);
                continue;
            }

            // Handle pipes if present
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
    }

    // If batch mode, exit after finishing
    if (batch_mode) {
        close(input_fd);
        exit(0);
    }

    if (is_interactive) {
        printf("Exiting my shell.\n");
    }
    return 0;
}
