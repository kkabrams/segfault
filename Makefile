LDFLAGS=-lirc -lhashtable -L/usr/local/lib
CFLAGS=-std=c99 -pedantic -Wall -ggdb -I/usr/local/include

all: segfault

again: clean all

segfault: segfault.c

clean:
	rm -f segfault

install: all
	install segfault /usr/local/bin/
