#include "sylui.h"
#include <stdio.h>
#include <string.h>

static int down;
static void big(cairo_t *c, double cx, double y, double sz, int bold, int slant, const char *s, Col k)
{
	cairo_select_font_face(c, "DejaVu Sans", slant ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(c, sz);
	cairo_text_extents_t e; cairo_text_extents(c, s, &e);
	ui_set(c, k); cairo_move_to(c, cx - e.x_advance / 2, y); cairo_show_text(c, s);
}
static cairo_surface_t *logo;
static void draw(App *a)
{
	cairo_t *c = a->cr; int W = a->W; Col fg = col_fg(FACE);
	if (!logo) { char p[900]; snprintf(p, sizeof p, "%s/icons/tswm-logo.png", ui_datadir()); logo = ui_image_load(p); }
	int top = 16;
	if (logo) { /* the Syllable wordmark */
		double iw = cairo_image_surface_get_width(logo), ih = cairo_image_surface_get_height(logo), k = 230.0 / iw;
		cairo_save(c); cairo_new_path(c); cairo_translate(c, (W - 230) / 2.0, top); cairo_scale(c, k, k); cairo_set_source_surface(c, logo, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(c), CAIRO_FILTER_BEST); cairo_paint(c); cairo_restore(c); top += (int)(ih * k);
	}
	big(c, W / 2, top + 36, 18, 1, 0, "TSWM 1.0.0", HEX(0x1C4A98));
	big(c, W / 2, top + 58, 12, 0, 0, "Texivi's Syllable Window Manager", fg);
	big(c, W / 2, top + 80, 12, 0, 1, "the retro syllable desktop experience", fg);
	ui_text_c(c, W / 2, top + 112, "\xC2\xA9 2026 texivi", fg, 0);
	ui_text_c(c, W / 2, top + 132, "TSWM is free software released under the GNU GPL v3.", fg, 0);
	ui_button(c, W / 2 - 40, a->H - 40, 80, 24, "_Ok", down ? 1 : 2);
}
static int inok(App *a, int x, int y) { return x >= a->W / 2 - 40 && x < a->W / 2 + 40 && y >= a->H - 40 && y < a->H - 16; }
static void press(App *a, int x, int y, int b, unsigned long t) { if (b == 1 && inok(a, x, y)) down = 1; }
static void release(App *a, int x, int y, int b) { if (down && inok(a, x, y)) a->quit = 1; down = 0; }
static void key(App *a, KeySym k, const char *t, int n, unsigned s) { if (k == 0xff0d || k == 0xff1b) a->quit = 1; }
int main(void)
{
	App *a = app_new("About TSWM", "about", 400, 310, 1);
	a->draw = draw; a->press = press; a->release = release; a->key = key;
	app_run(a);
	return 0;
}
