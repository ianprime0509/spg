static Key keys[] = {
	{ 'j', scrolldown, { .zu = 1 } },
	{ 'k', scrollup, { .zu = 1 } },
	{ 'g', scrolltop, { 0 } },
	{ 'G', scrollbot, { 0 } },
	{ 'd', pagedown, { .lf = 0.5 } },
	{ 'u', pageup, { .lf = 0.5 } },
	{ 'f', pagedown, { .lf = 1.0 } },
	{ 'b', pageup, { .lf = 1.0 } },
	{ 'q', quit, { 0 } },
};
