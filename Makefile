LDFLAGS=-lirc -lhashtable
CFLAGS=-std=c99 -pedantic -Wall

all: segfault

again: clean all

segfault: segfault.c

clean:
	rm -f segfault

install: all
	cp -f segfault /usr/local/bin/segfault
