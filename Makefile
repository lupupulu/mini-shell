CC = cc
CFLAG ?= -std=c99 -O2 -Wall

BUILD_TYPE ?= DEBUG

ifeq ($(BUILD_TYPE),RELEASE)
RELEASE = -s
DEBUG =
else ifeq ($(BUILD_TYPE),DEBUG)
RELEASE =
DEBUG = -g --static
else
RELEASE =
DEBUG =
endif

ALL: mnsh

mnsh: main.c config.c mnsh.o darray.o locale.o
	$(CC) $(RELEASE) $(DEBUG) $(CFLAG) $^ -o $@

config.c: init config.h
	./init

init: init.c darray.o
	$(CC) $(RELEASE) $(DEBUG) $(CFLAG) $^ -o $@

mnsh.o: mnsh.c
	$(CC) -c $(DEBUG) $(CFLAG) $^ -o $@

darray.o: darray.c
	$(CC) -c $(DEBUG) $(CFLAG) $^ -o $@

locale.o: locale.c
	$(CC) -c $(DEBUG) $(CFLAG) $^ -o $@

echokey: echokey.c
	$(CC) $(RELEASE) $(DEBUG) $(CFLAG) $^ -o $@

clean:
	- rm -f mnsh init config.c mnsh.o darray.o locale.o echokey

.PHONY: clean