#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // For system calls like fork(), exec(), chdir()
#include <fcntl.h>   // For file operations
#include <sys/wait.h>  // For process management
#include <dirent.h>  // For wildcards
#include <fnmatch.h> // For pattern matching

#define BUFFER_SIZE 1024

// Function pre-declarations to prevent explosions
void handleCd(char **tokens);
void handlePwd();
void handleWhich(char **tokens);
void handleExit(char **tokens);
void executeExternalCommand(char **tokens);
void executeWithPipe(char **cmd1_tokens, char **cmd2_tokens);
void handleRedirect(char **tokens, int *input_fd, int *output_fd);
void expandWildcards(char ***tokens_ptr);
char **inputToTokens(char *input);
void freeTokens(char **tokens);

// handles change directory
void handleCd(char **tokens) {
    if (tokens[1] == NULL) {
        fprintf(stderr, "cd: missing argument\n");
        return;
    }
    if (chdir(tokens[1]) != 0) {
        perror("cd");
    }
}

// handles current directory path using cwd
void handlePwd() {
    char cwd[BUFFER_SIZE];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("pwd");
    } else {
        printf("%s\n", cwd);
    }
}

// handles which (shows filepath of parsed file)
void handleWhich(char **tokens) {
    if (tokens[1] == NULL) {
        fprintf(stderr, "which: missing argument\n");
        return;
    }

    // array for builtin commands
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

void handleExit(char **tokens) {
    if (tokens[1] != NULL) {
        printf("Exiting with message: %s\n", tokens[1]);
    }

    // frees tokens array on exit to prevent leaks
    freeTokens(char **tokens);
    exit(0);
}

// processes wildcards like * and ?
void expandWildcards(char ***tokens_ptr) {
    char **tokens = *tokens_ptr;
    char **expanded_tokens = NULL; // array holds expanded tokens
    int expanded_count = 0;

    for (int i = 0; tokens[i] != NULL; i++) {
        if (strchr(tokens[i], '*')) {  // checks for wildcard in token
            DIR *dir = opendir(".");
            if (!dir) {
                perror("opendir");
                continue;
            }

            struct dirent *entry;
            int matches_found = 0;

            while ((entry = readdir(dir)) != NULL) {
                if (fnmatch(tokens[i], entry->d_name, 0) == 0) {  
                    expanded_tokens = realloc(expanded_tokens, (expanded_count + 1) * sizeof(char *));
                    if (!expanded_tokens) {
                        perror("realloc");
                        closedir(dir);
                        return;
                    }
                    expanded_tokens[expanded_count++] = strdup(entry->d_name);  
                    matches_found++;
                }
            }
            closedir(dir);

            if (matches_found == 0) {  // No matches, retain the original token
                expanded_tokens = realloc(expanded_tokens, (expanded_count + 1) * sizeof(char *));
                if (!expanded_tokens) {
                    perror("realloc");
                    return;
                }
                expanded_tokens[expanded_count++] = strdup(tokens[i]);  // Keep original
                fprintf(stderr, "No matches for wildcard: %s\n", tokens[i]);
            }
        } else {  // No wildcard, add the token as is
            expanded_tokens = realloc(expanded_tokens, (expanded_count + 1) * sizeof(char *));
            if (!expanded_tokens) {
                perror("realloc");
                return;
            }
            expanded_tokens[expanded_count++] = strdup(tokens[i]);
        }
    }

    // Null-terminate the expanded tokens array
    expanded_tokens = realloc(expanded_tokens, (expanded_count + 1) * sizeof(char *));
    if (!expanded_tokens) {
        perror("realloc");
        return;
    }
    expanded_tokens[expanded_count] = NULL;

    // Free the original tokens
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);

    *tokens_ptr = expanded_tokens;  //finalized array is put into tokens_ptr
}

void handleRedirect(char **tokens, int *input_fd, int *output_fd) {
    // checks for NULL token after redirect token
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
                // checks for incomplete command structure
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

// pipe execution
void executeWithPipe(char **cmd1_tokens, char **cmd2_tokens) {
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

    if (pid1 == 0) {  // arg before pipe (write output)
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

    if (pid2 == 0) {  // arg after pipe (read first arg output)
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

// executes all commands including builtins
void executeExternalCommand(char **tokens) {
    int input_fd = -1, output_fd = -1;
    handleRedirect(tokens, &input_fd, &output_fd);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {  // child process using dup2
        if (input_fd != -1) {
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }
        if (output_fd != -1) {
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }

        // resolves the full path manually
        const char *paths[] = {"/usr/local/bin", "/usr/bin", "/bin"};
        char full_path[BUFFER_SIZE];
        int found = 0;

        if (strchr(tokens[0], '/')) {  // in case of slash in command -> treat as path
            strncpy(full_path, tokens[0], BUFFER_SIZE);
            full_path[BUFFER_SIZE - 1] = '\0';
            found = access(full_path, X_OK) == 0;
        } else {  // search defined paths
            for (int i = 0; i < 3; i++) {
                snprintf(full_path, sizeof(full_path), "%s/%s", paths[i], tokens[0]);
                if (access(full_path, X_OK) == 0) {
                    found = 1;
                    break;
                }
            }
        }

        if (!found) {
            fprintf(stderr, "%s: command not found\n", tokens[0]);
            exit(EXIT_FAILURE);
        }

        // execute command with execv
        execv(full_path, tokens);
        perror("execv");
        exit(EXIT_FAILURE);
    } else {  
        wait(NULL);
    }
}

// converts input to tokens
char **inputToTokens(char *input) {
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

// frees array of tokens
void freeTokens(char **tokens) {
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
    int batch_mode = (argc == 2);  // if file input -> batch mode
    int input_fd = STDIN_FILENO;   //else default to stdin

    if (batch_mode) {
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd < 0) {
            perror("open");
            return EXIT_FAILURE;
        }
        is_interactive = 0;  // disables interaction in batch mode
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
        if (bytes_read <= 0) break;  // ends input
        buffer[bytes_read] = '\0';  // NULL terminate string

        tokens = inputToTokens(buffer);
        if (tokens[0] == NULL) {  // skips empty input
            freeTokens(tokens);
            continue;
        }

        expandWildcards(&tokens);

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
            executeWithPipe(cmd1_tokens, cmd2_tokens);
        } else if (strcmp(tokens[0], "cd") == 0) {
            handleCd(tokens);
        } else if (strcmp(tokens[0], "pwd") == 0) {
            handlePwd();
        } else if (strcmp(tokens[0], "which") == 0) {
            handleWhich(tokens);
        } else if (strcmp(tokens[0], "exit") == 0) {
            handleExit(tokens);
        } else {
            executeExternalCommand(tokens);
        }

        freeTokens(tokens);
    }

    if (batch_mode) {
        close(input_fd);
    }

    if (is_interactive) {
        printf("Exiting my shell.\n");
    }

    return 0;
}

