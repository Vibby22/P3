mysh: mysh.c
	gcc -g mysh.c -o mysh
clean: 
	rm -f mysh *.o
