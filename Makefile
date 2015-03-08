LDFLAGS=-lirc -Llibirc -lhashtable -Llibhashtable
CFLAGS=-std=c99 -pedantic -Wall

all:
	cd libirc && $(MAKE)
	cd libhashtable && $(MAKE)
	$(MAKE) segfault

clean:
	cd libirc && $(MAKE) clean
	cd libhashtable && $(MAKE) clean
	rm -f segfault

install:
	cp -f libirc/libirc.so /usr/local/lib
	cp -f libhashtable/libhashtable.so /usr/local/lib
#	cd libirc && $(MAKE) install
#	cd libhashtable && $(MAKE) install
	cp -f segfault /usr/local/bin/segfault
