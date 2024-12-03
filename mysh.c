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
void execute_external_command(char **tokens, int input_fd, int output_fd);
char **tokenize_input(char *input);
void free_tokens(char **tokens);
void handle_redirection(char **tokens, int *input_fd, int *output_fd);
void handle_pipes(char **tokens);

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

void execute_external_command(char **tokens, int input_fd, int output_fd) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {  // Child process
        if (input_fd != STDIN_FILENO) {  // Redirect input
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }
        if (output_fd != STDOUT_FILENO) {  // Redirect output
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }
        if (execvp(tokens[0], tokens) == -1) {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    } else {  // Parent process
        int status;
        if (wait(&status) == -1) {
            perror("wait");
        } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            fprintf(stderr, "Command failed with code %d\n", WEXITSTATUS(status));
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
            if (prev_read_end != -1) {
                dup2(prev_read_end, STDIN_FILENO);
                close(prev_read_end);
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
    for (int j = 0; j < i; j++) {
        wait(NULL);
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

void handle_exit(char **tokens) {
    if (tokens[1] != NULL) {
        printf("Exiting with message: %s\n", tokens[1]);
    }
    free_tokens(tokens);
    exit(0);
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
        if (bytes_read <= 0) break;

        buffer[bytes_read] = '\0';
        tokens = tokenize_input(buffer);

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
            // No pipes, execute a single command
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

    if (is_interactive) printf("Exiting my shell.\n");
    return 0;
}
