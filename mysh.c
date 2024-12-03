#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // For system calls like fork(), exec(), chdir()
#include <fcntl.h>   // For file operations
#include <sys/wait.h>  // For process management
#include <dirent.h>  // For wildcards
#include <fnmatch.h> // For pattern matching

#define BUFFER_SIZE 1024

// Function prototypes
void handle_cd(char **tokens);
void handle_pwd();
void handle_which(char **tokens);
void handle_exit(char **tokens);
void execute_external_command(char **tokens);
void execute_with_pipe(char **cmd1_tokens, char **cmd2_tokens);
void handle_redirection(char **tokens, int *input_fd, int *output_fd);
void expand_wildcards(char **tokens);
char **tokenize_input(char *input);
void free_tokens(char **tokens);

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

void expand_wildcards(char **tokens) {
    for (int i = 0; tokens[i] != NULL; i++) {
        if (strchr(tokens[i], '*')) {  // Check for wildcard
            DIR *dir = opendir(".");
            if (!dir) {
                perror("opendir");
                continue;
            }

            struct dirent *entry;
            char **expanded = NULL;
            int count = 0;

            while ((entry = readdir(dir)) != NULL) {
                if (fnmatch(tokens[i], entry->d_name, 0) == 0) {  // Match pattern
                    expanded = realloc(expanded, (count + 1) * sizeof(char *));
                    expanded[count++] = strdup(entry->d_name);
                }
            }
            closedir(dir);

            if (count > 0) {
                expanded = realloc(expanded, (count + 1) * sizeof(char *));
                expanded[count] = NULL;

                free(tokens[i]);
                tokens[i] = expanded[0];  // Replace token with first match
                for (int j = 1; j < count; j++) {
                    tokens = realloc(tokens, (i + j + 1) * sizeof(char *));
                    tokens[i + j] = expanded[j];
                }
                tokens[i + count] = NULL;  // Null-terminate
            }
        }
    }
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
            tokens[i] = NULL;
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
            tokens[i] = NULL;
        }
    }
}

void execute_with_pipe(char **cmd1_tokens, char **cmd2_tokens) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }

    pid_t pid1 = fork();
    if (pid1 < 0) {
        perror("fork");
        return;
    }

    if (pid1 == 0) {  // First child
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execvp(cmd1_tokens[0], cmd1_tokens);
        perror("execvp");
        exit(EXIT_FAILURE);
    }

    pid_t pid2 = fork();
    if (pid2 < 0) {
        perror("fork");
        return;
    }

    if (pid2 == 0) {  // Second child
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        execvp(cmd2_tokens[0], cmd2_tokens);
        perror("execvp");
        exit(EXIT_FAILURE);
    }

    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

void execute_external_command(char **tokens) {
    int input_fd = -1, output_fd = -1;
    handle_redirection(tokens, &input_fd, &output_fd);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {  // Child process
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
    } else {  // Parent process
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

int main(int argc, char *argv[]) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    char **tokens;
    int is_interactive = isatty(STDIN_FILENO);
    int batch_mode = (argc == 2);

    if (is_interactive) {
        printf("Welcome to my shell!\n");
    }

    while (1) {
        if (is_interactive) {
            printf("mysh> ");
            fflush(stdout);
        }

        bytes_read = read(STDIN_FILENO, buffer, BUFFER_SIZE - 1);
        if (bytes_read <= 0) break;
        buffer[bytes_read] = '\0';

        tokens = tokenize_input(buffer);
        if (tokens[0] == NULL) {
            free_tokens(tokens);
            continue;
        }

        expand_wildcards(tokens);

        int pipe_index = -1;
        for (int i = 0; tokens[i] != NULL; i++) {
            if (strcmp(tokens[i], "|") == 0) {
                pipe_index = i;
                break;
            }
        }

        if (pipe_index != -1) {
            tokens[pipe_index] = NULL;
            char **cmd1_tokens = tokens;
            char **cmd2_tokens = &tokens[pipe_index + 1];
            execute_with_pipe(cmd1_tokens, cmd2_tokens);
        } else if (strcmp(tokens[0], "cd") == 0) {
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

    if (is_interactive) {
        printf("Exiting my shell.\n");
    }

    return 0;
}

