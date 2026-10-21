#include "sylui.h"
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { const char *label; int icon; } B[5] = {{"Lock", IC_LOCK}, {"Suspend", IC_SUSPEND}, {"Restart", IC_RESTART}, {"Shut Down", IC_POWER}, {"Log Out", IC_BACK}};
static int hover = -1, down = -1;
static void draw(App *a)
{
	cairo_t *c = a->cr; Col fg = col_fg(FACE);
	ui_text(c, 16, 20, "What do you want to do?", fg, 1);
	for (int i = 0; i < 5; i++) {
		int x = 12 + i * 80, y = 36;
		if (i == down) ui_bevel(c, x, y, 76, 84, 1); else if (i == hover) ui_bevel(c, x, y, 76, 84, 0);
		ui_icon(c, B[i].icon, x + 14 + (i == down), y + 8 + (i == down), 48);
		ui_text_c(c, x + 38 + (i == down), y + 70 + (i == down), B[i].label, fg, 0);
	}
	ui_button(c, a->W - 92, a->H - 36, 80, 26, "Cancel", 0);
}
static int at(int x, int y) { for (int i = 0; i < 5; i++) if (x >= 12 + i * 80 && x < 88 + i * 80 && y >= 36 && y < 120) return i; return -1; }
static void act(App *a, int i)
{
	switch (i) {
	case 0: ui_spawn("lock"); break;
	case 1: ui_spawn("systemctl suspend"); break;
	case 2: ui_spawn("systemctl reboot"); break;
	case 3: ui_spawn("systemctl poweroff"); break;
	case 4: XChangeProperty(a->d, DefaultRootWindow(a->d), XInternAtom(a->d, "TSWM_CMD", False), XInternAtom(a->d, "UTF8_STRING", False), 8, PropModeReplace, (unsigned char *)"logout", 6); XFlush(a->d); break;
	}
	a->quit = 1;
}
static void press(App *a, int x, int y, int b, unsigned long t) { if (b == 1) { down = at(x, y); if (down < 0 && x >= a->W - 92 && y >= a->H - 36) a->quit = 1; } }
static void release(App *a, int x, int y, int b) { if (b == 1 && down >= 0 && at(x, y) == down) act(a, down); down = -1; }
static void motion(App *a, int x, int y, unsigned s) { hover = at(x, y); }
static void key(App *a, KeySym k, const char *t, int n, unsigned s) { if (k == 0xff1b) a->quit = 1; }
int main(void)
{
	App *a = app_new("Shut Down", "powerdialog", 424, 170, 1);
	a->draw = draw; a->press = press; a->release = release; a->motion = motion; a->key = key;
	app_run(a);
	return 0;
}
