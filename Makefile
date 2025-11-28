CC = gcc
CFLAG ?= -std=c99 -O2 -g -Wall

ALL: mnsh

mnsh: main.c config.c history.o variable.o builtin_cmd.o input.o
	$(CC) $(CFLAG) $^ -o $@

config.c: init
	./init

init: init.c builtin_cmd.o input.o variable.o history.o
	$(CC) $(CFLAG) $^ -o $@

input.o: input.c config.h mnsh.h
	$(CC) $(CFLAG) -c $< -o $@

builtin_cmd.o: builtin_cmd.c config.h mnsh.h
	$(CC) $(CFLAG) -c $< -o $@

variable.o: variable.c config.h mnsh.h
	$(CC) $(CFLAG) -c $< -o $@

history.o: history.c config.h mnsh.h
	$(CC) $(CFLAG) -c $< -o $@

clean:
	- rm -f mnsh builtin_cmd.o input.o variable.o history.o init config.c

.PHONY: clean