CC ?= gcc

all: rfid-access

rfid-access: main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o rfid-access main.c

install:
	install -D -m 0755 rfid-access $(DESTDIR)/usr/bin/rfid-access

clean:
	rm -f rfid-access