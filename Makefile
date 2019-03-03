CFLAGS = -Wall -Wextra -std=c99 -pedantic -O0 -g
CPPFLAGS = -D_XOPEN_SOURCE=700
LIBS = -lcurses
OBJS = spg.o

spg: $(OBJS)
	$(CC) $(CFLAGS) -o spg $(OBJS) $(LIBS)

clean:
	rm -f $(OBJS) spg

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
