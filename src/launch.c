#include "sylui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char cmd[400]; static int pb = -1, interm;
static void run(App *a)
{
	if (!cmd[0]) return;
	char b[500];
	if (interm) snprintf(b, sizeof b, "xterm -e %s", cmd); else snprintf(b, sizeof b, "%s", cmd);
	ui_spawn(b); a->quit = 1;
}
static void draw(App *a)
{
	cairo_t *c = a->cr; int W = a->W; Col fg = col_fg(FACE);
	ui_icon(c, IC_RUN, 14, 14, 40);
	ui_text(c, 66, 22, "Type the name of the program to run:", fg, 0);
	ui_field(c, 66, 36, W - 80, 24, cmd, 1);
	ui_check(c, 66, 72, "Run in a terminal", interm);
	ui_button(c, W - 180, a->H - 38, 80, 26, "Run", pb == 0 ? 1 : 2);
	ui_button(c, W - 92, a->H - 38, 80, 26, "Cancel", pb == 1 ? 1 : 0);
}
static int hit(App *a, int x, int y)
{
	if (y < a->H - 38 || y >= a->H - 12) return -1;
	if (x >= a->W - 180 && x < a->W - 100) return 0;
	if (x >= a->W - 92 && x < a->W - 12) return 1;
	return -1;
}
static void press(App *a, int x, int y, int b, unsigned long t)
{
	if (b != 1) return;
	pb = hit(a, x, y);
	if (x >= 66 && x < 250 && y >= 72 && y < 88) interm = !interm;
}
static void release(App *a, int x, int y, int b)
{
	int h = hit(a, x, y);
	if (h >= 0 && h == pb) { if (h == 0) run(a); else a->quit = 1; }
	pb = -1;
}
static void key(App *a, KeySym k, const char *t, int n, unsigned s)
{
	if (k == 0xff0d) run(a); else if (k == 0xff1b) a->quit = 1; else ui_edit(cmd, sizeof cmd, k, t, n);
}
int main(void)
{
	App *a = app_new("Launch", "launch", 420, 140, 1);
	a->draw = draw; a->press = press; a->release = release; a->key = key;
	app_run(a);
	return 0;
}
