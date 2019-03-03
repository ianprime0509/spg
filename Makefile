CFLAGS = -Wall -Wextra -std=c99 -pedantic -O0 -g
CPPFLAGS = -D_XOPEN_SOURCE=700
LIBS = -lcurses
OBJS = spg.o

spg: $(OBJS)
	$(CC) $(CFLAGS) -o spg $(OBJS) $(LIBS)

clean:
	rm -f $(OBJS) spg

config.h:
	cp config.def.h config.h

spg.o: spg.c config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o spg.o spg.c
