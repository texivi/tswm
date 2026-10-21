#include "sylui.h"
#include <stdio.h>
#include <string.h>

static const struct { const char *name; int from, to; } RNG[] = {
	{"Basic Latin", 0x20, 0x7F}, {"Latin-1 Supplement", 0xA0, 0xFF}, {"Latin Extended-A", 0x100, 0x17F}, {"Greek", 0x370, 0x3FF},
	{"Cyrillic", 0x400, 0x4BF}, {"Punctuation", 0x2000, 0x206F}, {"Currency", 0x20A0, 0x20CF}, {"Arrows", 0x2190, 0x21FF},
	{"Math Operators", 0x2200, 0x22BF}, {"Box Drawing", 0x2500, 0x257F}, {"Shapes", 0x25A0, 0x25FF}, {"Dingbats", 0x2700, 0x27BF},
};
#define NR ((int)(sizeof RNG / sizeof RNG[0]))
enum { CELL = 24, GX = 150, GY = 8, LW = 140, TXT = 400 };
static int rng, sel = -1, pb = -1; static char text[256];

static void u8(int cp, char *o)
{
	if (cp < 0x80) { o[0] = cp; o[1] = 0; }
	else if (cp < 0x800) { o[0] = 0xC0 | cp >> 6; o[1] = 0x80 | (cp & 63); o[2] = 0; }
	else { o[0] = 0xE0 | cp >> 12; o[1] = 0x80 | (cp >> 6 & 63); o[2] = 0x80 | (cp & 63); o[3] = 0; }
}
static void draw(App *a)
{
	cairo_t *c = a->cr; Col fg = col_fg(FACE);
	ui_rect(c, 8, 8, LW, TXT - 18, BGC); ui_bevel(c, 8, 8, LW, TXT - 18, 1);
	for (int i = 0; i < NR; i++) {
		Col tc = col_fg(BGC);
		if (i == rng) { ui_rect(c, 10, 10 + i * 22, LW - 4, 22, T.col[C_SEL]); tc = col_fg(T.col[C_SEL]); }
		ui_text_fit(c, 16, 21 + i * 22, RNG[i].name, LW - 16, tc, 0);
	}
	int n = RNG[rng].to - RNG[rng].from + 1, rows = (n + 15) / 16;
	char savefont[64]; snprintf(savefont, sizeof savefont, "%s", T.font);
	ui_rect(c, GX, GY, 16 * CELL + 1, rows * CELL + 1, BGC);
	ui_set(c, T.shadow); cairo_set_line_width(c, 1);
	for (int i = 0; i <= 16; i++) { cairo_move_to(c, GX + i * CELL + .5, GY); cairo_line_to(c, GX + i * CELL + .5, GY + rows * CELL); }
	for (int i = 0; i <= rows; i++) { cairo_move_to(c, GX, GY + i * CELL + .5); cairo_line_to(c, GX + 16 * CELL, GY + i * CELL + .5); }
	cairo_stroke(c);
	for (int i = 0; i < n; i++) {
		int x = GX + (i % 16) * CELL, y = GY + (i / 16) * CELL; char g[8]; Col k = col_fg(BGC);
		if (i == sel) { ui_rect(c, x + 1, y + 1, CELL - 1, CELL - 1, T.col[C_SEL]); k = col_fg(T.col[C_SEL]); }
		snprintf(T.font, sizeof T.font, "DejaVu Sans"); u8(RNG[rng].from + i, g); ui_text_c(c, x + CELL / 2 + .5, y + CELL / 2, g, k, 0); snprintf(T.font, sizeof T.font, "%s", savefont);
	}
	ui_group(c, GX, GY + 12 * CELL + 6, 16 * CELL + 1, 70, NULL);
	ui_text(c, GX + 8, GY + 12 * CELL + 28, "Text:", fg, 0); ui_field(c, GX + 48, GY + 12 * CELL + 17, 16 * CELL - 150, 24, text, 1);
	ui_button(c, GX + 16 * CELL - 94, GY + 12 * CELL + 17, 86, 24, "Copy", pb == 1 ? 1 : 0);
	char info[80] = "";
	if (sel >= 0) snprintf(info, sizeof info, "U+%04X", RNG[rng].from + sel);
	ui_text(c, GX + 8, GY + 12 * CELL + 52, info, fg, 0);
}
static void press(App *a, int x, int y, int b, unsigned long t)
{
	if (b != 1) return;
	if (x >= 10 && x < LW + 4 && y >= 10 && (y - 10) / 22 < NR) { rng = (y - 10) / 22; sel = -1; return; }
	int n = RNG[rng].to - RNG[rng].from + 1;
	if (x >= GX && y >= GY && (x - GX) / CELL < 16) {
		int i = (y - GY) / CELL * 16 + (x - GX) / CELL;
		if (i < n) {
			sel = i;
			if (app_isdbl(a, t, i)) { char g[8]; u8(RNG[rng].from + i, g); if (strlen(text) + strlen(g) < sizeof text) strcat(text, g); }
		}
		return;
	}
	if (x >= GX + 16 * CELL - 94 && y >= GY + 12 * CELL + 17 && y < GY + 12 * CELL + 41) pb = 1;
}
static void release(App *a, int x, int y, int b) { if (pb == 1 && x >= GX + 16 * CELL - 94 && y >= GY + 12 * CELL + 17 && y < GY + 12 * CELL + 41) app_copy(a, text); pb = -1; }
static void key(App *a, KeySym k, const char *t, int n, unsigned s) { if (k == 0xff0d && sel >= 0) { char g[8]; u8(RNG[rng].from + sel, g); if (strlen(text) + strlen(g) < sizeof text) strcat(text, g); } else ui_edit(text, sizeof text, k, t, n); }
int main(void)
{
	App *a = app_new("CharMap", "charmap", GX + 16 * CELL + 10, GY + 12 * CELL + 92, 1);
	a->draw = draw; a->press = press; a->release = release; a->key = key;
	app_run(a);
	return 0;
}
