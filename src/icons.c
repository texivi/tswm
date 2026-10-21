#include "sylui.h"
#include <math.h>
#include <stdio.h>

/* ---- drawing toolkit ---- */
static void rr(cairo_t *c, double x, double y, double w, double h, double r)
{
	cairo_new_sub_path(c);
	cairo_arc(c, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(c, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(c, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(c, x + r, y + r, r, M_PI, 1.5 * M_PI);
	cairo_close_path(c);
}
static void rgba(cairo_t *c, unsigned v, double a) { cairo_set_source_rgba(c, (v >> 16 & 255) / 255.0, (v >> 8 & 255) / 255.0, (v & 255) / 255.0, a); }
static void stop(cairo_pattern_t *p, double o, unsigned v, double a) { cairo_pattern_add_color_stop_rgba(p, o, (v >> 16 & 255) / 255.0, (v >> 8 & 255) / 255.0, (v & 255) / 255.0, a); }
static void vfill(cairo_t *c, double y0, double y1, unsigned a, unsigned b, unsigned d, unsigned e)
{
	cairo_pattern_t *p = cairo_pattern_create_linear(0, y0, 0, y1);
	stop(p, 0, a, 1); stop(p, .45, b, 1); stop(p, .55, d, 1); stop(p, 1, e, 1);
	cairo_set_source(c, p); cairo_fill_preserve(c); cairo_pattern_destroy(p);
}
static void vfill2(cairo_t *c, double y0, double y1, unsigned a, unsigned b)
{
	cairo_pattern_t *p = cairo_pattern_create_linear(0, y0, 0, y1);
	stop(p, 0, a, 1); stop(p, 1, b, 1);
	cairo_set_source(c, p); cairo_fill_preserve(c); cairo_pattern_destroy(p);
}
static void hfill(cairo_t *c, double x0, double x1, unsigned a, unsigned b, unsigned d)
{
	cairo_pattern_t *p = cairo_pattern_create_linear(x0, 0, x1, 0);
	stop(p, 0, a, 1); stop(p, .35, b, 1); stop(p, 1, d, 1);
	cairo_set_source(c, p); cairo_fill_preserve(c); cairo_pattern_destroy(p);
}
static void rfill(cairo_t *c, double x, double y, double r, unsigned in, unsigned mid, unsigned out)
{
	cairo_pattern_t *p = cairo_pattern_create_radial(x - r * .35, y - r * .4, r * .08, x, y, r);
	stop(p, 0, in, 1); stop(p, .5, mid, 1); stop(p, 1, out, 1);
	cairo_set_source(c, p); cairo_fill_preserve(c); cairo_pattern_destroy(p);
}
static void edge(cairo_t *c, unsigned v, double w) { rgba(c, v, 1); cairo_set_line_width(c, w); cairo_stroke_preserve(c); }
static void solid(cairo_t *c, unsigned v, double a) { rgba(c, v, a); cairo_fill_preserve(c); }
static void gloss(cairo_t *c, double a)
{
	double x1, y1, x2, y2; cairo_path_extents(c, &x1, &y1, &x2, &y2);
	cairo_save(c); cairo_clip(c);
	cairo_pattern_t *p = cairo_pattern_create_linear(0, y1, 0, y2);
	stop(p, 0, 0xFFFFFF, a); stop(p, .48, 0xFFFFFF, a * .25); stop(p, .5, 0xFFFFFF, 0); stop(p, 1, 0xFFFFFF, 0);
	cairo_set_source(c, p); cairo_rectangle(c, x1, y1, x2 - x1, y2 - y1); cairo_fill(c); cairo_pattern_destroy(p);
	cairo_restore(c);
}
static void rim(cairo_t *c, double x, double y, double w, double h, double r, double a)
{
	rr(c, x + .8, y + .8, w - 1.6, h - 1.6, r > 1 ? r - .8 : r);
	cairo_pattern_t *p = cairo_pattern_create_linear(x, y, x + w, y + h);
	stop(p, 0, 0xFFFFFF, a); stop(p, .5, 0xFFFFFF, 0); stop(p, 1, 0x000000, a * .3);
	cairo_set_source(c, p); cairo_set_line_width(c, 1); cairo_stroke(c); cairo_pattern_destroy(p);
}
static void ground(cairo_t *c, double cx, double cy, double rx, double ry, double a)
{
	for (int i = 0; i < 6; i++) {
		double k = 1 - i * .13;
		cairo_save(c); cairo_translate(c, cx, cy); cairo_scale(c, rx * (1 + i * .06), ry * (1 + i * .05)); cairo_arc(c, 0, 0, 1, 0, 2 * M_PI); cairo_restore(c);
		rgba(c, 0x000000, a * .13 * k); cairo_fill(c);
	}
}
static void txt(cairo_t *c, double x, double y, double sz, const char *s, unsigned col, int bold)
{
	cairo_select_font_face(c, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(c, sz);
	cairo_text_extents_t e; cairo_text_extents(c, s, &e);
	rgba(c, col, 1); cairo_move_to(c, x - e.x_advance / 2, y); cairo_show_text(c, s);
}
static void line(cairo_t *c, double x0, double y0, double x1, double y1, unsigned col, double w, double a)
{
	cairo_move_to(c, x0, y0); cairo_line_to(c, x1, y1); rgba(c, col, a); cairo_set_line_width(c, w); cairo_set_line_cap(c, CAIRO_LINE_CAP_ROUND); cairo_stroke(c); cairo_set_line_cap(c, CAIRO_LINE_CAP_BUTT);
}
static void orb(cairo_t *c, double cx, double cy, double r, unsigned top, unsigned bot, unsigned rimc)
{
	cairo_arc(c, cx, cy + 1.2, r, 0, 2 * M_PI); rgba(c, 0x000000, .28); cairo_fill(c);
	cairo_arc(c, cx, cy, r, 0, 2 * M_PI); vfill2(c, cy - r, cy + r, top, bot); edge(c, rimc, 1.2); cairo_new_path(c);
	cairo_arc(c, cx, cy, r - 1.4, 0, 2 * M_PI); cairo_set_source_rgba(c, 1, 1, 1, .0);
	cairo_new_path(c);
	cairo_save(c); cairo_translate(c, cx, cy - r * .42); cairo_scale(c, r * .72, r * .46); cairo_arc(c, 0, 0, 1, 0, 2 * M_PI); cairo_restore(c);
	cairo_pattern_t *p = cairo_pattern_create_linear(0, cy - r, 0, cy); stop(p, 0, 0xFFFFFF, .85); stop(p, 1, 0xFFFFFF, .08);
	cairo_set_source(c, p); cairo_fill(c); cairo_pattern_destroy(p);
}


/* ---- folders and files ---- */
static void folder_c(cairo_t *c, unsigned b0, unsigned b1, unsigned f0, unsigned f1, unsigned f2, unsigned f3, unsigned eb, unsigned ef)
{
	ground(c, 24, 43.8, 19, 2.6, 1);
	cairo_move_to(c, 4.5, 40); cairo_line_to(c, 4.5, 10.5); cairo_curve_to(c, 4.5, 8.5, 5.5, 7.5, 7.5, 7.5); cairo_line_to(c, 17.5, 7.5);
	cairo_curve_to(c, 19.5, 7.5, 20, 9, 21.5, 10.5); cairo_line_to(c, 40, 10.5); cairo_curve_to(c, 42, 10.5, 43.5, 11.5, 43.5, 13.5); cairo_line_to(c, 43.5, 40); cairo_close_path(c);
	vfill2(c, 7, 40, b0, b1); edge(c, eb, 1); cairo_new_path(c);
	cairo_save(c); cairo_translate(c, 24, 24); cairo_rotate(c, -.035); cairo_translate(c, -24, -24);
	rr(c, 8, 12.5, 31, 22, 1); vfill2(c, 12, 34, 0xFFFFFF, 0xE9E9EC); edge(c, 0x8A8A92, .8); cairo_new_path(c);
	for (int i = 0; i < 3; i++) line(c, 11, 17 + i * 3.2, 33 - i * 3, 17 + i * 3.2, 0xB5B8C4, .8, 1);
	cairo_restore(c);
	cairo_save(c); cairo_translate(c, 24, 24); cairo_rotate(c, .03); cairo_translate(c, -24, -24);
	rr(c, 10, 14.5, 31, 22, 1); vfill2(c, 14, 36, 0xFFFFFF, 0xE2E3E8); edge(c, 0x8A8A92, .8); cairo_new_path(c);
	cairo_restore(c);
	cairo_move_to(c, 3, 41); cairo_line_to(c, 3, 21.5); cairo_curve_to(c, 3, 19.5, 4.2, 18.5, 6, 18.5); cairo_line_to(c, 42, 18.5); cairo_curve_to(c, 44, 18.5, 45, 19.6, 45, 21.5);
	cairo_line_to(c, 45, 41); cairo_curve_to(c, 45, 42.6, 44, 43.5, 42.5, 43.5); cairo_line_to(c, 5.5, 43.5); cairo_curve_to(c, 4, 43.5, 3, 42.6, 3, 41); cairo_close_path(c);
	vfill(c, 18, 44, f0, f1, f2, f3); edge(c, ef, 1); cairo_new_path(c);
	line(c, 5.2, 19.9, 42.8, 19.9, 0xFFFFFF, 1.2, .55);
	line(c, 4.2, 42.4, 43.8, 42.4, 0x000000, 1, .3);
}
static void folder(cairo_t *c) { folder_c(c, 0xC99B4A, 0x9C7128, 0xFBE6AA, 0xF1CB7C, 0xE8BB62, 0xD3A043, 0x6E4D14, 0x7C5819); }
static void folder_home(cairo_t *c) { folder_c(c, 0x8A909B, 0x4E535D, 0x86B2EC, 0x5B8FD6, 0x3F72BC, 0x2A5AA0, 0x2A2D34, 0x143A78); }
static void house(cairo_t *c, double cx, double cy, double s)
{
	cairo_save(c); cairo_translate(c, cx, cy); cairo_scale(c, s / 10.0, s / 10.0);
	rr(c, -7, -.5, 14, 9.5, .6); vfill2(c, 0, 9, 0xFFFFFF, 0xCFD2DA); edge(c, 0x6A6E7A, .7); cairo_new_path(c);
	rr(c, -1.6, 3.4, 3.2, 5.6, .5); vfill2(c, 3, 9, 0x9A6A3A, 0x6A4220); edge(c, 0x3E2410, .5); cairo_new_path(c);
	cairo_move_to(c, -9, .6); cairo_line_to(c, 0, -8); cairo_line_to(c, 9, .6); cairo_close_path(c); vfill2(c, -8, .6, 0xF06858, 0xB02A20); edge(c, 0x6A1A14, .7); cairo_new_path(c);
	cairo_move_to(c, -7.5, -.2); cairo_line_to(c, 0, -7); cairo_line_to(c, 0, -3.5); cairo_line_to(c, -5, -.2); cairo_close_path(c); rgba(c, 0xFFFFFF, .28); cairo_fill(c);
	cairo_restore(c);
}
static void sheet(cairo_t *c, double x, double y, double w, double h, unsigned cut)
{
	cairo_move_to(c, x, y); cairo_line_to(c, x + w - cut, y); cairo_line_to(c, x + w, y + cut); cairo_line_to(c, x + w, y + h); cairo_line_to(c, x, y + h); cairo_close_path(c);
}
static void file(cairo_t *c, unsigned band)
{
	ground(c, 24, 44.4, 14, 2.2, .9);
	sheet(c, 10, 3.5, 29, 40, 9); vfill2(c, 3, 44, 0xFFFFFF, 0xE3E4EA); edge(c, 0x6C6E7A, 1); cairo_new_path(c);
	rim(c, 10, 3.5, 29, 40, 1, .9);
	cairo_move_to(c, 30, 3.5); cairo_line_to(c, 30, 12.5); cairo_line_to(c, 39, 12.5); cairo_close_path(c); vfill2(c, 3, 13, 0xFAFAFC, 0xB5B8C6); edge(c, 0x6C6E7A, .8); cairo_new_path(c);
	for (int i = 0; i < 5; i++) line(c, 14, 19 + i * 4.4, 35 - (i == 4) * 10, 19 + i * 4.4, 0xA9AEC0, 1.1, 1);
	if (band) { rr(c, 10.5, 33.5, 28, 9.5, 1); rgba(c, band, 1); cairo_fill_preserve(c); gloss(c, .45); }
}
/* ---- hardware ---- */
static void disk(cairo_t *c)
{
	ground(c, 25, 41.2, 20, 3, 1);
	cairo_move_to(c, 4, 22); cairo_line_to(c, 10.5, 13.5); cairo_line_to(c, 44, 13.5); cairo_line_to(c, 39.5, 22); cairo_close_path(c);
	vfill2(c, 13, 22, 0xF4F6F9, 0xCBD0D8); edge(c, 0x5A606C, .9); cairo_new_path(c);
	cairo_move_to(c, 39.5, 22); cairo_line_to(c, 44, 13.5); cairo_line_to(c, 44, 30.5); cairo_line_to(c, 39.5, 39); cairo_close_path(c);
	vfill2(c, 14, 39, 0x8E94A0, 0x555B66); edge(c, 0x3A3F48, .9); cairo_new_path(c);
	rr(c, 4, 22, 35.5, 17, 1.6); vfill(c, 22, 39, 0x8C929D, 0x6E747F, 0x575C67, 0x3C4049); edge(c, 0x23262C, 1); cairo_new_path(c);
	rim(c, 4, 22, 35.5, 17, 1.6, .9);
	rr(c, 8, 28, 24, 3.2, 1.2); vfill2(c, 28, 31.2, 0x16181C, 0x2E323A); edge(c, 0x101114, .6); cairo_new_path(c);
	line(c, 8.5, 31.8, 31.5, 31.8, 0xFFFFFF, .8, .55);
	cairo_arc(c, 34.3, 29.8, 1.9, 0, 2 * M_PI); rfill(c, 34.3, 29.8, 1.9, 0xD8FFD0, 0x3FD24A, 0x137A1E); edge(c, 0x0C4A12, .5); cairo_new_path(c);
	cairo_arc(c, 34.3, 29.8, 4, 0, 2 * M_PI); rgba(c, 0x40FF60, .16); cairo_fill(c);
	for (int i = 0; i < 2; i++) { cairo_arc(c, 8 + i * 27, 36, .9, 0, 2 * M_PI); rgba(c, 0x4A4F58, 1); cairo_fill(c); }
}
static void trash(cairo_t *c)
{
	ground(c, 24, 44, 14, 2.6, 1);
	cairo_move_to(c, 10, 16); cairo_line_to(c, 38, 16); cairo_line_to(c, 35.5, 42); cairo_curve_to(c, 35.3, 43.4, 34.4, 44, 33, 44); cairo_line_to(c, 15, 44); cairo_curve_to(c, 13.6, 44, 12.7, 43.4, 12.5, 42); cairo_close_path(c);
	hfill(c, 10, 38, 0xCFD98A, 0xA8B84A, 0x66741F); edge(c, 0x46501A, 1); cairo_new_path(c);
	for (int i = 0; i < 5; i++) { double x = 15 + i * 4.5; line(c, x, 19, x - .35 + (i - 2) * .22, 41, 0x2F4B15, 1.1, .38); line(c, x + 1, 19, x + .65 + (i - 2) * .22, 41, 0xFFFFFF, .8, .25); }
	rr(c, 8, 10.5, 32, 6.5, 2.2); hfill(c, 8, 40, 0xF6F6F6, 0xCFD2D6, 0x7D828A); edge(c, 0x4A4E56, 1); cairo_new_path(c);
	line(c, 9.5, 11.6, 38.5, 11.6, 0xFFFFFF, 1, .9);
	rr(c, 18, 6.5, 12, 5, 2); hfill(c, 18, 30, 0xEAECEE, 0xC2C6CC, 0x7D828A); edge(c, 0x4A4E56, .9); cairo_new_path(c);
	rr(c, 20.5, 8, 7, 2, 1); rgba(c, 0x30343A, .85); cairo_fill(c);
	cairo_save(c); cairo_translate(c, 24, 31); /* recycling arrows */
	for (int i = 0; i < 3; i++) { cairo_save(c); cairo_rotate(c, i * 2.0944); cairo_move_to(c, -2.6, -6.6); cairo_line_to(c, 2.8, -6.6); cairo_line_to(c, 5, -3); cairo_line_to(c, 2.4, -3.4); cairo_line_to(c, .6, -.4); cairo_line_to(c, -1.4, -3.4); cairo_close_path(c); rgba(c, 0x2A4812, .55); cairo_fill(c); cairo_restore(c); }
	cairo_restore(c);
}
static void monitor(cairo_t *c, int kind)
{
	ground(c, 24, 44.5, 17, 2.4, 1);
	rr(c, 10, 38.5, 28, 4.5, 1.8); hfill(c, 10, 38, 0xD8DADF, 0x9DA1AA, 0x5C606A); edge(c, 0x383B42, .9); cairo_new_path(c);
	rr(c, 18, 35, 12, 4.6, 1); hfill(c, 18, 30, 0xBFC3CB, 0x8A8F99, 0x565A63); edge(c, 0x383B42, .8); cairo_new_path(c);
	rr(c, 3.5, 4, 41, 32, 3.2); vfill(c, 4, 36, 0xEFF1F4, 0xC4C8D0, 0xB0B5BF, 0x8A909B); edge(c, 0x353840, 1); cairo_new_path(c);
	rim(c, 3.5, 4, 41, 32, 3.2, .95);
	rr(c, 7, 7.5, 34, 25, 1.4); rgba(c, 0x14161A, 1); cairo_fill_preserve(c); edge(c, 0x6A6F7A, .8); cairo_new_path(c);
	cairo_save(c); rr(c, 8, 8.5, 32, 23, 1); cairo_clip(c);
	if (kind == 0) {
		cairo_rectangle(c, 8, 8.5, 32, 23); vfill2(c, 8, 32, 0x3C5A4C, 0x1C2E26); cairo_new_path(c);
		for (int i = 0; i < 12; i++) line(c, 8, 10 + i * 2, 40, 10 + i * 2, 0x000000, .6, .22);
		cairo_move_to(c, 11.5, 14); cairo_line_to(c, 17, 18.5); cairo_line_to(c, 11.5, 23); rgba(c, 0xFFFFFF, 1); cairo_set_line_width(c, 2); cairo_stroke(c);
		cairo_rectangle(c, 20, 21.6, 8, 2.2); rgba(c, 0xFFFFFF, 1); cairo_fill(c);
	} else if (kind == 1) {
		cairo_rectangle(c, 8, 8.5, 32, 23); vfill2(c, 8, 32, 0x4C9CF0, 0xC8E6FF); cairo_new_path(c);
		cairo_move_to(c, 8, 31.5); cairo_curve_to(c, 15, 20, 26, 25, 40, 18); cairo_line_to(c, 40, 31.5); cairo_close_path(c); vfill2(c, 18, 32, 0x68B84A, 0x2C7A30); cairo_new_path(c);
		cairo_move_to(c, 8, 31.5); cairo_curve_to(c, 18, 27, 28, 31, 40, 25); cairo_line_to(c, 40, 31.5); cairo_close_path(c); vfill2(c, 25, 32, 0x4C9A3A, 0x1E5A22); cairo_new_path(c);
		cairo_arc(c, 32, 15, 3.4, 0, 2 * M_PI); rfill(c, 32, 15, 3.4, 0xFFFFF0, 0xFFE24A, 0xE8A010); cairo_new_path(c);
	} else if (kind == 2) {
		cairo_rectangle(c, 8, 8.5, 32, 23); vfill2(c, 8, 32, 0x143020, 0x08140C); cairo_new_path(c);
		for (int i = 1; i < 4; i++) line(c, 8, 8.5 + i * 5.75, 40, 8.5 + i * 5.75, 0x2C7A48, .6, .8);
		for (int i = 1; i < 6; i++) line(c, 8 + i * 5.33, 8.5, 8 + i * 5.33, 31.5, 0x2C7A48, .6, .5);
		cairo_move_to(c, 9, 28); cairo_line_to(c, 15, 22); cairo_line_to(c, 20, 26); cairo_line_to(c, 27, 14); cairo_line_to(c, 33, 20); cairo_line_to(c, 39, 12);
		rgba(c, 0x6CFF9C, 1); cairo_set_line_width(c, 1.7); cairo_set_line_join(c, CAIRO_LINE_JOIN_ROUND); cairo_stroke(c);
	} else {
		cairo_rectangle(c, 8, 8.5, 32, 23); vfill2(c, 8, 32, 0x7CBBFF, 0x2C68C8); cairo_new_path(c);
	}
	cairo_new_path(c);
	cairo_move_to(c, 8, 8.5); cairo_line_to(c, 28, 8.5); cairo_line_to(c, 14, 31.5); cairo_line_to(c, 8, 31.5); cairo_close_path(c); rgba(c, 0xFFFFFF, .14); cairo_fill(c);
	cairo_restore(c);
	cairo_arc(c, 40.3, 34, .75, 0, 2 * M_PI); rgba(c, 0x4DE060, 1); cairo_fill(c);
}
static void keyboard(cairo_t *c)
{
	ground(c, 24, 41.5, 21, 2.6, 1);
	cairo_move_to(c, 3, 36); cairo_line_to(c, 7, 17); cairo_line_to(c, 45, 17); cairo_line_to(c, 45, 36); cairo_close_path(c);
	rgba(c, 0x000000, 0); cairo_fill(c);
	rr(c, 2.5, 15, 43, 24, 3); vfill(c, 15, 39, 0xF2F3F6, 0xD2D5DC, 0xBFC3CC, 0x8E939E); edge(c, 0x3A3E46, 1); cairo_new_path(c);
	rim(c, 2.5, 15, 43, 24, 3, .95);
	rr(c, 5, 17.5, 38, 18.5, 1.6); rgba(c, 0x41454E, 1); cairo_fill(c); cairo_new_path(c);
	for (int r = 0; r < 4; r++) {
		int n = r == 3 ? 0 : 10 - r % 2; double off = 5.8 + (r % 2) * 1.6, kw = 3.1;
		for (int k = 0; k < n; k++) { rr(c, off + k * 3.7, 18.8 + r * 4, kw, 3.2, .7); vfill2(c, 18.8 + r * 4, 22 + r * 4, 0xFAFBFC, 0xC9CDD6); cairo_new_path(c); }
	}
	rr(c, 12, 18.8 + 12, 24, 3.2, .8); vfill2(c, 30.8, 34, 0xFAFBFC, 0xC9CDD6); cairo_new_path(c);
}
static void mouse(cairo_t *c)
{
	ground(c, 24, 44.5, 12, 2.2, 1);
	cairo_move_to(c, 24, 4.5); cairo_curve_to(c, 36, 4.5, 40, 13, 40, 25); cairo_curve_to(c, 40, 37, 33, 44.5, 24, 44.5); cairo_curve_to(c, 15, 44.5, 8, 37, 8, 25); cairo_curve_to(c, 8, 13, 12, 4.5, 24, 4.5); cairo_close_path(c);
	cairo_pattern_t *p = cairo_pattern_create_radial(18, 13, 2, 24, 26, 24); stop(p, 0, 0xFFFFFF, 1); stop(p, .5, 0xDADDE4, 1); stop(p, 1, 0x80858F, 1);
	cairo_set_source(c, p); cairo_fill_preserve(c); cairo_pattern_destroy(p); edge(c, 0x3A3E46, 1.1); cairo_new_path(c);
	cairo_move_to(c, 8.5, 21); cairo_curve_to(c, 14, 22.6, 34, 22.6, 39.5, 21); rgba(c, 0x4A4F5A, .8); cairo_set_line_width(c, 1.1); cairo_stroke(c);
	line(c, 24, 5.5, 24, 21.8, 0x4A4F5A, 1.1, .8);
	rr(c, 21.8, 11, 4.4, 8, 2.2); vfill2(c, 11, 19, 0x6A6F7A, 0x2A2D34); edge(c, 0x1A1C20, .6); cairo_new_path(c);
	cairo_move_to(c, 12, 12); cairo_curve_to(c, 15, 8, 20, 7, 23, 7); rgba(c, 0xFFFFFF, .65); cairo_set_line_width(c, 1.4); cairo_stroke(c);
}
/* ---- apps ---- */
static void apps_icon(cairo_t *c)
{
	ground(c, 25, 44.6, 19, 2.6, 1);
	rr(c, 6, 4, 24, 38, 1.6); vfill2(c, 4, 42, 0xFFFFFF, 0xDDDFE6); edge(c, 0x5C606E, 1); cairo_new_path(c);
	rim(c, 6, 4, 24, 38, 1.6, .9);
	rr(c, 6, 4, 6, 38, 1.2); vfill2(c, 4, 42, 0xD8DAE2, 0xB8BBC6); edge(c, 0x5C606E, .8); cairo_new_path(c);
	rr(c, 14, 10, 11, 12, 1); vfill2(c, 10, 22, 0xE8584A, 0xA81E14); edge(c, 0x601008, .8); gloss(c, .5);
	for (int i = 0; i < 3; i++) line(c, 14, 27 + i * 3.4, 26, 27 + i * 3.4, 0xA9AEC0, 1, 1);
	cairo_arc(c, 32, 30, 12.5, 0, 2 * M_PI); rgba(c, 0x000000, .25); cairo_fill(c);
	cairo_arc(c, 31.5, 29.4, 12.2, 0, 2 * M_PI); { cairo_pattern_t *p = cairo_pattern_create_radial(27, 24, 2, 31.5, 29.4, 13); stop(p, 0, 0xFFFFFF, 1); stop(p, .5, 0xD6DAE4, 1); stop(p, 1, 0x7E8696, 1); cairo_set_source(c, p); cairo_fill_preserve(c); cairo_pattern_destroy(p); }
	edge(c, 0x4A5268, .9); cairo_new_path(c);
	cairo_arc(c, 31.5, 29.4, 6.2, 0, 2 * M_PI); rfill(c, 31.5, 29.4, 6.2, 0xFF9A8A, 0xD8281C, 0x8A0E08); edge(c, 0x5A0A06, .6); cairo_new_path(c);
	cairo_arc(c, 31.5, 29.4, 2.2, 0, 2 * M_PI); rgba(c, 0xFFFFFF, 1); cairo_fill_preserve(c); edge(c, 0x7A8296, .6); cairo_new_path(c);
	for (int i = 0; i < 4; i++) { double a0 = i * M_PI / 2 + .3; cairo_move_to(c, 31.5, 29.4); cairo_arc(c, 31.5, 29.4, 11.4, a0, a0 + .35); cairo_close_path(c); rgba(c, 0xFFFFFF, .3); cairo_fill(c); }
}
static void globe(cairo_t *c)
{
	ground(c, 24, 44, 14, 2.4, 1);
	cairo_arc(c, 24, 23, 19.5, 0, 2 * M_PI); rfill(c, 24, 23, 19.5, 0xA8DCFF, 0x2F78D0, 0x0E2F78); edge(c, 0x0A2255, 1.1); cairo_new_path(c);
	cairo_save(c); cairo_arc(c, 24, 23, 19, 0, 2 * M_PI); cairo_clip(c);
	cairo_move_to(c, 9, 12); cairo_curve_to(c, 15, 7, 25, 9, 26, 15); cairo_curve_to(c, 25, 20, 20, 21, 17, 26); cairo_curve_to(c, 14, 28, 11, 24, 8, 21); cairo_close_path(c); vfill2(c, 7, 28, 0x7CCB5A, 0x2F7A2C); edge(c, 0x1A4A18, .6); cairo_new_path(c);
	cairo_move_to(c, 28, 24); cairo_curve_to(c, 36, 22, 42, 29, 37, 37); cairo_curve_to(c, 32, 40, 27, 35, 26.5, 30); cairo_close_path(c); vfill2(c, 22, 40, 0x7CCB5A, 0x2F7A2C); edge(c, 0x1A4A18, .6); cairo_new_path(c);
	for (int i = 1; i < 4; i++) { cairo_save(c); cairo_translate(c, 24, 23); cairo_scale(c, 1, .22 + .0); cairo_restore(c); }
	cairo_arc(c, 24, 23, 19, 0, 2 * M_PI); { cairo_pattern_t *p = cairo_pattern_create_radial(31, 32, 8, 24, 23, 22); stop(p, 0, 0x000000, 0); stop(p, 1, 0x000840, .55); cairo_set_source(c, p); cairo_fill(c); cairo_pattern_destroy(p); }
	cairo_restore(c);
	cairo_save(c); cairo_translate(c, 17.5, 12.5); cairo_scale(c, 9, 5.8); cairo_rotate(c, -.5); cairo_arc(c, 0, 0, 1, 0, 2 * M_PI); cairo_restore(c);
	{ cairo_pattern_t *p = cairo_pattern_create_linear(0, 7, 0, 18); stop(p, 0, 0xFFFFFF, .75); stop(p, 1, 0xFFFFFF, .05); cairo_set_source(c, p); cairo_fill(c); cairo_pattern_destroy(p); }
}
static void palette(cairo_t *c)
{
	ground(c, 24, 43.8, 17, 2.4, 1);
	cairo_move_to(c, 24, 6); cairo_curve_to(c, 40, 4, 47, 19, 41, 31); cairo_curve_to(c, 37.5, 39, 29, 41.5, 24, 41); cairo_curve_to(c, 19, 40.5, 20.5, 35, 24, 33.5);
	cairo_curve_to(c, 27, 32, 26, 28, 22, 27.5); cairo_curve_to(c, 17, 27, 14, 31, 10.5, 29); cairo_curve_to(c, 3, 24, 6, 8, 24, 6); cairo_close_path(c);
	vfill(c, 5, 42, 0xF1D6A4, 0xDDB878, 0xCDA362, 0xA97D3E); edge(c, 0x6E4A18, 1); cairo_new_path(c);
	cairo_move_to(c, 8, 14); cairo_curve_to(c, 12, 9, 18, 7.5, 24, 7.6); rgba(c, 0xFFFFFF, .75); cairo_set_line_width(c, 1.3); cairo_stroke(c);
	struct { double x, y; unsigned c1, c2; } d[5] = {{11.5, 20, 0xFF6A5A, 0xB01C10}, {16.5, 12.8, 0xFFE25A, 0xC09A08}, {26, 11.6, 0x6AE05A, 0x2A8A1C}, {34.5, 17, 0x5AA8FF, 0x1A5AC0}, {35.5, 27, 0xC87AFF, 0x7A28B8}};
	for (int i = 0; i < 5; i++) { cairo_arc(c, d[i].x, d[i].y, 3.5, 0, 2 * M_PI); rfill(c, d[i].x, d[i].y, 3.5, 0xFFFFFF, d[i].c1, d[i].c2); edge(c, 0x00000, .5); cairo_new_path(c); cairo_arc(c, d[i].x - .8, d[i].y - 1.1, 1, 0, 2 * M_PI); rgba(c, 0xFFFFFF, .8); cairo_fill(c); }
	cairo_save(c); cairo_translate(c, 31, 33); cairo_rotate(c, .9);
	rr(c, -1.8, -4, 3.6, 17, 1.2); hfill(c, -1.8, 1.8, 0xE0B070, 0xB58040, 0x6E4818); edge(c, 0x3E2808, .6); cairo_new_path(c);
	rr(c, -1.9, -9.5, 3.8, 5.5, 1); hfill(c, -1.9, 1.9, 0xE8E8EC, 0xA8ACB6, 0x606670); edge(c, 0x383B42, .6); cairo_new_path(c);
	cairo_move_to(c, -1.8, -9.5); cairo_curve_to(c, -2.4, -13, 2.4, -13, 1.8, -9.5); cairo_close_path(c); hfill(c, -2, 2, 0x5A8AE0, 0x2A52B0, 0x10286A); edge(c, 0x10286A, .5); cairo_new_path(c);
	cairo_restore(c);
}
static void calendar(cairo_t *c)
{
	ground(c, 24, 44.5, 16, 2.4, 1);
	rr(c, 6.5, 7.5, 35, 35.5, 2.6); vfill2(c, 7, 43, 0xFFFFFF, 0xE2E4EC); edge(c, 0x545868, 1); cairo_new_path(c);
	rr(c, 6.5, 7.5, 35, 11, 2.6); vfill2(c, 7, 19, 0xF2685C, 0xB22418); edge(c, 0x6E1410, 1); gloss(c, .55);
	line(c, 8, 18.8, 40, 18.8, 0x7A1810, 1, .7);
	for (int i = 0; i < 3; i++) { double x = 13 + i * 11; rr(c, x - 1.6, 3.5, 3.2, 8, 1.4); hfill(c, x - 1.6, x + 1.6, 0xF2F3F6, 0xB4B9C4, 0x6C7280); edge(c, 0x3A3E46, .7); cairo_new_path(c); }
	txt(c, 19.5, 36.5, 14, "21", 0xFFFFFF, 1);
	txt(c, 19.2, 36, 14, "21", 0x25283A, 1);
	cairo_arc(c, 36, 36.5, 8.6, 0, 2 * M_PI); rgba(c, 0x000000, .3); cairo_fill(c);
	cairo_arc(c, 36, 35.8, 8.2, 0, 2 * M_PI); hfill(c, 27, 45, 0xF0F2F6, 0xB6BBC6, 0x6A707C); edge(c, 0x383B42, .9); cairo_new_path(c);
	cairo_arc(c, 36, 35.8, 6.4, 0, 2 * M_PI); rfill(c, 36, 35.8, 6.4, 0xFFFFFF, 0xF3F4F8, 0xCBD0DA); edge(c, 0x6A707C, .6); cairo_new_path(c);
	for (int i = 0; i < 12; i++) { double a = i * M_PI / 6; line(c, 36 + sin(a) * 4.9, 35.8 - cos(a) * 4.9, 36 + sin(a) * 5.7, 35.8 - cos(a) * 5.7, 0x30343C, .6, 1); }
	line(c, 36, 35.8, 36, 31.8, 0x20232A, 1, 1); line(c, 36, 35.8, 39, 37.4, 0x20232A, 1, 1);
}
static void info(cairo_t *c)
{
	ground(c, 24, 44.5, 15, 2.2, .8);
	orb(c, 24, 23, 19.5, 0x8FCBFF, 0x1658C8, 0x0A3A8A);
	txt(c, 24, 33, 28, "i", 0xFFFFFF, 1);
	cairo_move_to(c, 24, 14.5); cairo_arc(c, 24, 13.8, 2.4, 0, 2 * M_PI); rgba(c, 0xFFFFFF, 1); cairo_fill(c);
}

static void arrow_dir(cairo_t *c, double ang, unsigned a, unsigned b, unsigned e, int glossy)
{
	ground(c, 24, 44.5, 15, 2.2, .7);
	cairo_save(c); cairo_translate(c, 24, 24); cairo_rotate(c, ang); cairo_translate(c, -24, -24);
	cairo_move_to(c, 24, 3.5); cairo_line_to(c, 43, 23.5); cairo_line_to(c, 31.5, 23.5); cairo_line_to(c, 31.5, 42); cairo_curve_to(c, 31.5, 43, 31, 43.5, 30, 43.5); cairo_line_to(c, 18, 43.5);
	cairo_curve_to(c, 17, 43.5, 16.5, 43, 16.5, 42); cairo_line_to(c, 16.5, 23.5); cairo_line_to(c, 5, 23.5); cairo_close_path(c);
	vfill2(c, 3, 44, a, b); cairo_set_line_join(c, CAIRO_LINE_JOIN_ROUND); edge(c, e, 1.2);
	if (glossy) { cairo_new_path(c); cairo_move_to(c, 24, 6.5); cairo_line_to(c, 38, 21.5); cairo_line_to(c, 30, 21.5); cairo_line_to(c, 30, 26); cairo_line_to(c, 18, 26); cairo_line_to(c, 18, 21.5); cairo_line_to(c, 10, 21.5); cairo_close_path(c); gloss(c, .55); }
	cairo_restore(c);
}
static void refresh(cairo_t *c)
{
	ground(c, 24, 44.5, 15, 2.2, .7);
	for (int i = 0; i < 2; i++) {
		cairo_save(c); cairo_translate(c, 24, 24); cairo_rotate(c, i * M_PI); cairo_translate(c, -24, -24);
		cairo_arc(c, 24, 24, 13, -2.55, .15); rgba(c, 0x1E4E94, 1); cairo_set_line_width(c, 8); cairo_stroke(c);
		cairo_arc(c, 24, 24, 13, -2.55, .15); { cairo_pattern_t *p = cairo_pattern_create_linear(0, 8, 0, 40); stop(p, 0, 0xCFE6FA, 1); stop(p, 1, 0x5C98DA, 1); cairo_set_source(c, p); cairo_set_line_width(c, 5.6); cairo_stroke(c); cairo_pattern_destroy(p); }
		cairo_move_to(c, 29.5, 19.5); cairo_line_to(c, 43, 19.5); cairo_line_to(c, 36, 32); cairo_close_path(c); vfill2(c, 19, 32, 0xCFE6FA, 0x4C88CC); cairo_set_line_join(c, CAIRO_LINE_JOIN_ROUND); edge(c, 0x1E4E94, 1.2); cairo_new_path(c);
		cairo_arc(c, 24, 24, 13, -2.4, -1.2); rgba(c, 0xFFFFFF, .75); cairo_set_line_width(c, 1.6); cairo_stroke(c);
		cairo_restore(c);
	}
}
static void search(cairo_t *c)
{
	ground(c, 26, 44.5, 14, 2, .6);
	cairo_save(c); cairo_translate(c, 29, 31); cairo_rotate(c, -M_PI / 4);
	rr(c, -2.6, -3, 5.2, 18, 2.4); hfill(c, -2.6, 2.6, 0x9BA1AC, 0x5A606C, 0x262A32); edge(c, 0x14161A, .8); cairo_new_path(c);
	cairo_restore(c);
	cairo_arc(c, 19, 18, 14, 0, 2 * M_PI); hfill(c, 5, 33, 0xF4F6FA, 0xB0B6C2, 0x5A606C); edge(c, 0x2C3038, 1); cairo_new_path(c);
	cairo_arc(c, 19, 18, 10.8, 0, 2 * M_PI); { cairo_pattern_t *p = cairo_pattern_create_radial(15, 13, 1, 19, 18, 12); stop(p, 0, 0xF8FDFF, .95); stop(p, .6, 0xBEDDF6, .85); stop(p, 1, 0x7FB2E0, .85); cairo_set_source(c, p); cairo_fill_preserve(c); cairo_pattern_destroy(p); }
	edge(c, 0x4A6A8C, .8); cairo_new_path(c);
	cairo_arc(c, 15.5, 13.5, 4.5, 3.4, 5.2); rgba(c, 0xFFFFFF, .9); cairo_set_line_width(c, 1.6); cairo_stroke(c);
}
static void calc(cairo_t *c)
{
	ground(c, 24, 45, 15, 2.2, 1);
	rr(c, 8, 3, 32, 42, 3.2); vfill(c, 3, 45, 0xE2E4EA, 0xB9BDC8, 0xA3A8B4, 0x7C818E); edge(c, 0x30343C, 1); cairo_new_path(c);
	rim(c, 8, 3, 32, 42, 3.2, .95);
	rr(c, 11.5, 6.5, 25, 10, 1.4); vfill2(c, 6, 17, 0x7D9468, 0xB4CE98); edge(c, 0x2A3A20, .9); cairo_new_path(c);
	txt(c, 33.5, 14.8, 8.5, "0.", 0x1A2412, 1);
	for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) {
		double x = 11.5 + k * 6.4, y = 20 + r * 6.1; unsigned a = k == 3 ? 0xFFB866 : 0xFFFFFF, b = k == 3 ? 0xD87818 : 0xC2C6D0;
		rr(c, x, y, 5.2, 4.6, 1.1); vfill2(c, y, y + 4.6, a, b); edge(c, 0x3A3E48, .6); cairo_new_path(c);
	}
}
static void note(cairo_t *c)
{
	ground(c, 22, 45, 15, 2.2, 1);
	rr(c, 7, 6.5, 29, 37, 1.6); vfill2(c, 6, 44, 0xFFF6B0, 0xF0DE70); edge(c, 0x8A7420, 1); cairo_new_path(c);
	rim(c, 7, 6.5, 29, 37, 1.6, .9);
	for (int i = 0; i < 6; i++) line(c, 10, 16 + i * 4.6, 33, 16 + i * 4.6, 0xA9B8D8, .9, .9);
	line(c, 13, 8, 13, 43, 0xE08080, .8, .8);
	for (int i = 0; i < 5; i++) { double x = 11.5 + i * 5.2; rr(c, x - 1.2, 3, 2.4, 8, 1.2); hfill(c, x - 1.2, x + 1.2, 0xF2F3F6, 0xAEB3BE, 0x5C626E); edge(c, 0x30343C, .6); cairo_new_path(c); }
	cairo_save(c); cairo_translate(c, 36, 31); cairo_rotate(c, .75);
	rr(c, -2.2, -14, 4.4, 24, 1); hfill(c, -2.2, 2.2, 0xFF9A8A, 0xD8281C, 0x7A0E08); edge(c, 0x4A0A06, .6); cairo_new_path(c);
	rr(c, -2.3, -17, 4.6, 3.6, 1); hfill(c, -2.3, 2.3, 0xF0F0F2, 0xB0B4BE, 0x646A76); edge(c, 0x30343C, .5); cairo_new_path(c);
	cairo_move_to(c, -2.2, 10); cairo_line_to(c, 2.2, 10); cairo_line_to(c, 0, 15.5); cairo_close_path(c); vfill2(c, 10, 15.5, 0xF0D2A0, 0xC49C60); edge(c, 0x5A4018, .5); cairo_new_path(c);
	cairo_move_to(c, -.7, 13.6); cairo_line_to(c, .7, 13.6); cairo_line_to(c, 0, 15.5); cairo_close_path(c); rgba(c, 0x303030, 1); cairo_fill(c);
	cairo_restore(c);
}
static void view(cairo_t *c)
{
	ground(c, 24, 44.6, 18, 2.4, 1);
	cairo_save(c); cairo_translate(c, 24, 24); cairo_rotate(c, -.12); cairo_translate(c, -24, -24);
	rr(c, 6, 8, 33, 28, 1); vfill2(c, 8, 36, 0xFFFFFF, 0xE2E4EA); edge(c, 0x6A6E7C, .9); cairo_new_path(c);
	cairo_restore(c);
	cairo_save(c); cairo_translate(c, 24, 24); cairo_rotate(c, .05); cairo_translate(c, -24, -24);
	rr(c, 8, 9, 34, 30, 1); vfill2(c, 9, 39, 0xFFFFFF, 0xE8EAEF); edge(c, 0x6A6E7C, .9); cairo_new_path(c);
	rim(c, 8, 9, 34, 30, 1, .8);
	cairo_rectangle(c, 11.5, 12.5, 27, 20); vfill2(c, 12, 33, 0x4C98EE, 0xCDE8FF); edge(c, 0x2A4E78, .7); cairo_new_path(c);
	cairo_save(c); cairo_rectangle(c, 11.5, 12.5, 27, 20); cairo_clip(c);
	cairo_move_to(c, 11, 33); cairo_line_to(c, 19, 21); cairo_line_to(c, 25, 28); cairo_line_to(c, 30, 22.5); cairo_line_to(c, 39, 33); cairo_close_path(c); vfill2(c, 21, 33, 0x68B84A, 0x2A7430); cairo_new_path(c);
	cairo_restore(c);
	cairo_arc(c, 33, 17.5, 2.6, 0, 2 * M_PI); rfill(c, 33, 17.5, 2.6, 0xFFFFF0, 0xFFE24A, 0xE0A010); cairo_new_path(c);
	cairo_restore(c);
}
static void charmap(cairo_t *c)
{
	ground(c, 24, 44.6, 17, 2.4, 1);
	static const char *g[9] = {"A", "\xC3\xA4", "\xCE\xA9", "\xC3\xB1", "&", "\xC2\xA7", "7", "\xC3\x9F", "@"};
	static const unsigned col[3] = {0x1C4A98, 0xB82A22, 0x2A7A30};
	for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) {
		double x = 5 + k * 13.4, y = 5 + r * 13.4;
		rr(c, x, y, 12, 12, 1.8); vfill2(c, y, y + 12, 0xFFFFFF, 0xD6D9E2); edge(c, 0x5A5E6C, .8); cairo_new_path(c);
		rim(c, x, y, 12, 12, 1.8, .9);
		txt(c, x + 6, y + 9.2, 9.5, g[r * 3 + k], col[(r + k) % 3], 1);
	}
}
static void runwin(cairo_t *c)
{
	ground(c, 24, 44.5, 18, 2.4, 1);
	rr(c, 4.5, 8, 39, 33, 2.6); vfill2(c, 8, 41, 0xFAFAFC, 0xCDD0DA); edge(c, 0x474B58, 1); cairo_new_path(c);
	rr(c, 4.5, 8, 39, 8.5, 2.6); vfill2(c, 8, 16.5, 0x8CB8E0, 0x4A7CB4); edge(c, 0x243E66, 1); gloss(c, .5);
	for (int i = 0; i < 3; i++) { cairo_rectangle(c, 33 + i * 3.4, 10.5, 2.6, 2.6); rgba(c, 0xFFFFFF, .85); cairo_fill(c); }
	cairo_move_to(c, 15, 21.5); cairo_line_to(c, 15, 36.5); cairo_line_to(c, 31.5, 29); cairo_close_path(c); vfill2(c, 21, 37, 0x9AEE70, 0x2C9A24); cairo_set_line_join(c, CAIRO_LINE_JOIN_ROUND); edge(c, 0x16601A, 1.2);
	cairo_new_path(c); cairo_move_to(c, 16.5, 23.5); cairo_line_to(c, 16.5, 29); cairo_line_to(c, 26, 24.8); cairo_close_path(c); rgba(c, 0xFFFFFF, .35); cairo_fill(c);
}
static void fonticon(cairo_t *c)
{
	ground(c, 24, 44.5, 16, 2.2, .8);
	cairo_select_font_face(c, "DejaVu Serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(c, 38); cairo_move_to(c, 6, 36);
	cairo_text_path(c, "A"); { cairo_pattern_t *p = cairo_pattern_create_linear(0, 8, 0, 38); stop(p, 0, 0x7FB0F4, 1); stop(p, 1, 0x1A52B8, 1); cairo_set_source(c, p); cairo_fill_preserve(c); cairo_pattern_destroy(p); }
	edge(c, 0x0E2E7A, 1.2); cairo_new_path(c);
	cairo_set_font_size(c, 25); cairo_move_to(c, 27, 40);
	cairo_text_path(c, "a"); { cairo_pattern_t *p = cairo_pattern_create_linear(0, 20, 0, 40); stop(p, 0, 0x8EDC6E, 1); stop(p, 1, 0x2A8A26, 1); cairo_set_source(c, p); cairo_fill_preserve(c); cairo_pattern_destroy(p); }
	edge(c, 0x16521A, 1); cairo_new_path(c);
}
static void prefs_tools(cairo_t *c)
{
	ground(c, 24, 44.2, 17, 2.6, 1);
	cairo_save(c); cairo_translate(c, 24, 24); cairo_rotate(c, -.78); /* screwdriver, blue handle */
	rr(c, -1.3, -2, 2.6, 25, 1); hfill(c, -1.3, 1.3, 0xF4F6FA, 0xAEB4C0, 0x5A606C); edge(c, 0x2C3038, .7); cairo_new_path(c);
	rr(c, -4.2, -21, 8.4, 20, 4); hfill(c, -4.2, 4.2, 0x8CB6F4, 0x2F66C8, 0x123A86); edge(c, 0x0A2258, 1); cairo_new_path(c);
	for (int i = 0; i < 4; i++) line(c, -3.4, -17 + i * 3.6, 3.4, -17 + i * 3.6, 0x0A2258, .8, .35);
	line(c, -2.6, -19, -2.6, -4, 0xFFFFFF, 1, .55);
	cairo_restore(c);
	cairo_save(c); cairo_translate(c, 24, 24); cairo_rotate(c, .78); /* wrench */
	cairo_move_to(c, -2.8, -9); cairo_line_to(c, -2.8, 21); cairo_curve_to(c, -2.8, 24, 2.8, 24, 2.8, 21); cairo_line_to(c, 2.8, -9); cairo_close_path(c);
	hfill(c, -2.8, 2.8, 0xF6F8FB, 0xB0B6C2, 0x5C626E); edge(c, 0x2C3038, 1); cairo_new_path(c);
	cairo_arc(c, 0, -13.5, 7.6, 0, 2 * M_PI); hfill(c, -8, 8, 0xF6F8FB, 0xB0B6C2, 0x5C626E); edge(c, 0x2C3038, 1); cairo_new_path(c);
	cairo_move_to(c, -3.2, -23); cairo_line_to(c, 3.2, -23); cairo_line_to(c, 2.2, -15.5); cairo_line_to(c, -2.2, -15.5); cairo_close_path(c); rgba(c, 0x3A3F48, 1); cairo_fill(c);
	line(c, -1.6, -8, -1.6, 20, 0xFFFFFF, 1, .7);
	cairo_restore(c);
}
static void lock(cairo_t *c)
{
	ground(c, 24, 44.6, 13, 2, .9);
	cairo_move_to(c, 14.5, 22); cairo_line_to(c, 14.5, 15.5); cairo_curve_to(c, 14.5, 7, 33.5, 7, 33.5, 15.5); cairo_line_to(c, 33.5, 22); rgba(c, 0x5A606C, 1); cairo_set_line_width(c, 5); cairo_stroke_preserve(c);
	{ cairo_pattern_t *p = cairo_pattern_create_linear(14, 0, 34, 0); stop(p, 0, 0xF2F4F8, 1); stop(p, .5, 0x9CA2AE, 1); stop(p, 1, 0x555B66, 1); cairo_set_source(c, p); cairo_set_line_width(c, 3.2); cairo_stroke(c); cairo_pattern_destroy(p); }
	rr(c, 9, 21, 30, 22, 3); vfill(c, 21, 43, 0xFFE27A, 0xF2BC34, 0xE0A41C, 0xB87A0C); edge(c, 0x6E4A06, 1); cairo_new_path(c);
	rim(c, 9, 21, 30, 22, 3, .9);
	cairo_arc(c, 24, 30.5, 3.2, 0, 2 * M_PI); rgba(c, 0x4A3006, 1); cairo_fill(c);
	rr(c, 22.5, 31, 3, 7, 1.2); rgba(c, 0x4A3006, 1); cairo_fill(c);
}
static void powerorb(cairo_t *c, unsigned a, unsigned b, unsigned e)
{
	ground(c, 24, 45, 15, 2.2, .8);
	orb(c, 24, 23, 19.5, a, b, e);
}
static void power(cairo_t *c)
{
	powerorb(c, 0xFF9A7A, 0xC8240C, 0x7A1004);
	cairo_arc(c, 24, 24.5, 8.2, -M_PI / 2 + .62, 1.5 * M_PI - .62); rgba(c, 0xFFFFFF, 1); cairo_set_line_width(c, 3.2); cairo_set_line_cap(c, CAIRO_LINE_CAP_ROUND); cairo_stroke(c);
	line(c, 24, 13, 24, 24, 0xFFFFFF, 3.2, 1); cairo_set_line_cap(c, CAIRO_LINE_CAP_BUTT);
}
static void suspend(cairo_t *c)
{
	powerorb(c, 0x9ACBFF, 0x2A5CC8, 0x12308A);
	cairo_arc(c, 23, 24, 9, 0, 2 * M_PI); rgba(c, 0xFFF6C8, 1); cairo_fill(c);
	cairo_arc(c, 27.5, 21, 8, 0, 2 * M_PI); rgba(c, 0x2A5CC8, 1); cairo_fill(c);
}
static void restart(cairo_t *c)
{
	powerorb(c, 0xB6F08A, 0x3AA018, 0x1A5A0A);
	cairo_arc(c, 24, 24, 8.6, -2.4, 1.2); rgba(c, 0xFFFFFF, 1); cairo_set_line_width(c, 3.2); cairo_stroke(c);
	cairo_move_to(c, 28, 11.5); cairo_line_to(c, 36, 17.5); cairo_line_to(c, 26, 20); cairo_close_path(c); rgba(c, 0xFFFFFF, 1); cairo_fill(c);
}
static void speaker(cairo_t *c, int mute)
{
	ground(c, 24, 44, 15, 2, .7);
	cairo_move_to(c, 5, 18); cairo_line_to(c, 13, 18); cairo_line_to(c, 23, 8); cairo_line_to(c, 23, 40); cairo_line_to(c, 13, 30); cairo_line_to(c, 5, 30); cairo_close_path(c);
	hfill(c, 5, 23, 0xF4F6FA, 0xB4BAC6, 0x626875); cairo_set_line_join(c, CAIRO_LINE_JOIN_ROUND); edge(c, 0x2C3038, 1.2); cairo_new_path(c);
	if (!mute) for (int i = 0; i < 3; i++) { cairo_arc(c, 24, 24, 7 + i * 5.5, -.78, .78); rgba(c, 0x2F72D0, 1); cairo_set_line_width(c, 2.8); cairo_set_line_cap(c, CAIRO_LINE_CAP_ROUND); cairo_stroke(c); }
	else { line(c, 30, 17, 42, 31, 0xD02818, 4, 1); line(c, 42, 17, 30, 31, 0xD02818, 4, 1); }
	cairo_set_line_cap(c, CAIRO_LINE_CAP_BUTT);
}
static void battery(cairo_t *c, int plug)
{
	ground(c, 24, 40, 18, 2, .7);
	rr(c, 4.5, 14, 36, 20, 3); vfill(c, 14, 34, 0xFAFBFC, 0xE0E3EA, 0xCDD1DA, 0xA6ABB7); edge(c, 0x30343C, 1.1); cairo_new_path(c);
	rr(c, 40.5, 20, 4.2, 8, 1.4); hfill(c, 40, 45, 0xD0D3DA, 0x8A8F9B, 0x4A4F5A); edge(c, 0x30343C, .8); cairo_new_path(c);
	for (int i = 0; i < 4; i++) { rr(c, 7.5 + i * 8.2, 17, 6.6, 14, 1.2); vfill2(c, 17, 31, plug ? 0xBFEF8C : 0x9CE06A, plug ? 0x3E9A1E : 0x2E8A18); edge(c, 0x1A5A0E, .7); cairo_new_path(c); }
	if (plug) { cairo_move_to(c, 25, 11); cairo_line_to(c, 17, 25); cairo_line_to(c, 23, 25); cairo_line_to(c, 21, 37); cairo_line_to(c, 31, 21); cairo_line_to(c, 25, 21); cairo_close_path(c); vfill2(c, 11, 37, 0xFFF08A, 0xF0A818); cairo_set_line_join(c, CAIRO_LINE_JOIN_ROUND); edge(c, 0x7A4A06, 1); cairo_new_path(c); }
}
static void wifi(cairo_t *c, int off)
{
	ground(c, 24, 41, 14, 2, .6);
	for (int i = 3; i >= 1; i--) { cairo_arc(c, 24, 36, 7 * i + 1, -2.36, -.78); rgba(c, off ? 0xA0A4AE : 0x2F72D0, 1); cairo_set_line_width(c, 4); cairo_set_line_cap(c, CAIRO_LINE_CAP_ROUND); cairo_stroke(c); cairo_arc(c, 24, 36, 7 * i + 1, -2.2, -1.5); rgba(c, 0xFFFFFF, .5); cairo_set_line_width(c, 1.2); cairo_stroke(c); }
	cairo_arc(c, 24, 36, 3.2, 0, 2 * M_PI); rfill(c, 24, 36, 3.2, 0xFFFFFF, off ? 0xA0A4AE : 0x5C9AEA, off ? 0x606570 : 0x1A52B8); edge(c, 0x12308A, .6); cairo_new_path(c);
	cairo_set_line_cap(c, CAIRO_LINE_CAP_BUTT);
	if (off) { line(c, 8, 10, 40, 42, 0xD02818, 3.6, 1); }
}
static void bright(cairo_t *c)
{
	ground(c, 24, 44.5, 14, 2, .6);
	for (int i = 0; i < 8; i++) { double a = i * M_PI / 4; line(c, 24 + cos(a) * 13, 23 + sin(a) * 13, 24 + cos(a) * 19, 23 + sin(a) * 19, 0xF0A818, 3.2, 1); }
	cairo_arc(c, 24, 23, 10, 0, 2 * M_PI); rfill(c, 24, 23, 10, 0xFFFBC0, 0xFFD43A, 0xE08C0C); edge(c, 0x9A5A06, 1); cairo_new_path(c);
	cairo_arc(c, 21, 19.5, 3.4, 3.4, 5.2); rgba(c, 0xFFFFFF, .8); cairo_set_line_width(c, 1.6); cairo_stroke(c);
}
static void bell(cairo_t *c)
{
	ground(c, 24, 44.5, 13, 2, .8);
	cairo_move_to(c, 8, 34); cairo_curve_to(c, 13, 30, 12, 22, 13, 17); cairo_curve_to(c, 14, 10, 34, 10, 35, 17); cairo_curve_to(c, 36, 22, 35, 30, 40, 34); cairo_close_path(c);
	hfill(c, 8, 40, 0xFFEFA0, 0xF0BC34, 0x9C6A0A); edge(c, 0x6E4A06, 1); cairo_new_path(c);
	rr(c, 21, 6, 6, 5, 2.5); vfill2(c, 6, 11, 0xFFD25A, 0xB8820C); edge(c, 0x6E4A06, .8); cairo_new_path(c);
	cairo_arc(c, 24, 38, 3.6, 0, M_PI); vfill2(c, 36, 42, 0x8A9098, 0x4A4F58); edge(c, 0x2A2D32, .8); cairo_new_path(c);
	cairo_move_to(c, 14.5, 26); cairo_curve_to(c, 15.5, 20, 15, 16.5, 18, 14.5); rgba(c, 0xFFFFFF, .65); cairo_set_line_width(c, 1.6); cairo_stroke(c);
}
static void glyph_file(cairo_t *c, int kind)
{
	switch (kind) {
	case IC_IMAGE: file(c, 0); rr(c, 14, 19, 21, 15, 1); vfill2(c, 19, 34, 0x6AB0F4, 0xCDE8FF); edge(c, 0x2A4E78, .7); cairo_new_path(c);
		cairo_save(c); rr(c, 14, 19, 21, 15, 1); cairo_clip(c); cairo_move_to(c, 14, 34); cairo_line_to(c, 20, 26); cairo_line_to(c, 25, 31); cairo_line_to(c, 29, 27); cairo_line_to(c, 35, 34); cairo_close_path(c); rgba(c, 0x4C9A3A, 1); cairo_fill(c); cairo_restore(c); break;
	case IC_AUDIO: file(c, 0); cairo_arc(c, 21, 33, 3.2, 0, 2 * M_PI); rgba(c, 0x2A52B0, 1); cairo_fill(c); line(c, 24, 33, 24, 18, 0x2A52B0, 1.8, 1); cairo_move_to(c, 24, 18); cairo_curve_to(c, 28, 19, 31, 22, 30, 26); rgba(c, 0x2A52B0, 1); cairo_set_line_width(c, 1.8); cairo_stroke(c); break;
	case IC_ARCHIVE: file(c, 0xD8A838); for (int i = 0; i < 6; i++) { rr(c, 22 + (i % 2) * 2.4, 6 + i * 4, 2.4, 2.4, .4); rgba(c, 0x4A4F5A, 1); cairo_fill(c); } break;
	case IC_SCRIPT: file(c, 0x3A4A5A); txt(c, 24.5, 41.2, 7, ">_", 0xFFFFFF, 1); break;
	case IC_PDF: file(c, 0xD83A2A); txt(c, 24.5, 41.2, 7, "PDF", 0xFFFFFF, 1); break;
	}
}
static void draw_icon(cairo_t *c, int id)
{
	switch (id) {
	case IC_FOLDER: folder(c); break;
	case IC_HOME: folder_home(c); house(c, 24, 32, 9.5); break;
	case IC_FILE: file(c, 0); break;
	case IC_IMAGE: case IC_AUDIO: case IC_ARCHIVE: case IC_SCRIPT: case IC_PDF: glyph_file(c, id); break;
	case IC_DISK: disk(c); break;
	case IC_PREFS: prefs_tools(c); break;
	case IC_TRASH: trash(c); break;
	case IC_TERM: monitor(c, 0); break;
	case IC_MON: monitor(c, 2); break;
	case IC_DESKTOP: monitor(c, 1); break;
	case IC_SCREEN: monitor(c, 3); break;
	case IC_APPS: apps_icon(c); break;
	case IC_GLOBE: globe(c); break;
	case IC_APPEAR: palette(c); break;
	case IC_CLOCK: calendar(c); break;
	case IC_KEYBOARD: keyboard(c); break;
	case IC_INFO: case IC_ABOUT: info(c); break;
	case IC_BACK: arrow_dir(c, -M_PI / 2, 0xC8EE98, 0x4AA82A, 0x246A10, 1); break;
	case IC_UP: arrow_dir(c, 0, 0xC8EE98, 0x4AA82A, 0x246A10, 1); break;
	case IC_REFRESH: refresh(c); break;
	case IC_HOMEBTN: ground(c, 24, 44.5, 15, 2.2, .7); house(c, 24, 22, 19); break;
	case IC_SEARCH: search(c); break;
	case IC_CALC: calc(c); break;
	case IC_NOTE: note(c); break;
	case IC_VIEW: view(c); break;
	case IC_CHARMAP: charmap(c); break;
	case IC_RUN: runwin(c); break;
	case IC_FONT: fonticon(c); break;
	case IC_MOUSE: mouse(c); break;
	case IC_LOCK: lock(c); break;
	case IC_POWER: power(c); break;
	case IC_SUSPEND: suspend(c); break;
	case IC_RESTART: restart(c); break;
	case IC_SPEAKER: speaker(c, 0); break;
	case IC_MUTE: speaker(c, 1); break;
	case IC_BATTERY: battery(c, 0); break;
	case IC_PLUG: battery(c, 1); break;
	case IC_WIFI: wifi(c, 0); break;
	case IC_NOWIFI: wifi(c, 1); break;
	case IC_BRIGHT: bright(c); break;
	case IC_BELL: bell(c); break;
	}
}
#define APP "system/apps/"
static const struct { int id; const char *path; } ASSETS[] = {
	{IC_FOLDER, "folder.png"}, {IC_HOME, "home.png"}, {IC_DISK, "disk.png"}, {IC_TRASH, "trash.png"},
	{IC_TERM, "aterm.png"}, {IC_APPS, "applications.png"}, {IC_PREFS, "settings.png"}, {IC_FILE, "file.png"},
	{IC_GLOBE, "text_html.png"}, {IC_IMAGE, "image_png.png"}, {IC_AUDIO, "audio_mp3.png"},
	{IC_SCRIPT, "executable.png"},
	{IC_APPEAR, "appearance.png"}, {IC_DESKTOP, "desktop.png"},
	{IC_CLOCK, "datetime.png"}, {IC_FONT, "font.png"},
	{IC_KEYBOARD, "keyboard.png"}, {IC_MOUSE, "mouse.png"},
	{IC_SCREEN, "screen.png"},
	{IC_CALC, "albert.png"}, {IC_MON, "cpumon.png"}, {IC_MEM, "memmon.png"},
	{IC_NOTE, "aedit.png"}, {IC_VIEW, "aview.png"},
	{IC_CHARMAP, "charmap.png"}, {IC_RUN, "launch.png"},
	{IC_BACK, "fb-back.png"}, {IC_UP, "fb-up.png"}, {IC_REFRESH, "fb-reload.png"},
	{IC_HOMEBTN, "fb-home.png"}, {IC_SEARCH, "fb-view.png"},
	{IC_SPEAKER, "mixer.png"}, {IC_BATTERY, "battery.png"},
	{IC_ABOUT, "tswm-mark.png"}, {IC_LOGOUT, "dock-logout.png"}, {IC_WINDOW, "dock-window.png"},
};
#define NASSET ((int)(sizeof ASSETS / sizeof ASSETS[0]))
static cairo_surface_t *asset_img[NASSET + 1], *asset_dim[NASSET + 1]; static char asset_tried[NASSET + 1];
static int asset_index(int id) { for (int i = 0; i < NASSET; i++) if (ASSETS[i].id == id) return i; return -1; }
static cairo_surface_t *asset_get(int id, int dim)
{
	int i = asset_index(id); if (i < 0) return NULL;
	if (!asset_tried[i]) {
		asset_tried[i] = 1;
		char p[900]; snprintf(p, sizeof p, "%s/icons/%s", ui_datadir(), ASSETS[i].path);
		asset_img[i] = ui_image_load(p);
		if (asset_img[i]) {
			int w = cairo_image_surface_get_width(asset_img[i]), h = cairo_image_surface_get_height(asset_img[i]);
			asset_dim[i] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
			unsigned char *s0 = cairo_image_surface_get_data(asset_img[i]), *d = cairo_image_surface_get_data(asset_dim[i]);
			int ss = cairo_image_surface_get_stride(asset_img[i]), ds = cairo_image_surface_get_stride(asset_dim[i]);
			for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
				unsigned v = *(unsigned *)(s0 + y * ss + x * 4); unsigned a = v >> 24, r = v >> 16 & 255, g = v >> 8 & 255, b = v & 255;
				unsigned gr = (r * 30 + g * 59 + b * 11) / 100; gr = 140 + gr * 60 / 255; a = a * 6 / 10; gr = gr * a / 255;
				*(unsigned *)(d + y * ds + x * 4) = a << 24 | gr << 16 | gr << 8 | gr;
			}
			cairo_surface_mark_dirty(asset_dim[i]);
		}
	}
	return dim ? asset_dim[i] : asset_img[i];
}
static int draw_asset(cairo_t *c, int id, double x, double y, double size, int dim)
{
	cairo_surface_t *im = asset_get(id, dim); if (!im) return 0;
	double w = cairo_image_surface_get_width(im), h = cairo_image_surface_get_height(im), sc = size / (w > h ? w : h);
	cairo_save(c); cairo_new_path(c); cairo_translate(c, x + (size - w * sc) / 2, y + (size - h * sc) / 2); cairo_scale(c, sc, sc);
	cairo_set_source_surface(c, im, 0, 0); cairo_pattern_set_filter(cairo_get_source(c), sc < 1 ? CAIRO_FILTER_BEST : CAIRO_FILTER_BILINEAR); cairo_paint(c); cairo_restore(c);
	return 1;
}
void ui_icon(cairo_t *c, int id, double x, double y, double size)
{
	if (draw_asset(c, id, x, y, size, 0)) return;
	cairo_save(c); cairo_new_path(c); cairo_translate(c, x, y); cairo_scale(c, size / 48.0, size / 48.0);
	draw_icon(c, id); cairo_restore(c);
}
void ui_icon_dim(cairo_t *c, int id, double x, double y, double size)
{
	if (draw_asset(c, id, x, y, size, 1)) return;
	cairo_save(c); cairo_new_path(c); cairo_translate(c, x, y); cairo_scale(c, size / 48.0, size / 48.0);
	if (id == IC_BACK) arrow_dir(c, -M_PI / 2, 0xE4E6E8, 0xB4B8BC, 0x8A8E92, 0); else draw_icon(c, id);
	cairo_restore(c);
}
void ui_logo(cairo_t *c, double cx, double cy, double r)
{
	if (draw_asset(c, IC_ABOUT, cx - r, cy - r, 2 * r, 0)) return;
	cairo_save(c); cairo_new_path(c); cairo_translate(c, cx, cy); cairo_scale(c, r / 20.0, r / 20.0);
	cairo_arc(c, 0, 0, 20, 0, 2 * M_PI);
	cairo_pattern_t *p = cairo_pattern_create_linear(-14, -18, 14, 18);
	cairo_pattern_add_color_stop_rgb(p, 0, 1, .86, .3); cairo_pattern_add_color_stop_rgb(p, .5, .95, .55, .05); cairo_pattern_add_color_stop_rgb(p, 1, .75, .3, 0);
	cairo_set_source(c, p); cairo_fill(c); cairo_pattern_destroy(p);
	cairo_arc(c, 0, 0, 14.5, 0, 2 * M_PI); rfill(c, 0, 0, 14.5, 0xFFFFFF, 0xC4DAF4, 0x7AA8E0);
	cairo_select_font_face(c, "DejaVu Serif", CAIRO_FONT_SLANT_ITALIC, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(c, 25);
	cairo_text_extents_t e; cairo_text_extents(c, "S", &e);
	cairo_move_to(c, -e.x_advance / 2 - e.x_bearing * 0 , 8.5);
	cairo_text_path(c, "S"); ui_set(c, HEX(0xF4A010)); cairo_fill_preserve(c);
	ui_set(c, HEX(0x1C4A98)); cairo_set_line_width(c, 1.6); cairo_stroke(c);
	cairo_restore(c);
}

void ui_badge(cairo_t *c, double x, double y, double size, const char *name)
{
	unsigned h = 5381; for (const char *s = name; *s; s++) h = h * 33 + *s;
	double r = size / 2;
	cairo_save(c); cairo_new_path(c); cairo_arc(c, x + r, y + r, r - 1, 0, 2 * M_PI);
	cairo_pattern_t *p = cairo_pattern_create_linear(0, y, 0, y + size);
	double a = .30 + (h & 255) / 700.0, b = .30 + (h >> 8 & 255) / 700.0, d = .30 + (h >> 16 & 255) / 700.0;
	cairo_pattern_add_color_stop_rgb(p, 0, a + .18, b + .18, d + .18); cairo_pattern_add_color_stop_rgb(p, 1, a, b, d);
	cairo_set_source(c, p); cairo_fill(c); cairo_pattern_destroy(p);
	char l[2] = {name[0], 0}; if (l[0] >= 'a' && l[0] <= 'z') l[0] -= 32;
	cairo_select_font_face(c, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD); cairo_set_font_size(c, size * .55);
	cairo_text_extents_t e; cairo_text_extents(c, l, &e);
	ui_set(c, HEX(0xFFFFFF)); cairo_move_to(c, x + r - e.x_advance / 2 - e.x_bearing / 2 * 0, y + r + e.height / 2); cairo_show_text(c, l);
	cairo_restore(c);
}
