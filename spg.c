#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curses.h>

typedef struct Buffer Buffer;
typedef struct Window Window;

static SCREEN *scrn;
static Window *win;

struct Buffer {
	char **lines;
	size_t len, cap, width;
};

struct Window {
	Buffer *buf;
	size_t rows, row;
};

static void die(int status, const char *fmt, ...);
static void *xmalloc(size_t sz);
static void *xrealloc(void *mem, size_t sz);

static Buffer *bufnew(void);
static void buffree(Buffer *buf);
static void bufgrow(Buffer *buf);
static char *bufnewline(Buffer *buf);
static void bufsetwidth(Buffer *buf, size_t width);

static Window *winnew(void);
static void winfree(Window *win);
static void winfill(Window *win);
static int wingetline(Window *win);
static void winresize(Window *win, size_t rows, size_t cols);
static void winscrolldown(Window *win, size_t lines);
static void winscrollup(Window *win, size_t lines);

static void uiinit(void);
static void uiteardown(void);
static int uigetkey(void);
static void uirefresh(void);
static void uiresize(void);

static void
die(int status, const char *fmt, ...)
{
	va_list args;

	uiteardown();
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	if (errno)
		fprintf(stderr, ": %s", strerror(errno));
	fprintf(stderr, "\n");

	exit(status);
}

static void *
xmalloc(size_t sz)
{
	void *mem;

	if (!(mem = malloc(sz)))
		die(1, "malloc");
	return mem;
}

static void *
xrealloc(void *mem, size_t sz)
{
	void *newmem;

	if (!(newmem = realloc(mem, sz)))
		die(1, "realloc");
	return newmem;
}

static Buffer *
bufnew(void)
{
	Buffer *buf;

	buf = xmalloc(sizeof(*buf));
	buf->len = buf->width = 0;
	buf->cap = 128;
	buf->lines = xmalloc(buf->cap * sizeof(*buf->lines));
	return buf;
}

static void
buffree(Buffer *buf)
{
	size_t i;

	for (i = 0; i < buf->len; i++)
		free(buf->lines[i]);
	free(buf->lines);
	free(buf);
}

static void
bufgrow(Buffer *buf)
{
	buf->cap *= 2;
	buf->lines = xrealloc(buf->lines, buf->cap * sizeof(*buf->lines));
}

static char *
bufnewline(Buffer *buf)
{
	if (buf->len == buf->cap)
		bufgrow(buf);
	return buf->lines[buf->len++] = malloc(buf->width);
}

static void
bufsetwidth(Buffer *buf, size_t width)
{
	size_t i, j;

	if (width > buf->width)
		for (i = 0; i < buf->len; i++) {
			buf->lines[i] = xrealloc(buf->lines[i], width);
			for (j = buf->width; j < width; j++)
				buf->lines[i][j] = ' ';
		}
	buf->width = width;
}

static Window *
winnew(void)
{
	Window *win;

	win = xmalloc(sizeof(*win));
	win->buf = bufnew();
	win->rows = win->row = 0;
	return win;
}

static void
winfree(Window *win)
{
	buffree(win->buf);
	free(win);
}

static void
winfill(Window *win)
{
	size_t newrows;

	newrows = 0;
	while (win->buf->len < win->rows)
		if (wingetline(win))
			break;
		else
			newrows++;

	win->row += newrows;
}

static int
wingetline(Window *win)
{
	char *line;
	size_t i;
	int c;

	if (feof(stdin) || ferror(stdin))
		return 1;

	line = bufnewline(win->buf);
	for (i = 0; i < win->buf->width; i++)
		if ((c = getchar()) == EOF || c == '\n')
			break;
		else
			line[i] = c;

	for (; i < win->buf->width; i++)
		line[i] = ' ';
	return 0;
}

static void
winresize(Window *win, size_t rows, size_t cols)
{
	win->rows = rows;
	bufsetwidth(win->buf, cols);
	winfill(win);
}

static void
winscrolldown(Window *win, size_t lines)
{
	while (win->buf->len < win->row + lines) {
		if (wingetline(win))
			break;
	}

	win->row += lines;
	if (win->row > win->buf->len)
		win->row = win->buf->len;
}

static void
winscrollup(Window *win, size_t lines)
{
	win->row = lines > win->row ? 0 : win->row - lines;
	if (win->row < win->rows)
		win->row = win->rows < win->buf->len ? win->rows : win->buf->len;
}

static void
uiinit(void)
{
	FILE *tty;

	if (!(tty = fopen("/dev/tty", "r")))
		die(1, "no tty");
	if (!(scrn = newterm(NULL, stdout, tty)))
		die(1, "cannot create screen");
	set_term(scrn);
	cbreak();
	keypad(stdscr, TRUE);
	noecho();
	curs_set(0);
}

static void
uiteardown(void)
{
	if (scrn) {
		endwin();
		delscreen(scrn);
	}
}

static int
uigetkey(void)
{
	return getch();
}

static void
uirefresh(void)
{
	size_t i, j, start;

	clear();
	start = win->row >= win->rows ? win->row - win->rows : 0;
	for (i = start; i < win->row; i++) {
		move(i - start, 0);
		for (j = 0; j < win->buf->width; j++)
			addch(win->buf->lines[i][j]);
	}
	refresh();
}

static void
uiresize(void)
{
	int rows, cols;

	getmaxyx(stdscr, rows, cols);
	winresize(win, rows, cols);
	uirefresh();
}

int
main(int argc, char **argv)
{
	win = winnew();
	uiinit();
	uiresize();

	for (;;)
		switch (uigetkey()) {
		case 'q':
			goto done;
		case 'j':
			winscrolldown(win, 1);
			uirefresh();
			break;
		case 'k':
			winscrollup(win, 1);
			uirefresh();
			break;
		}

done:
	uiteardown();
	winfree(win);
	return 0;
}
