CC = gcc
CFLAGS = -Wall -Werror -g

OBJ = main.o builtins.o command.o wildcards.o parser.o

mysh: $(OBJ)
    $(CC) $(CFLAGS) -o mysh $(OBJ)

main.o: main.c builtins.h command.h wildcards.h parser.h
    $(CC) $(CFLAGS) -c main.c

builtins.o: builtins.c builtins.h
    $(CC) $(CFLAGS) -c builtins.c

command.o: command.c command.h
    $(CC) $(CFLAGS) -c command.c

wildcards.o: wildcards.c wildcards.h
    $(CC) $(CFLAGS) -c wildcards.c

parser.o: parser.c parser.h
    $(CC) $(CFLAGS) -c parser.c

clean:
    rm -f *.o mysh
