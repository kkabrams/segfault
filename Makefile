LDFLAGS=-L/usr/local/lib -L/home/pi/.local/lib -lirc -lhashtable -lgcc_s -lidc
CFLAGS=-pedantic -Wall -ggdb -I/usr/local/include -I/home/pi/.local/include

all: segfault

again: clean all

segfault: segfault.c

clean:
	rm -f segfault

install: all
	install segfault /usr/local/bin/
