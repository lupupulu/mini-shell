CC ?= cc
CFLAG ?= -std=c99 -O2 -Wall

BUILD_TYPE ?= DEBUG

ifeq ($(BUILD_TYPE),RELEASE)
RELEASE = -s
DEBUG =
else ifeq ($(BUILD_TYPE),DEBUG)
RELEASE =
DEBUG = -g --static -DTERM_TOSTOP
else
RELEASE =
DEBUG =
endif

ALL: nmsh mnsh echokey

mnsh: main.c mnsh.o confc.o darray.o input.o
	$(CC) $(RELEASE) $(DEBUG) $(CFLAG) $^ -o $@

mnsh.o: mnsh.c mnsh.h
	$(CC) -c $(DEBUG) $(CFLAG) $< -o $@

confc.o: CDS/confc.c
	$(CC) -c $(DEBUG) $(CFLAG) $< -o $@

darray.o: CDS/darray.c
	$(CC) -c $(DEBUG) $(CFLAG) $< -o $@

input.o: CDS/input.c
	$(CC) -c $(DEBUG) $(CFLAG) $< -o $@


nmsh: mini-shell.c
	$(CC) $(RELEASE) $(DEBUG) $(CFLAG) $< -o $@

echokey: echokey.c
	$(CC) $(RELEASE) $(DEBUG) $(CFLAG) $^ -o $@


clean:
	- rm -f nmsh mnsh init config.c mnsh.o darray.o input.o echokey

.PHONY: clean