#ifndef COMMAND_H
#define COMMAND_H

void execute_external_command(char **tokens);
void handle_redirection(char **tokens, int *input_fd, int *output_fd);

#endif // COMMAND_H
