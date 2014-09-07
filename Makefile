LDFLAGS=-lirc -Llibirc -lhashtable -Llibhashtable
CFLAGS=-pedantic -Wall

all:
	cd libirc && $(MAKE)
	cd libhashtable && $(MAKE)
	$(MAKE) segfault

clean:
	rm -f segfault
