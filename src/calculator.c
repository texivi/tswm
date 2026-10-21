#include "sylui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double acc, cur; static char op, disp[40] = "0"; static int fresh = 1, err, pb = -1;
static const char *LBL[5][4] = {{"C", "+/-", "%", "/"}, {"7", "8", "9", "*"}, {"4", "5", "6", "-"}, {"1", "2", "3", "+"}, {"0", "0", ".", "="}};
static void show(double v)
{
	if (err) { snprintf(disp, sizeof disp, "Error"); return; }
	snprintf(disp, sizeof disp, "%.12g", v);
}
static void apply(void)
{
	if (!op) { acc = cur; return; }
	if (op == '+') acc += cur; else if (op == '-') acc -= cur; else if (op == '*') acc *= cur;
	else if (op == '/') { if (cur == 0) err = 1; else acc /= cur; }
}
static void press_key(const char *k)
{
	if (err && strcmp(k, "C")) return;
	if ((k[0] >= '0' && k[0] <= '9') || k[0] == '.') {
		if (fresh) { snprintf(disp, sizeof disp, "%s", k[0] == '.' ? "0." : k); fresh = 0; }
		else if (k[0] == '.' ? !strchr(disp, '.') : 1) { if (strlen(disp) < 18) strcat(disp, k); }
		cur = atof(disp);
	} else if (!strcmp(k, "C")) { acc = cur = 0; op = 0; err = 0; fresh = 1; strcpy(disp, "0"); }
	else if (!strcmp(k, "+/-")) { cur = -cur; show(cur); }
	else if (!strcmp(k, "%")) { cur /= 100; show(cur); fresh = 1; }
	else if (!strcmp(k, "=")) { apply(); cur = acc; op = 0; show(cur); fresh = 1; }
	else { if (!fresh || !op) apply(); op = k[0]; cur = acc; show(acc); fresh = 1; }
}
static void geom(int r, int c, int *x, int *y, int *w, int *h)
{
	*x = 8 + c * 54; *y = 64 + r * 44; *w = 50; *h = 40;
	if (r == 4 && c == 0) *w = 104;
}
static int hit(int mx, int my)
{
	for (int r = 0; r < 5; r++) for (int c = 0; c < 4; c++) {
		if (r == 4 && c == 1) continue;
		int x, y, w, h; geom(r, c, &x, &y, &w, &h);
		if (mx >= x && mx < x + w && my >= y && my < y + h) return r * 4 + c;
	}
	return -1;
}
static void draw(App *a)
{
	cairo_t *c = a->cr;
	ui_field(c, 8, 8, a->W - 16, 44, "", 0);
	cairo_select_font_face(c, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL); cairo_set_font_size(c, 24);
	cairo_text_extents_t e; cairo_text_extents(c, disp, &e);
	ui_set(c, col_fg(BGC)); cairo_move_to(c, a->W - 16 - e.x_advance, 40); cairo_show_text(c, disp);
	for (int r = 0; r < 5; r++) for (int k = 0; k < 4; k++) {
		if (r == 4 && k == 1) continue;
		int x, y, w, h; geom(r, k, &x, &y, &w, &h);
		ui_button(c, x, y, w, h, LBL[r][k], pb == r * 4 + k ? 1 : (LBL[r][k][0] == '=' ? 2 : 0));
	}
}
static void press(App *a, int x, int y, int b, unsigned long t) { if (b == 1) pb = hit(x, y); }
static void release(App *a, int x, int y, int b)
{
	int h = hit(x, y);
	if (h >= 0 && h == pb) press_key(LBL[h / 4][h % 4]);
	pb = -1;
}
static void key(App *a, KeySym k, const char *t, int n, unsigned s)
{
	if (k == 0xff1b) { press_key("C"); return; }
	if (k == 0xff0d || k == 0xff8d) { press_key("="); return; }
	if (k == 0xff08) { int l = strlen(disp); if (!fresh && l > 1) { disp[l - 1] = 0; cur = atof(disp); } return; }
	if (n == 1 && strchr("0123456789.+-*/%=", t[0])) { char b[2] = {t[0], 0}; press_key(b); }
	else if (n == 1 && (t[0] == 'c' || t[0] == 'C')) press_key("C");
}
int main(void)
{
	App *a = app_new("Calculator", "calculator", 230, 290, 1);
	a->draw = draw; a->press = press; a->release = release; a->key = key;
	app_run(a);
	return 0;
}
