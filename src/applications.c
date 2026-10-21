#define _GNU_SOURCE
#include "sylui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct { char name[64], exec[400]; int icon; cairo_surface_t *sic; int sec; } Item;
static Item *items; static int nitems, *vis, nvis, sel, scroll;
static char query[128];
enum { CW = 120, CH = 80, TOP = 44, SB = 16 };

static int cmp(const void *a, const void *b)
{
	const Item *x = a, *y = b;
	if (x->sec != y->sec) return x->sec - y->sec;
	return strcasecmp(x->name, y->name);
}
static void filter(void)
{
	nvis = 0;
	for (int i = 0; i < nitems; i++) if (!query[0] || strcasestr(items[i].name, query)) vis[nvis++] = i;
	if (sel >= nvis) sel = nvis - 1;
	if (sel < 0) sel = 0;
	scroll = 0;
}
static int cols(App *a) { int c = (a->W - SB - 8) / CW; return c < 1 ? 1 : c; }
static void launch(App *a)
{
	if (nvis) ui_spawn(items[vis[sel]].exec);
	else if (query[0]) ui_spawn(query);
	else return;
	a->quit = 1;
}
static void draw(App *a)
{
	cairo_t *c = a->cr; int W = a->W, H = a->H; Col fg = col_fg(BGC);
	ui_icon(c, IC_SEARCH, 8, 6, 28);
	ui_field(c, 42, 8, W - 52, 26, query, 1);
	ui_rect(c, 0, TOP, W - SB, H - TOP, BGC);
	cairo_save(c); cairo_rectangle(c, 0, TOP, W - SB, H - TOP); cairo_clip(c);
	int nc = cols(a), rows = (H - TOP) / CH;
	if (!nvis) ui_text(c, 16, TOP + 20, query[0] ? "No match. Press Enter to run it as a command." : "No applications.", fg, 0);
	for (int i = 0; i < nvis; i++) {
		int r = i / nc - scroll; if (r < 0 || r > rows) continue;
		int x = 8 + (i % nc) * CW, y = TOP + r * CH; Item *it = &items[vis[i]]; Col tc = fg;
		if (i == sel) { cairo_set_source_rgba(c, T.col[C_SEL].r, T.col[C_SEL].g, T.col[C_SEL].b, .9); cairo_rectangle(c, x, y + 2, CW - 4, CH - 4); cairo_fill(c); tc = col_fg(T.col[C_SEL]); }
		if (it->sic) {
			double w = cairo_image_surface_get_width(it->sic), h = cairo_image_surface_get_height(it->sic), sc = 40.0 / (w > h ? w : h);
			cairo_save(c); cairo_new_path(c); cairo_translate(c, x + (CW - 4 - w * sc) / 2, y + 8 + (40 - h * sc) / 2); cairo_scale(c, sc, sc);
			cairo_set_source_surface(c, it->sic, 0, 0); cairo_paint(c); cairo_restore(c);
		} else if (it->icon == IC_FILE) ui_badge(c, x + (CW - 4 - 40) / 2, y + 8, 40, it->name);
		else ui_icon(c, it->icon, x + (CW - 4 - 40) / 2, y + 8, 40);
		ui_text_fit(c, x + 4, y + 62, it->name, CW - 12, tc, 0);
	}
	cairo_restore(c);
	int tot = (nvis + nc - 1) / nc;
	ui_vscroll(c, W - SB, TOP, SB, H - TOP, scroll, rows, tot);
}
static void clamp(App *a) { int tot = (nvis + cols(a) - 1) / cols(a), rows = (a->H - TOP) / CH, m = tot - rows; if (m < 0) m = 0; if (scroll > m) scroll = m; if (scroll < 0) scroll = 0; }
static void press(App *a, int x, int y, int b, unsigned long t)
{
	if (b == 4 || b == 5) { scroll += b == 4 ? -1 : 1; clamp(a); return; }
	if (b != 1 || y < TOP) return;
	if (x >= a->W - SB) { scroll += y < a->H / 2 ? -1 : 1; clamp(a); return; }
	int nc = cols(a), cx = (x - 8) / CW, i = ((y - TOP) / CH + scroll) * nc + cx;
	if (x >= 8 && cx < nc && i < nvis) { sel = i; if (app_isdbl(a, t, i)) launch(a); }
}
static void key(App *a, KeySym k, const char *t, int n, unsigned s)
{
	int nc = cols(a);
	if (k == 0xff1b) a->quit = 1;
	else if (k == 0xff0d) launch(a);
	else if (k == 0xff53) { if (sel + 1 < nvis) sel++; }
	else if (k == 0xff51) { if (sel > 0) sel--; }
	else if (k == 0xff54) { if (sel + nc < nvis) sel += nc; }
	else if (k == 0xff52) { if (sel - nc >= 0) sel -= nc; }
	else { char old[128]; snprintf(old, sizeof old, "%s", query); ui_edit(query, sizeof query, k, t, n); if (strcmp(old, query)) filter(); }
	int r = sel / nc, rows = (a->H - TOP) / CH;
	if (r < scroll) scroll = r;
	if (r >= scroll + rows) scroll = r - rows + 1;
	clamp(a);
}
int main(void)
{
	theme_load();
	AppEnt *list = calloc(1024, sizeof *list); int n = ui_apps_scan(list, 1024);
	items = calloc(UI_NOWN + n, sizeof *items); vis = calloc(UI_NOWN + n, sizeof *vis);
	for (int i = 0; i < UI_NOWN; i++) { Item *it = &items[nitems++]; snprintf(it->name, 64, "%s", UI_OWN[i].name); snprintf(it->exec, 400, "%s", UI_OWN[i].cmd); it->icon = UI_OWN[i].icon; it->sec = UI_OWN[i].sec; }
	for (int i = 0; i < n; i++) {
		int dup = 0; for (int j = 0; j < UI_NOWN; j++) if (!strcasecmp(items[j].name, list[i].name)) dup = 1;
		if (dup) continue;
		Item *it = &items[nitems++]; snprintf(it->name, 64, "%s", list[i].name); snprintf(it->exec, 400, "%s", list[i].exec);
		it->sec = list[i].section; it->icon = IC_FILE; it->sic = ui_icon_load(list[i].icon, 40);
	}
	free(list);
	qsort(items, nitems, sizeof *items, cmp);
	filter();
	App *a = app_new("Applications", "applications", 640, 460, 0);
	a->draw = draw; a->press = press; a->key = key;
	app_run(a);
	return 0;
}
