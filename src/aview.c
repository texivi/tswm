#define _GNU_SOURCE
#include "sylui.h"
#include <math.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static cairo_surface_t *img; static char path[1024]; static double zoom; static int fit = 1, panx, pany, pb = -1, drag, dx, dy, msg_t; static const char *msg = "";
static int BH = 30;
static void zfit(App *a)
{
	if (!img) return;
	double iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
	zoom = fmin((a->W) / iw, (a->H - BH) / ih); if (zoom > 1) zoom = 1;
}
static const char *LB[3] = {"Fit", "100%", "Set as Wallpaper"};
static void bgeom(App *a, int i, int *x, int *w)
{
	int ws[3] = {50, 60, 140}; *x = 6; for (int k = 0; k < i; k++) *x += ws[k] + 6; *w = ws[i];
}
static void draw(App *a)
{
	cairo_t *c = a->cr;
	ui_rect(c, 0, 0, a->W, a->H - BH, HEX(0x202020));
	if (img) {
		if (fit) zfit(a);
		double iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
		double x = (a->W - iw * zoom) / 2 + panx, y = (a->H - BH - ih * zoom) / 2 + pany;
		cairo_save(c); cairo_rectangle(c, 0, 0, a->W, a->H - BH); cairo_clip(c);
		cairo_translate(c, x, y); cairo_scale(c, zoom, zoom); cairo_set_source_surface(c, img, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(c), zoom < 1 ? CAIRO_FILTER_BEST : CAIRO_FILTER_NEAREST); cairo_paint(c);
		cairo_restore(c);
	} else ui_text_c(c, a->W / 2, a->H / 2 - BH, path[0] ? "Cannot open this image." : "No image. Pass an image path.", HEX(0xCCCCCC), 0);
	ui_rect(c, 0, a->H - BH, a->W, BH, FACE); ui_rect(c, 0, a->H - BH, a->W, 1, T.light);
	for (int i = 0; i < 3; i++) { int x, w; bgeom(a, i, &x, &w); ui_button(c, x, a->H - BH + 4, w, 22, LB[i], pb == i ? 1 : 0); }
	char s[160]; snprintf(s, sizeof s, "%s", msg[0] ? msg : "");
	if (!s[0] && img) snprintf(s, sizeof s, "%dx%d  %.0f%%", cairo_image_surface_get_width(img), cairo_image_surface_get_height(img), zoom * 100);
	ui_text(c, 270, a->H - BH / 2, s, col_fg(FACE), 0);
}
static int hit(App *a, int x, int y)
{
	if (y < a->H - BH + 4 || y >= a->H - BH + 26) return -1;
	for (int i = 0; i < 3; i++) { int bx, w; bgeom(a, i, &bx, &w); if (x >= bx && x < bx + w) return i; }
	return -1;
}
static void press(App *a, int x, int y, int b, unsigned long t)
{
	if (b == 4 || b == 5) { fit = 0; zoom *= b == 4 ? 1.15 : 1 / 1.15; return; }
	if (b != 1) return;
	pb = hit(a, x, y);
	if (pb < 0 && y < a->H - BH) { drag = 1; dx = x - panx; dy = y - pany; }
}
static void motion(App *a, int x, int y, unsigned st) { if (drag) { panx = x - dx; pany = y - dy; } }
static void release(App *a, int x, int y, int b)
{
	drag = 0;
	if (pb >= 0 && hit(a, x, y) == pb) {
		if (pb == 0) { fit = 1; panx = pany = 0; }
		else if (pb == 1) { fit = 0; zoom = 1; panx = pany = 0; }
		else if (path[0]) { snprintf(T.wall, sizeof T.wall, "%s", path); theme_save(); ui_notify_theme(a->d); msg = "Wallpaper set."; }
	}
	pb = -1;
}
static void key(App *a, KeySym k, const char *t, int n, unsigned s)
{
	if (k == 0xff1b || (n == 1 && t[0] == 'q')) a->quit = 1;
	else if (n == 1 && (t[0] == '+' || t[0] == '=')) { fit = 0; zoom *= 1.25; }
	else if (n == 1 && t[0] == '-') { fit = 0; zoom /= 1.25; }
	else if (n == 1 && t[0] == '1') { fit = 0; zoom = 1; panx = pany = 0; }
	else if (n == 1 && t[0] == 'f') { fit = 1; panx = pany = 0; }
}
int main(int argc, char **argv)
{
	if (argc > 1 && realpath(argv[1], path)) {
		img = ui_image_load(path);
	}
	char title[1100]; snprintf(title, sizeof title, "%s - Image Viewer", path[0] ? strrchr(path, '/') + 1 : "Image Viewer");
	App *a = app_new(title, "aview", 720, 520, 0);
	a->draw = draw; a->press = press; a->release = release; a->motion = motion; a->key = key;
	app_run(a);
	return 0;
}
