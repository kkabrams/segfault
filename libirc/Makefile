LDFLAGS=-lirc -Llibirc
CFLAGS=-fpic -shared -pedantic -Wall
TARGET=libirc.so

all: $(TARGET)

$(TARGET):
	$(CC) $(CFLAGS) -o $(TARGET) libirc.c

clean:
	rm -f libirc.so
