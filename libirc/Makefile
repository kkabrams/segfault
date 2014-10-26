LDFLAGS=-lirc -Llibirc
CFLAGS=-fpic -shared -pedantic -Wall
TARGET=libirc.so

all: $(TARGET)

$(TARGET):
	$(CC) $(CFLAGS) -o $(TARGET) libirc.c

clean:
	rm -f libirc.so

install:
	cp $(TARGET) /usr/local/lib/$(TARGET)
	cp irc.h /usr/local/lib/irc.h
