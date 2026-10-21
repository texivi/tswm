#include <cairo/cairo.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define W 1600
#define H 1000
#define RGB(v) ((v) >> 16 & 255) / 255.0, ((v) >> 8 & 255) / 255.0, ((v) & 255) / 255.0

static unsigned rs = 12345;
static double rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (rs & 0xFFFFFF) / (double)0x1000000; }
static void stop(cairo_pattern_t *p, double o, unsigned v) { cairo_pattern_add_color_stop_rgb(p, o, RGB(v)); }
static void stopa(cairo_pattern_t *p, double o, unsigned v, double a) { cairo_pattern_add_color_stop_rgba(p, o, RGB(v), a); }
static void vgrad(cairo_t *c, unsigned a, unsigned b)
{
	cairo_pattern_t *p = cairo_pattern_create_linear(0, 0, 0, H); stop(p, 0, a); stop(p, 1, b);
	cairo_set_source(c, p); cairo_paint(c); cairo_pattern_destroy(p);
}
static void stars(cairo_t *c, int n, double maxy)
{
	for (int i = 0; i < n; i++) {
		double x = rnd() * W, y = rnd() * maxy, r = rnd() < .08 ? 1.6 : .8;
		cairo_set_source_rgba(c, 1, 1, 1, .3 + rnd() * .7); cairo_arc(c, x, y, r, 0, 6.2832); cairo_fill(c);
	}
}
static double ridge(double x, double seed, double amp)
{
	return amp * (.5 * sin(x * .004 + seed) + .3 * sin(x * .011 + seed * 2.3) + .2 * sin(x * .029 + seed * 5.1));
}

static void wave(cairo_t *c)
{
	vgrad(c, 0x6FA8DC, 0x1B4F8C);
	for (int i = 0; i < 3; i++) {
		double o = i * 90;
		cairo_move_to(c, 0, 620 + o);
		cairo_curve_to(c, 500, 380 + o, 1000, 760 + o, W, 330 + o);
		cairo_line_to(c, W, 330 + o + 260); cairo_curve_to(c, 1000, 760 + o + 300, 500, 380 + o + 260, 0, 620 + o + 330); cairo_close_path(c);
		cairo_pattern_t *p = cairo_pattern_create_linear(0, 300 + o, W, 700 + o);
		stopa(p, 0, 0xFFFFFF, 0); stopa(p, .5, 0xFFFFFF, .22 - i * .05); stopa(p, 1, 0xFFFFFF, 0);
		cairo_set_source(c, p); cairo_fill(c); cairo_pattern_destroy(p);
	}
	cairo_pattern_t *g = cairo_pattern_create_radial(1250, 120, 0, 1250, 120, 700); stopa(g, 0, 0xFFFFFF, .35); stopa(g, 1, 0xFFFFFF, 0);
	cairo_set_source(c, g); cairo_paint(c); cairo_pattern_destroy(g);
}
static void dusk(cairo_t *c)
{
	cairo_pattern_t *p = cairo_pattern_create_linear(0, 0, 0, H * .7);
	stop(p, 0, 0x120A2C); stop(p, .45, 0x5B2A6B); stop(p, .8, 0xE8685A); stop(p, 1, 0xF8C070);
	cairo_set_source(c, p); cairo_paint(c); cairo_pattern_destroy(p);
	stars(c, 160, 380);
	cairo_pattern_t *g = cairo_pattern_create_radial(W * .62, 640, 0, W * .62, 640, 420); stopa(g, 0, 0xFFE0A0, .9); stopa(g, .25, 0xFFB070, .4); stopa(g, 1, 0xFF8060, 0);
	cairo_set_source(c, g); cairo_paint(c); cairo_pattern_destroy(g);
	cairo_arc(c, W * .62, 640, 74, 0, 6.2832); cairo_set_source_rgb(c, RGB(0xFFE9B0)); cairo_fill(c);
	unsigned cols[5] = {0x7A3A70, 0x5A2A68, 0x3E2058, 0x261648, 0x120C30};
	for (int l = 0; l < 5; l++) {
		double base = 640 + l * 70, amp = 120 - l * 14;
		cairo_move_to(c, 0, H);
		for (int x = 0; x <= W; x += 8) cairo_line_to(c, x, base - fabs(ridge(x, l * 1.7 + 1, amp)) - 40 + l * 10);
		cairo_line_to(c, W, H); cairo_close_path(c); cairo_set_source_rgb(c, RGB(cols[l])); cairo_fill(c);
	}
}
static void planet(cairo_t *c)
{
	vgrad(c, 0x05060E, 0x1A1426);
	stars(c, 260, 700);
	double px = 1080, py = 330, pr = 250;
	cairo_pattern_t *g = cairo_pattern_create_radial(px, py, pr, px, py, pr * 1.8); stopa(g, 0, 0xE8A070, .35); stopa(g, 1, 0xE8A070, 0);
	cairo_set_source(c, g); cairo_paint(c); cairo_pattern_destroy(g);
	cairo_save(c); cairo_arc(c, px, py, pr, 0, 6.2832); cairo_clip(c);
	cairo_pattern_t *b = cairo_pattern_create_linear(px, py - pr, px, py + pr);
	stop(b, 0, 0xD9B48C); stop(b, .2, 0xB8825A); stop(b, .4, 0xE0C09A); stop(b, .6, 0x9C6444); stop(b, .8, 0xC89870); stop(b, 1, 0x7A4A34);
	cairo_set_source(c, b); cairo_paint(c); cairo_pattern_destroy(b);
	cairo_pattern_t *sh = cairo_pattern_create_radial(px - pr * .5, py - pr * .4, pr * .2, px - pr * .5, py - pr * .4, pr * 1.6);
	stopa(sh, 0, 0, 0); stopa(sh, .55, 0, .1); stopa(sh, 1, 0, .85);
	cairo_set_source(c, sh); cairo_paint(c); cairo_pattern_destroy(sh);
	cairo_restore(c);
	cairo_arc(c, 330, 170, 26, 0, 6.2832); cairo_set_source_rgb(c, RGB(0x4A4A58)); cairo_fill(c);
	cairo_arc(c, 322, 162, 24, 0, 6.2832); cairo_set_source_rgb(c, RGB(0x8A8A9A)); cairo_fill(c);
	cairo_pattern_t *hz = cairo_pattern_create_linear(0, 560, 0, 760); stopa(hz, 0, 0x8A5A5A, 0); stopa(hz, 1, 0xB07860, .55);
	cairo_set_source(c, hz); cairo_rectangle(c, 0, 560, W, 200); cairo_fill(c); cairo_pattern_destroy(hz);
	for (int layer = 0; layer < 2; layer++) {
		unsigned col = layer ? 0x2A1A18 : 0x4A3030;
		for (int i = 0; i < 9; i++) {
			double x = (i * 190 + rnd() * 80 + layer * 90) - 40, w = 60 + rnd() * 70, top = 640 + layer * 90 - rnd() * 130, bot = H;
			cairo_move_to(c, x, bot); cairo_line_to(c, x + 6, top + 14); cairo_line_to(c, x + 16, top); cairo_line_to(c, x + w - 16, top + 4); cairo_line_to(c, x + w - 4, top + 20); cairo_line_to(c, x + w + 14, bot); cairo_close_path(c);
			cairo_pattern_t *m = cairo_pattern_create_linear(x, 0, x + w, 0); stop(m, 0, col + 0x201410); stop(m, 1, col - 0x0A0606 * (layer + 1) + 0x0A0606);
			cairo_set_source(c, m); cairo_fill(c); cairo_pattern_destroy(m);
		}
	}
	cairo_move_to(c, 0, H); for (int x = 0; x <= W; x += 10) cairo_line_to(c, x, 880 + ridge(x, 4, 40)); cairo_line_to(c, W, H); cairo_close_path(c);
	cairo_set_source_rgb(c, RGB(0x140C0C)); cairo_fill(c);
}
static void grid(cairo_t *c)
{
	cairo_set_source_rgb(c, RGB(0x020806)); cairo_paint(c);
	double hy = 520;
	cairo_pattern_t *g = cairo_pattern_create_radial(W / 2, hy, 0, W / 2, hy, 700); stopa(g, 0, 0x30FF70, .35); stopa(g, 1, 0x30FF70, 0);
	cairo_set_source(c, g); cairo_paint(c); cairo_pattern_destroy(g);
	cairo_arc(c, W / 2, hy, 190, M_PI, 2 * M_PI); cairo_close_path(c);
	cairo_pattern_t *s = cairo_pattern_create_linear(0, hy - 190, 0, hy); stop(s, 0, 0xB8FFC8); stop(s, 1, 0x20C860);
	cairo_set_source(c, s); cairo_fill(c); cairo_pattern_destroy(s);
	for (int i = 0; i < 12; i++) { cairo_set_source_rgb(c, RGB(0x020806)); cairo_rectangle(c, W / 2 - 200, hy - 150 + i * 13, 400, 1.5 + i * .5); cairo_fill(c); }
	cairo_set_source_rgb(c, RGB(0x020806)); cairo_rectangle(c, 0, hy, W, H - hy); cairo_fill(c);
	cairo_set_line_width(c, 1.6); cairo_set_source_rgba(c, .2, 1, .45, .75);
	for (int i = -30; i <= 30; i++) { cairo_move_to(c, W / 2 + i * 14, hy); cairo_line_to(c, W / 2 + i * 190, H); cairo_stroke(c); }
	for (int i = 1; i < 14; i++) { double y = hy + pow(i / 13.0, 2.2) * (H - hy); cairo_move_to(c, 0, y); cairo_line_to(c, W, y); cairo_stroke(c); }
	cairo_move_to(c, 0, hy); cairo_line_to(c, W, hy); cairo_set_source_rgb(c, RGB(0x70FFA0)); cairo_stroke(c);
	for (int y = 0; y < H; y += 3) { cairo_set_source_rgba(c, 0, 0, 0, .18); cairo_rectangle(c, 0, y, W, 1); cairo_fill(c); }
}
static void contour(cairo_t *c)
{
	cairo_set_source_rgb(c, RGB(0x12303A)); cairo_paint(c);
	double cx[2] = {520, 1180}, cy[2] = {420, 620};
	for (int b = 0; b < 2; b++)
		for (int k = 1; k < 42; k++) {
			double base = k * (b ? 17 : 21);
			for (int i = 0; i <= 360; i++) {
				double a = i * M_PI / 180, r = base * (1 + .16 * sin(a * 3 + b * 2 + k * .05) + .08 * sin(a * 5 + k * .09) + .05 * sin(a * 2 + 1 + b));
				double x = cx[b] + r * cos(a), y = cy[b] + r * .8 * sin(a);
				if (i) cairo_line_to(c, x, y); else cairo_move_to(c, x, y);
			}
			cairo_close_path(c);
			cairo_set_source_rgba(c, RGB(k % 5 ? 0x5FA8A0 : 0xE8D8A8), k % 5 ? .28 : .6);
			cairo_set_line_width(c, k % 5 ? 1.1 : 1.8); cairo_stroke(c);
		}
}
static void ribbons(cairo_t *c)
{
	cairo_pattern_t *p = cairo_pattern_create_linear(0, 0, W, H); stop(p, 0, 0x050514); stop(p, .5, 0x1A1040); stop(p, 1, 0x0A0820);
	cairo_set_source(c, p); cairo_paint(c); cairo_pattern_destroy(p);
	for (int i = 0; i < 9; i++) {
		double y0 = 100 + i * 105, sh = i * .5;
		cairo_move_to(c, -50, y0 + 300);
		cairo_curve_to(c, W * .3, y0 - 260 + sh * 30, W * .6, y0 + 520, W + 50, y0 - 200);
		cairo_curve_to(c, W * .6, y0 + 700, W * .3, y0 - 60, -50, y0 + 520); cairo_close_path(c);
		cairo_pattern_t *r = cairo_pattern_create_linear(0, y0, W, y0 + 200);
		stopa(r, 0, 0x5040C0, 0); stopa(r, .4, i % 2 ? 0x8070E8 : 0x4A60E0, .18); stopa(r, .8, 0xB090FF, .10); stopa(r, 1, 0x5040C0, 0);
		cairo_set_source(c, r); cairo_fill(c); cairo_pattern_destroy(r);
	}
	stars(c, 120, H);
}
static void dither(cairo_t *c)
{
	static const int bay[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
	int cell = 3, cw = W / cell + 1, ch = H / cell + 1;
	cairo_set_source_rgb(c, RGB(0x0B1220)); cairo_paint(c);
	cairo_set_source_rgb(c, RGB(0xD8C9A3));
	for (int y = 0; y < ch; y++) for (int x = 0; x < cw; x++) {
		double fx = x * cell, fy = y * cell;
		double v = (fy / H) * .9 + .12 * sin(fx * .006 + fy * .004) + .1 * sin(fx * .017 - fy * .009 + 1.3) + .05 * sin(fx * .04 + fy * .02);
		double d = hypot(fx - W * .72, fy - H * .3); v += 0.5 * exp(-d * d / (2 * 160.0 * 160.0)) - .1;
		v = v < 0 ? 0 : v > 1 ? 1 : v;
		if (v * 16 > bay[y & 3][x & 3]) { cairo_rectangle(c, x * cell, y * cell, cell, cell); }
	}
	cairo_fill(c);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	struct { const char *n; void (*f)(cairo_t *); } w[] = {
		{"syllable-wave", wave}, {"dusk-ridge", dusk}, {"dune-planet", planet}, {"phosphor-horizon", grid},
		{"contour-teal", contour}, {"violet-ribbons", ribbons}, {"dither-sea", dither}};
	for (unsigned i = 0; i < sizeof w / sizeof w[0]; i++) {
		rs = 12345 + i * 77;
		cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, W, H);
		cairo_t *c = cairo_create(s); w[i].f(c);
		char p[600]; snprintf(p, sizeof p, "%s/%s.png", dir, w[i].n);
		cairo_surface_write_to_png(s, p); cairo_destroy(c); cairo_surface_destroy(s);
		printf("%s\n", p);
	}
	return 0;
}
