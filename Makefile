LDFLAGS=-lirc -Llibirc
CFLAGS=-pedantic -Wall

all:
	cd libirc && $(MAKE)
	$(MAKE) segfault

clean:
	rm -f segfault
