LDFLAGS=-lirc -lhashtable
CFLAGS=-std=c99 -pedantic -Wall

all:
	$(MAKE) segfault

clean:
	rm -f segfault

install:
	cp -f segfault /usr/local/bin/segfault
