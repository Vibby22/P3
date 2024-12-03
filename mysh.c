#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // For isatty(), chdir(), fork(), execvp(), dup2()
#include <fcntl.h>   // For open()
#include <sys/wait.h>  // For wait()

#define BUFFER_SIZE 1024

// Function prototypes
void handle_cd(char **tokens);
void handle_pwd();
void handle_which(char **tokens);
void handle_exit(char **tokens);
void execute_external_command(char **tokens);
char **tokenize_input(char *input);
void free_tokens(char **tokens);
void handle_redirection(char **tokens, int *input_fd, int *output_fd);

void handle_cd(char **tokens) {
    if (tokens[1] == NULL) {
        fprintf(stderr, "cd: missing argument\n");
        return;
    }
    if (chdir(tokens[1]) != 0) {
        perror("cd");
    }
}

void handle_pwd() {
    char cwd[BUFFER_SIZE];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("pwd");
    } else {
        printf("%s\n", cwd);
    }
}

void handle_which(char **tokens) {
    if (tokens[1] == NULL) {
        fprintf(stderr, "which: missing argument\n");
        return;
    }

    const char *paths[] = {"/usr/local/bin", "/usr/bin", "/bin"};
    char path[BUFFER_SIZE];
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "%s/%s", paths[i], tokens[1]);
        if (access(path, X_OK) == 0) {
            printf("%s\n", path);
            return;
        }
    }

    fprintf(stderr, "which: command not found: %s\n", tokens[1]);
}

void handle_exit(char **tokens) {
    if (tokens[1] != NULL) {
        printf("Exiting with message: %s\n", tokens[1]);
    }
    exit(0);
}

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
            tokens[i] = NULL;  // Remove < and the file name from tokens
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
            tokens[i] = NULL;  // Remove > and the file name from tokens
        }
    }
}

void execute_external_command(char **tokens) {
    int input_fd = -1, output_fd = -1;
    handle_redirection(tokens, &input_fd, &output_fd);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        // Child process
        if (input_fd != -1) {
            if (dup2(input_fd, STDIN_FILENO) == -1) {
                perror("dup2 input");
                exit(EXIT_FAILURE);
            }
            close(input_fd);
        }
        if (output_fd != -1) {
            if (dup2(output_fd, STDOUT_FILENO) == -1) {
                perror("dup2 output");
                exit(EXIT_FAILURE);
            }
            close(output_fd);
        }

        // Use execvp to search for the command in PATH
        if (execvp(tokens[0], tokens) == -1) {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    } else {
        // Parent process
        int status;
        if (wait(&status) == -1) {
            perror("wait");
        } else {
            if (WIFEXITED(status)) {
                if (WEXITSTATUS(status) != 0) {
                    fprintf(stderr, "Command failed with code %d\n", WEXITSTATUS(status));
                }
            } else if (WIFSIGNALED(status)) {
                fprintf(stderr, "Terminated by signal: %d\n", WTERMSIG(status));
            }
        }
        if (input_fd != -1) close(input_fd);
        if (output_fd != -1) close(output_fd);
    }
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
    int batch_mode = 0;                        // Flag for batch mode
    int input_fd = STDIN_FILENO;               // Default to standard input

    // Check for batch file input
    if (argc == 2) {
        batch_mode = 1;
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd < 0) {
            perror("open");
            exit(EXIT_FAILURE);
        }
        is_interactive = 0;  // Batch mode disables interactive behavior
    } else if (argc > 2) {
        fprintf(stderr, "Usage: %s [batch_file]\n", argv[0]);
        exit(EXIT_FAILURE);
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
        if (bytes_read < 0) {
            perror("read");
            break;
        } else if (bytes_read == 0) {
            // End of input
            break;
        }

        buffer[bytes_read] = '\0';  // Null-terminate the input string
        tokens = tokenize_input(buffer);  // Tokenize the input

        if (tokens[0] == NULL) {  // Skip empty input
            free_tokens(tokens);
            continue;
        }

        // Handle built-in commands or execute external commands
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
