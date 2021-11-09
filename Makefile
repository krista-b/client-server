CC=gcc
CFLAGS=-Wall -pedantic -std=gnu99 -g -pthread
INC=-I/local/courses/csse2310/include
LIB=-L/local/courses/csse2310/lib -ltinyexpr -lcsse2310a4 -lcsse2310a3 -lm

.PHONY: all clean
.DEFAULT_GOAL := all

all: intserver intclient

intserver: intserver.c
	$(CC) $(CFLAGS) $(LIB) $(INC) intserver.c -o intserver

intclient: intclient.c
	$(CC) $(CFLAGS) $(LIB) $(INC) intclient.c -o intclient

clean:
	rm -f intserver
	rm -f intclient
