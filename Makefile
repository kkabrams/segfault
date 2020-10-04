PREFIX:=/usr/local
LDFLAGS=-L/usr/local/lib -L$(PREFIX)/lib -lirc -lhashtable -lgcc_s -lidc -ldl
CFLAGS=-pedantic -Wall -ggdb -I/usr/local/include -I$(PREFIX)/include

all: segfault libhack.so

libhack.so: libhack.o
	ld -shared -o libhack.so libhack.o

again: clean all

segfault: segfault.c access.h

clean:
	rm -f segfault

install: all
	install segfault /usr/local/bin/
