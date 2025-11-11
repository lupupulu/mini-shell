CC ?= gcc
CFLAG ?= -std=c99 -O2 -g

ALL: mnsh

mnsh: main.c config.c builtin_cmd.o
	$(CC) $(CFLAG) $^ -o $@

config.c: init
	./init

init: init.c builtin_cmd.o
	$(CC) $(CFLAG) $^ -o $@

builtin_cmd.o: builtin_cmd.c config.h mnsh.h
	$(CC) -std=c99 -O2 -g -c $< -o $@

clean:
	- rm -f mnsh builtin_cmd.o init config.c

.PHONY: clean