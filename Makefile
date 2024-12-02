mysh: mysh.c
	gcc -Werror -Wall -fsanitize=address,undefined -g mysh.c -o mysh
clean: 
	rm -f mysh *.o
