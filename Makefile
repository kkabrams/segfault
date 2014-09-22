LDFLAGS=-lirc -Llibirc -lhashtable -Llibhashtable
CFLAGS=-pedantic -Wall

all:
	cd libirc && $(MAKE)
	cd libhashtable && $(MAKE)
	$(MAKE) segfault

clean:
	cd libirc && $(MAKE) clean
	cd libhashtable && $(MAKE) clean
	rm -f segfault
