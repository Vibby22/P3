#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // For chdir(), fork(), execvp(), dup2()
#include <fcntl.h>   // For open()
#include <sys/wait.h>  // For wait()
#include <glob.h>    // For wildcard expansion

#define BUFFER_SIZE 1024

// Function prototypes
void handle_cd(char **tokens);
void handle_pwd();
void handle_exit(char **tokens);
void execute_command(char **tokens);
char **tokenize_input(char *input);
void free_tokens(char **tokens);
void handle_pipes(char **tokens);
void expand_wildcards(char ***tokens_ptr);
int handle_redirection(char **tokens, int *input_fd, int *output_fd);

// Change current directory
void handle_cd(char **tokens) {
    if (tokens[1] == NULL) {
        fprintf(stderr, "cd: missing argument\n");
    } else if (chdir(tokens[1]) != 0) {
        perror("cd");
    }
}

// Print current directory
void handle_pwd() {
    char cwd[BUFFER_SIZE];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("pwd");
    } else {
        printf("%s\n", cwd);
    }
}

// Exit the shell
void handle_exit(char **tokens) {
    free_tokens(tokens);
    exit(0);
}

// Expand wildcards in command arguments
void expand_wildcards(char ***tokens_ptr) {
    char **tokens = *tokens_ptr;
    char **expanded_tokens = malloc(10 * sizeof(char *));
    if (!expanded_tokens) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    int index = 0, size = 10;

    for (int i = 0; tokens[i] != NULL; i++) {
        if (strpbrk(tokens[i], "*?[")) {
            glob_t glob_result;
            if (glob(tokens[i], 0, NULL, &glob_result) == 0) {
                for (size_t j = 0; j < glob_result.gl_pathc; j++) {
                    expanded_tokens[index++] = strdup(glob_result.gl_pathv[j]);
                    if (index >= size) {
                        size *= 2;
                        expanded_tokens = realloc(expanded_tokens, size * sizeof(char *));
                        if (!expanded_tokens) {
                            perror("realloc");
                            exit(EXIT_FAILURE);
                        }
                    }
                }
                globfree(&glob_result);
            }
            free(tokens[i]);
        } else {
            expanded_tokens[index++] = strdup(tokens[i]);
            if (index >= size) {
                size *= 2;
                expanded_tokens = realloc(expanded_tokens, size * sizeof(char *));
                if (!expanded_tokens) {
                    perror("realloc");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }

    expanded_tokens[index] = NULL;
    free(tokens);
    *tokens_ptr = expanded_tokens;
}

// Tokenize input string into command arguments
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

// Free memory allocated for tokens
void free_tokens(char **tokens) {
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
}

// Handle input/output redirection
int handle_redirection(char **tokens, int *input_fd, int *output_fd) {
    for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i], "<") == 0) {
            if (tokens[i + 1] == NULL) {
                fprintf(stderr, "Syntax error: no file specified for input redirection\n");
                return -1;
            }
            *input_fd = open(tokens[i + 1], O_RDONLY);
            if (*input_fd < 0) {
                perror("open");
                return -1;
            }
            free(tokens[i]);
            free(tokens[i + 1]);
            tokens[i] = NULL;
            tokens[i + 1] = NULL;
        } else if (strcmp(tokens[i], ">") == 0) {
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
        }
    }

    return 0;
}

// Execute external command
void execute_command(char **tokens) {
    int input_fd = STDIN_FILENO, output_fd = STDOUT_FILENO;

    if (handle_redirection(tokens, &input_fd, &output_fd) == -1) {
        return; // Error handling redirection
    }

    // Compact tokens array to remove NULLs left by redirection removal
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
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }
        if (output_fd != STDOUT_FILENO) {
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }
        if (execvp(tokens[0], tokens) == -1) {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    } else {  // Parent process
        int status;
        waitpid(pid, &status, 0);
        if (input_fd != STDIN_FILENO) close(input_fd);
        if (output_fd != STDOUT_FILENO) close(output_fd);
    }
}

// Handle pipes in command
void handle_pipes(char **tokens) {
    int pipe_fd[2];
    int prev_fd = -1;
    int i = 0;

    while (tokens[i] != NULL) {
        char *current_command[BUFFER_SIZE];
        int cmd_index = 0;

        while (tokens[i] != NULL && strcmp(tokens[i], "|") != 0) {
            current_command[cmd_index++] = tokens[i++];
        }
        current_command[cmd_index] = NULL;

        if (tokens[i] != NULL && strcmp(tokens[i], "|") == 0) {
            if (pipe(pipe_fd) < 0) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {  // Child process
            if (prev_fd != -1) {
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            }
            if (tokens[i] != NULL && strcmp(tokens[i], "|") == 0) {
                dup2(pipe_fd[1], STDOUT_FILENO);
                close(pipe_fd[0]);
                close(pipe_fd[1]);
            }
            if (execvp(current_command[0], current_command) == -1) {
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        } else {  // Parent process
            if (prev_fd != -1) close(prev_fd);
            if (tokens[i] != NULL && strcmp(tokens[i], "|") == 0) {
                prev_fd = pipe_fd[0];
                close(pipe_fd[1]);
            }
            i++;
        }
    }

    int status;
    while (wait(&status) > 0);
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
            } else if (strcmp(tokens[0], "exit") == 0) {
                handle_exit(tokens);
            } else {
                execute_command(tokens);
            }
        }

        free_tokens(tokens);
    }

    if (batch_file) {
        fclose(batch_file);
    }

    if (is_interactive) {
        printf("Exiting my shell.\n");
    }
    return 0;
}
