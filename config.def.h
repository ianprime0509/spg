/* Cosmetic configuration */
#define TABWIDTH 8

/*
 * Keybindings are defined in the following format:
 * { key, function, argument }
 * where argument is a union whose members are named after their types'
 * printf(3) format strings. For a function that doesn't take an argument, any
 * value for the argument will do ({ 0 } expresses this well).
 *
 * For reference, here is a list of the functions provided:
 * pagedown(lf) - scroll down by lf screens
 * pageup(lf) - scroll up by lf screens
 * scrolldown(zu) - scroll down by zu lines
 * scrollup(zu) - scroll up by zu lines
 * scrolltop() - scroll to the top of the document
 * scrollbot() - scroll to the bottom of the document
 * quit() - exit spg
 */
static Key keys[] = {
	{ 'j', scrolldown, { .zu = 1 } },
	{ 'k', scrollup, { .zu = 1 } },
	{ 'g', scrolltop, { 0 } },
	{ 'G', scrollbot, { 0 } },
	{ 'd', pagedown, { .lf = 0.5 } },
	{ 'u', pageup, { .lf = 0.5 } },
	{ 'f', pagedown, { .lf = 1.0 } },
	{ 'b', pageup, { .lf = 1.0 } },
	{ '/', promptsearch, { 0 } },
	{ 'q', quit, { 0 } },
};
