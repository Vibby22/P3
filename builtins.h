#ifndef BUILTINS_H
#define BUILTINS_H

void handle_cd(char **tokens);
void handle_pwd();
void handle_which(char **tokens);
void handle_exit(char **tokens);

#include "parser.h"

#endif // BUILTINS_H
