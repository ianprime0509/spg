.POSIX:

include config.mk

SPGCFLAGS = $(CFLAGS) -Wall -Wextra -std=c99 -pedantic
CPPFLAGS = -D_XOPEN_SOURCE=700
OBJS = spg.o

all: spg

clean:
	rm -f $(OBJS) spg

install: spg spg.1
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f spg $(DESTDIR)$(PREFIX)/bin
	chmod 555 $(DESTDIR)$(PREFIX)/bin/spg
	mkdir -p $(DESTDIR)$(MANDIR)
	cp -f spg.1 $(DESTDIR)$(MANDIR)

spg: $(OBJS)
	$(CC) -o spg $(OBJS) $(LIBS)

config.h:
	cp config.def.h config.h

spg.o: spg.c config.h config.mk
	$(CC) $(SPGCFLAGS) $(CPPFLAGS) -c -o spg.o spg.c
