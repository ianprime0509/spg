/*
 * Copyright (c) 2019 Ian Johnson
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curses.h>

#define LEN(x) (sizeof(x) / sizeof(*(x)))
#define USED(x) ((void)(x))

typedef union Arg Arg;
typedef struct Key Key;

union Arg {
	size_t zu;
	double lf;
};

struct Key {
	int key;
	int (*func)(Arg);
	Arg arg;
};

static int pagedown(Arg a);
static int pageup(Arg a);
static int scrollbot(Arg a);
static int scrolldown(Arg a);
static int scrolltop(Arg a);
static int scrollup(Arg a);
static int quit(Arg a);

#include "config.h"

typedef struct Buffer Buffer;
typedef struct Window Window;

static SCREEN *scrn;
static Window *win;

struct Buffer {
	char **lines;
	size_t len, cap, width, maxwidth;
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
static void winscrollbot(Window *win);
static void winscrolldown(Window *win, size_t lines);
static void winscrolltop(Window *win);
static void winscrollup(Window *win, size_t lines);

static void uiinit(void);
static void uiteardown(void);
static int uigetkey(void);
static void uirefresh(void);
static void uiresize(void);

static int
pagedown(Arg a)
{
	winscrolldown(win, a.lf > 0 ? a.lf * win->rows : 1);
	uirefresh();
	return 0;
}

static int
pageup(Arg a)
{
	winscrollup(win, a.lf > 0 ? a.lf * win->rows : 1);
	uirefresh();
	return 0;
}

static int
scrollbot(Arg a)
{
	USED(a);
	winscrollbot(win);
	uirefresh();
	return 0;
}

static int
scrolldown(Arg a)
{
	winscrolldown(win, a.zu);
	uirefresh();
	return 0;
}

static int
scrolltop(Arg a)
{
	USED(a);
	winscrolltop(win);
	uirefresh();
	return 0;
}

static int
scrollup(Arg a)
{
	winscrollup(win, a.zu);
	uirefresh();
	return 0;
}

static int
quit(Arg a)
{
	USED(a);
	return 1;
}

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
	buf->len = buf->width = buf->maxwidth = 0;
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
	char *line;

	if (buf->len == buf->cap)
		bufgrow(buf);
	line = buf->lines[buf->len++] = malloc(buf->maxwidth);
	memset(line, ' ', buf->maxwidth);
	return line;
}

static void
bufsetwidth(Buffer *buf, size_t width)
{
	size_t i, j;

	if (width > buf->maxwidth) {
		for (i = 0; i < buf->len; i++) {
			buf->lines[i] = xrealloc(buf->lines[i], width);
			for (j = buf->maxwidth; j < width; j++)
				buf->lines[i][j] = ' ';
		}
		buf->maxwidth = width;
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
winscrollbot(Window *win)
{
	while (!wingetline(win))
		;
	win->row = win->buf->len;
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
winscrolltop(Window *win)
{
	win->row = win->buf->len > win->rows ? win->rows : win->buf->len;
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
	int key;
	size_t i;

	win = winnew();
	uiinit();
	uiresize();

	for (;;) {
		key = uigetkey();
		/* TODO: this isn't portable */
		if (key == KEY_RESIZE) {
			uiresize();
			continue;
		}
		for (i = 0; i < LEN(keys); i++)
			if (keys[i].key == key) {
				if (keys[i].func(keys[i].arg))
					goto done;
				break;
			}
	}

done:
	uiteardown();
	winfree(win);
	return 0;
}
