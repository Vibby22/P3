CC = gcc
CFLAGS = -Wall -Werror -fsanitize=address -g
OBJ = main.o builtins.o command.o wildcards.o parser.o
TARGET = mysh

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
