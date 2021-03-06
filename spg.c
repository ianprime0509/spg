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

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <term.h>
#include <termios.h>
#include <unistd.h>

/* This is coming from term.h and it conflicts with one of our names */
#undef lines

#define LEN(x) (sizeof(x) / sizeof(*(x)))
#define MIN(x, y) ((x) > (y) ? (y) : (x))
#define USED(x) ((void)(x))

enum {
	KEY_BACKSPACE = '\x7F',
	KEY_ESCAPE = '\x1B',
	KEY_RESIZE = -2,
	KEY_RETURN = '\n',
};

enum {
	RUNE_EOF = -1,
	RUNE_INCOMPLETE = -2,
	RUNE_INVALID = 0xFFFD,
};

enum Direction {
	FORWARDS,
	BACKWARDS,
};

typedef union Arg Arg;
typedef struct Key Key;
typedef enum Direction Direction;

union Arg {
	size_t zu;
	double lf;
	enum Direction dir;
};

struct Key {
	int key;
	int (*func)(Arg);
	Arg arg;
};

static int pagedown(Arg a);
static int pageup(Arg a);
static int promptsearch(Arg a);
static int scrollbot(Arg a);
static int scrolldown(Arg a);
static int scrolltop(Arg a);
static int scrollup(Arg a);
static int searchbackwards(Arg a);
static int searchforwards(Arg a);
static int quit(Arg a);

#include "config.h"

typedef int_fast32_t Rune;
typedef struct Buffer Buffer;
typedef struct Window Window;
typedef struct Input Input;
typedef struct Prompt Prompt;

static Window *win;
static Input *input;
static Prompt *search;

static struct termios tsave;
static struct termios tcurr;
static FILE *tty;
static sig_atomic_t winch;

struct Buffer {
	Rune **lines;
	size_t len, cap, linecap;
};

struct Window {
	Buffer *buf;
	size_t rows, cols, row;
};

struct Input {
	FILE *file;
	char buf[4];
	size_t buflen;
	Rune unread;
};

struct Prompt {
	Rune *text;
	char buf[4];
	size_t len, cap, buflen, col;
	int active;
	Rune prompt;
	int (*action)(Arg);
};

static void die(int status, const char *fmt, ...);
static void *xmalloc(size_t sz);
static void *xrealloc(void *mem, size_t sz);

static size_t linelen(const Rune *r);
static size_t nexttabstop(size_t col);
static size_t printwidth(Rune r);
static size_t sprintrune(char *s, Rune r);
static size_t utfdecode(const char *s, size_t len, Rune *r);
static size_t utfencode(char *s, Rune r);
static size_t utfpeeklen(char c);

static Buffer *bufnew(size_t width);
static void buffree(Buffer *buf);
static void bufgrow(Buffer *buf);
static int buflookingat(Buffer *buf, const Rune *s, size_t len, size_t row, size_t col);
static Rune *bufnewline(Buffer *buf);
static Buffer *bufreflow(Buffer *buf, size_t width, size_t row, size_t *newrow);
static int bufsearchbackwards(Buffer *buf, const Rune *s, size_t len, size_t row, size_t *found);
static int bufsearchforwards(Buffer *buf, const Rune *s, size_t len, size_t row, size_t *found);

static Window *winnew(size_t rows, size_t cols);
static void winfree(Window *win);
static void winfill(Window *win, Input *in);
static int wingetline(Window *win, Input *in);
static void winresize(Window *win, size_t rows, size_t cols, Input *in);
static void winscrollbot(Window *win, Input *in);
static void winscrolldown(Window *win, size_t lines, Input *in);
static void winscrolltop(Window *win);
static void winscrollup(Window *win, size_t lines);
static void winsearchbackwards(Window *win, const Rune *s, size_t len);
static void winsearchforwards(Window *win, const Rune *s, size_t len, Input *in);

static Input *inputnew(FILE *file);
static void inputfree(Input *in);
static int inputatend(Input *in);
static Rune inputgetrune(Input *in);
static void inputungetrune(Input *in, Rune r);

static Prompt *promptnew(Rune prompt, int (*action)(Arg));
static void promptfree(Prompt *p);
static Rune promptputchar(Prompt *p, char c);

static void uiinit(void);
static void uiteardown(void);
static int uigetkey(void);
static void uigetsize(size_t *rows, size_t *cols);
static size_t uiprint(Rune r, size_t col);
static void uipromptkey(Prompt *p, char key);
static void uipromptopen(Prompt *p);
static void uirefresh(void);
static void uiresize(void);

static void sigterm(int signo);
static void sigwinch(int signo);

static int
pagedown(Arg a)
{
	winscrolldown(win, a.lf > 0 ? a.lf * win->rows : 1, input);
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
promptsearch(Arg a)
{
	if (a.dir == FORWARDS) {
		search->prompt = '/';
		search->action = searchforwards;
	} else {
		search->prompt = '?';
		search->action = searchbackwards;
	}
	uipromptopen(search);
	return 0;
}

static int
scrollbot(Arg a)
{
	USED(a);
	winscrollbot(win, input);
	uirefresh();
	return 0;
}

static int
scrolldown(Arg a)
{
	winscrolldown(win, a.zu, input);
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
searchbackwards(Arg a)
{
	USED(a);
	winsearchbackwards(win, search->text, search->len);
	uirefresh();
	return 0;
}

static int
searchforwards(Arg a)
{
	USED(a);
	winsearchforwards(win, search->text, search->len, input);
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

	fprintf(stderr, "spg: ");
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

static size_t
linelen(const Rune *line)
{
	size_t len;

	len = 0;
	while (*line++ != RUNE_EOF)
		len++;
	return len;
}

static size_t
nexttabstop(size_t col)
{
	return (col + TABWIDTH) / TABWIDTH * TABWIDTH;
}

static size_t
printwidth(Rune r)
{
	/* TODO: implement some logic for characters that take up two cells */
	if (iscntrl(r))
		return 2;
	else if (r == '\n' || r == '\t')
		return 0; /* These characters require special handling */
	return 1;
}

static size_t
sprintrune(char *s, Rune r)
{
	if (iscntrl(r)) {
		s[0] = '^';
		s[1] = r ^ 0x40;
		return 2;
	}
	return utfencode(s, r);
}

static size_t
utfdecode(const char *s, size_t len, Rune *r)
{
	Rune got;
	size_t bytes, i;

	got = 0;
	bytes = 0;
	if (len < 1)
		goto done;

	if ((s[0] & 0x80) == 0) {
		got = s[0];
		bytes = 1;
		goto done;
	}

	if ((s[0] & 0xF8) == 0xF0) {
		got = s[0] & 0x7;
		bytes = 4;
	} else if ((s[0] & 0xF0) == 0xE0) {
		got = s[0] & 0xF;
		bytes = 3;
	} else if ((s[0] & 0xE0) == 0xC0) {
		got = s[0] & 0x1F;
		bytes = 2;
	} else {
		got = RUNE_INVALID;
		bytes = 1;
		goto done;
	}
	if (bytes > len) {
		got = RUNE_INVALID;
		bytes = 1;
		goto done;
	}

	for (i = 1; i < bytes; i++)
		if ((s[i] & 0xC0) == 0x80) {
			got = (got << 6) | (s[i] & 0x3F);
		} else {
			got = RUNE_INVALID;
			bytes = 1;
			goto done;
		}

	if (got >= 0xD800 && got <= 0xDFFF) {
		got = RUNE_INVALID;
		bytes = 1;
	}

done:
	if (r)
		*r = got;
	return bytes;
}

static size_t
utfencode(char *s, Rune r)
{
	if (r < 0 || r > 0x10FFFF) {
		return 0;
	} else if (r <= 0x7F) {
		s[0] = r;
		return 1;
	} else if (r <= 0x7FF) {
		s[0] = 0xC0 | ((r >> 6) & 0x1F);
		s[1] = 0x80 | (r & 0x3F);
		return 2;
	} else if (r <= 0xFFFF) {
		s[0] = 0xE0 | ((r >> 12) & 0x0F);
		s[1] = 0x80 | ((r >> 6) & 0x3F);
		s[2] = 0x80 | (r & 0x3F);
		return 3;
	} else {
		s[0] = 0xF0 | ((r >> 18) & 0x07);
		s[1] = 0x80 | ((r >> 12) & 0x3F);
		s[2] = 0x80 | ((r >> 6) & 0x3F);
		s[3] = 0x80 | (r & 0x3F);
		return 4;
	}
}

static size_t
utfpeeklen(char c)
{
	if ((c & 0xF8) == 0xF0)
		return 4;
	else if ((c & 0xF0) == 0xE0)
		return 3;
	else if ((c & 0xE0) == 0xC0)
		return 2;
	return 1;
}

static Buffer *
bufnew(size_t width)
{
	Buffer *buf;

	buf = xmalloc(sizeof(*buf));
	buf->linecap = width + 2;
	buf->len = 0;
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

static int
buflookingat(Buffer *buf, const Rune *s, size_t len, size_t row, size_t col)
{
	if (row >= buf->len || col >= buf->linecap)
		return 0;

	while (len-- > 0) {
		if (buf->lines[row][col] != *s++)
			return 0;
		col++;
		if (buf->lines[row][col] == RUNE_EOF) {
			col = 0;
			if (++row >= buf->len)
				return 0;
		}
	}
	return 1;
}

static Rune *
bufnewline(Buffer *buf)
{
	Rune *line;
	size_t i;

	if (buf->len == buf->cap)
		bufgrow(buf);
	line = buf->lines[buf->len++] = xmalloc(buf->linecap * sizeof(**buf->lines));
	for (i = 0; i < buf->linecap; i++)
		line[i] = RUNE_EOF;
	return line;
}

static Buffer *
bufreflow(Buffer *buf, size_t width, size_t row, size_t *newrow)
{
	Buffer *new;
	Rune *oldl, *newl;
	size_t i, j, c, w;
	int needline;

	new = bufnew(width);
	needline = 1;
	c = j = 0;

	for (i = 0; i < buf->len; i++) {
		for (oldl = buf->lines[i]; *oldl != RUNE_EOF; oldl++) {
			w = printwidth(*oldl);
			if (needline || c + w > width || j >= new->linecap - 1) {
				newl = bufnewline(new);
				c = j = 0;
				needline = 0;
			}

			*newl++ = *oldl;
			j++;
			if (*oldl == '\n')
				needline = 1;
			else if (*oldl == '\t')
				c = nexttabstop(c);
			else
				c += w;
		}

		if (i == row - 1 && newrow)
			*newrow = new->len;
		free(buf->lines[i]);
	}
	if (i <= row - 1 && newrow)
		*newrow = new->len;

	free(buf->lines);
	return new;
}

static int
bufsearchbackwards(Buffer *buf, const Rune *s, size_t len, size_t row, size_t *found)
{
	size_t i, j;

	if (len == 0 || buf->len == 0 || row == 0)
		return 1;

	if (row >= buf->len - 1)
		row = buf->len - 1;

	i = row - 1;
	j = linelen(buf->lines[i]);

	for (;;) {
		if (buf->lines[i][j] == s[0] && buflookingat(buf, s, len, i, j)) {
			if (found)
				*found = i;
			return 0;
		}

		if (j-- == 0) {
			if (i-- == 0)
				return 1;
			j = linelen(buf->lines[i]);
		}
	}
}

static int
bufsearchforwards(Buffer *buf, const Rune *s, size_t len, size_t row, size_t *found)
{
	size_t i, j;

	if (len == 0 || row + 1 >= buf->len)
		return 1;

	i = row + 1;
	j = 0;

	for (;;) {
		if (buf->lines[i][j] == s[0] && buflookingat(buf, s, len, i, j)) {
			if (found)
				*found = i;
			return 0;
		}

		if (buf->lines[i][++j] == RUNE_EOF) {
			if (++i == buf->len)
				return 1;
			j = 0;
		}
	}
}

static Window *
winnew(size_t rows, size_t cols)
{
	Window *win;

	win = xmalloc(sizeof(*win));
	win->buf = bufnew(cols);
	win->rows = rows;
	win->cols = cols;
	win->row = 0;
	return win;
}

static void
winfree(Window *win)
{
	buffree(win->buf);
	free(win);
}

static void
winfill(Window *win, Input *in)
{
	size_t newrows;

	newrows = 0;
	while (win->buf->len < win->rows)
		if (wingetline(win, in))
			break;
		else
			newrows++;

	win->row += newrows;
}

static int
wingetline(Window *win, Input *in)
{
	Rune *line;
	size_t i, w;
	Rune r;

	if (inputatend(in))
		return 1;

	line = bufnewline(win->buf);
	w = 0;
	for (i = 0; i < win->buf->linecap - 1; i++)
		if ((r = inputgetrune(in)) == RUNE_EOF) {
			break;
		} else if (w + printwidth(r) > win->cols) {
			inputungetrune(in, r);
			break;
		} else {
			line[i] = r;
			if (r == '\t')
				w = nexttabstop(w);
			else
				w += printwidth(r);
			if (r == '\n')
				break;
		}

	return 0;
}

static void
winresize(Window *win, size_t rows, size_t cols, Input *in)
{
	win->rows = rows;
	win->buf = bufreflow(win->buf, cols, win->row, &win->row);
	winfill(win, in);
}

static void
winscrollbot(Window *win, Input *in)
{
	while (!wingetline(win, in))
		;
	win->row = win->buf->len;
}

static void
winscrolldown(Window *win, size_t lines, Input *in)
{
	while (win->buf->len < win->row + lines) {
		if (wingetline(win, in))
			break;
	}

	win->row += lines;
	if (win->row > win->buf->len)
		win->row = win->buf->len;
}

static void
winscrolltop(Window *win)
{
	win->row = MIN(win->rows, win->buf->len);
}

static void
winscrollup(Window *win, size_t lines)
{
	win->row = lines > win->row ? 0 : win->row - lines;
	if (win->row < win->rows)
		win->row = MIN(win->rows, win->buf->len);
}

static void
winsearchbackwards(Window *win, const Rune *s, size_t len)
{
	size_t row;

	if (win->row == 0 || win->buf->len == 0 || win->row <= win->rows)
		return;

	row = win->row - win->rows;
	if (bufsearchbackwards(win->buf, s, len, row, &row))
		return;
	win->row = MIN(row + win->rows, win->buf->len);
}

static void
winsearchforwards(Window *win, const Rune *s, size_t len, Input *in)
{
	size_t row;

	if (win->row == 0 || win->buf->len == 0)
		return;
	row = win->row - 1;

	while (bufsearchforwards(win->buf, s, len, row, &row)) {
		row = win->buf->len - 1;
		if (wingetline(win, in))
			return;
	}
	win->row = row + 1;
}

static Input *
inputnew(FILE *file)
{
	Input *in;

	in = xmalloc(sizeof(*in));
	in->file = file;
	in->buflen = 0;
	in->unread = RUNE_EOF;
	return in;
}

static void
inputfree(Input *in)
{
	fclose(in->file);
	free(in);
}

static int
inputatend(Input *in)
{
	return in->buflen == 0 && in->unread == RUNE_EOF && (feof(in->file) || ferror(in->file));
}

static Rune
inputgetrune(Input *in)
{
	size_t i, len;
	int c;
	Rune r;

	if (in->unread != RUNE_EOF) {
		r = in->unread;
		in->unread = RUNE_EOF;
		return r;
	}

	for (; in->buflen < LEN(in->buf); i++)
		if ((c = fgetc(in->file)) != EOF)
			in->buf[in->buflen++] = c;
		else
			break;

	if (in->buflen == 0)
		return RUNE_EOF;

	len = utfdecode(in->buf, in->buflen, &r);
	for (i = len; i < in->buflen; i++)
		in->buf[i - len] = in->buf[i];
	in->buflen -= len;
	return r;
}

static void
inputungetrune(Input *in, Rune r)
{
	in->unread = r;
}

static Prompt *
promptnew(Rune prompt, int (*action)(Arg))
{
	Prompt *p;

	p = xmalloc(sizeof(*p));
	p->len = p->buflen = p->col = 0;
	p->cap = 128;
	p->text = xmalloc(p->cap * sizeof(*p->text));
	p->active = 0;
	p->prompt = prompt;
	p->action = action;
	return p;
}

static void
promptfree(Prompt *p)
{
	free(p->text);
	free(p);
}

static Rune
promptputchar(Prompt *p, char c)
{
	size_t rlen, i;
	Rune r;

	p->buf[p->buflen++] = c;
	rlen = utfpeeklen(p->buf[0]);
	if (rlen >= p->buflen) {
		rlen = utfdecode(p->buf, p->buflen, &r);
		for (i = rlen; i < p->buflen; i++)
			p->buf[i - rlen] = p->buf[i];
		p->buflen -= rlen;

		if (p->len == p->cap) {
			p->cap *= 2;
			p->text = xrealloc(p->text, p->cap * sizeof(*p->text));
		}
		p->text[p->len++] = r;
		return r;
	}

	return RUNE_INCOMPLETE;
}

static void
uiinit(void)
{
	struct sigaction sa;

	if (!(tty = fopen("/dev/tty", "r")))
		die(1, "no tty");

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = sigwinch;
	sigaction(SIGWINCH, &sa, NULL);
	sa.sa_handler = sigterm;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	tcgetattr(fileno(tty), &tsave);
	atexit(uiteardown);
	tcurr = tsave;
	tcurr.c_lflag &= ~(ECHO | ICANON);
	tcsetattr(fileno(tty), TCSANOW, &tcurr);
	setupterm(NULL, 1, NULL);
	putp(tparm(cursor_invisible, 0, 0, 0, 0, 0, 0, 0, 0, 0));
	putp(tparm(clear_screen, 0, 0, 0, 0, 0, 0, 0, 0, 0));
	fflush(stdout);
}

static void
uiteardown(void)
{
	putp(tparm(cursor_normal, 0, 0, 0, 0, 0, 0, 0, 0, 0));
	putchar('\n'); /* Make sure the cursor ends up on a new line */
	tcsetattr(fileno(tty), TCSANOW, &tsave);
	fflush(stdout);
}

static int
uigetkey(void)
{
	int c;

	errno = 0;
	while ((c = fgetc(tty)) == EOF)
		if (errno == EINTR) {
			if (winch) {
				winch = 0;
				return KEY_RESIZE;
			}
		} else {
			die(1, "could not get input key");
		}

	return c;
}

static void
uigetsize(size_t *rows, size_t *cols)
{
	struct winsize ws;

	if (ioctl(1, TIOCGWINSZ, &ws) < 0)
		die(1, "can't get terminal size");
	if (rows)
		*rows = ws.ws_row;
	if (cols)
		*cols = ws.ws_col;
}

static size_t
uiprint(Rune r, size_t col)
{
	size_t i, rlen, w;
	char buf[4];

	if (r == '\n') {
		return col;
	} else if (r == '\t') {
		w = nexttabstop(col) - col;
		if (col + w >= win->cols)
			w = win->cols - col - 1;
		for (i = 0; i < w; i++)
			putchar(' ');
		return col + w;
	}

	if (iscntrl(r))
		putp(tparm(enter_standout_mode, 0, 0, 0, 0, 0, 0, 0, 0, 0));
	rlen = sprintrune(buf, r);
	for (i = 0; i < rlen; i++)
		putchar(buf[i]);
	if (iscntrl(r))
		putp(tparm(exit_standout_mode, 0, 0, 0, 0, 0, 0, 0, 0, 0));

	w = printwidth(r);
	if (col + w > win->cols)
		col = 0;
	return col + w;
}

static void
uipromptkey(Prompt *p, char key)
{
	Rune r;

	if (key == KEY_RETURN) {
		p->active = 0;
		p->action((Arg){ 0 });
	} else if (key == KEY_ESCAPE) {
		p->len = p->col = 0;
		p->active = 0;
		uirefresh();
	} else if (key == KEY_BACKSPACE) {
		if (p->len > 0) {
			p->col -= printwidth(--p->len);
			printf("\b \b");
			fflush(stdout);
		}
	} else {
		r = promptputchar(p, key);
		if (r != RUNE_INCOMPLETE) {
			p->col = uiprint(r, p->col);
			fflush(stdout);
		}
	}
}

static void
uipromptopen(Prompt *p)
{
	p->len = 0;
	putp(tparm(cursor_address, win->rows - 1, 0, 0, 0, 0, 0, 0, 0, 0));
	p->col = uiprint(p->prompt, 0);
	fflush(stdout);
	p->active = 1;
}

static void
uirefresh(void)
{
	size_t i, col, start;
	Rune *line;

	putp(tparm(clear_screen, 0, 0, 0, 0, 0, 0, 0, 0, 0));
	start = win->row >= win->rows ? win->row - win->rows : 0;
	if (win->row < win->rows)
		win->row = MIN(win->rows, win->buf->len);

	for (i = start; i < win->row; i++) {
		col = 0;
		if (i != start)
			printf("\r\n");
		for (line = win->buf->lines[i]; *line != RUNE_EOF; line++)
			col = uiprint(*line, col);
	}
	fflush(stdout);
}

static void
uiresize(void)
{
	size_t rows, cols;

	uigetsize(&rows, &cols);
	winresize(win, rows, cols, input);
	uirefresh();
}

static void
sigterm(int signo)
{
	USED(signo);
	exit(1);
}

static void
sigwinch(int signo)
{
	USED(signo);
	winch = 1;
}

int
main(int argc, char **argv)
{
	int key;
	size_t i, rows, cols;
	FILE *file;

	if (argc == 1) {
		file = stdin;
	} else if (argc == 2) {
		if (!(file = fopen(argv[1], "r")))
			die(1, "cannot open '%s'", argv[1]);
	} else {
		die(2, "usage: spg [file]");
	}

	if (isatty(fileno(file)))
		die(1, "input is a tty; provide input via file argument or pipe");
	uiinit();
	input = inputnew(file);
	uigetsize(&rows, &cols);
	win = winnew(rows, cols);
	search = promptnew('/', searchforwards);
	uiresize();

	for (;;) {
		key = uigetkey();
		if (key == KEY_RESIZE) {
			uiresize();
			continue;
		}

		if (search->active) {
			uipromptkey(search, key);
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
	promptfree(search);
	winfree(win);
	inputfree(input);
	return 0;
}
