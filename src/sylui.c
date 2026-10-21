#define _GNU_SOURCE
#include "sylui.h"
#include <X11/Xatom.h>
#include <fontconfig/fontconfig.h>
#include <X11/Xlocale.h>
#include <dirent.h>
#include <locale.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

Theme T;

const struct Preset PRESETS[] = {
	{"Syllable", {0xD3D3D3, 0xFFFFFF, 0x000000, 0x6688E3, 0xD3D3D3, 0xBAC7E3, 0xD3D3D3, 0x396DAD}},
	{"Amber",    {0xD7D3CF, 0xFFFFFF, 0x000000, 0xE8B820, 0xBDB9B5, 0xF0D878, 0xE0DEDC, 0x5A4030}},
	{"Blueberry",{0xC8D2E0, 0xF8FAFF, 0x101820, 0x3A6EA5, 0x9FB2CC, 0x9DB8E0, 0xD6DEEA, 0x1E3A5F}},
	{"Graphite", {0x4A4A4E, 0x2B2B2E, 0xE8E8E8, 0x70707A, 0x3C3C40, 0x5A6C8C, 0x3A3A3E, 0x202024}},
	{"Mint",     {0xD0DDD2, 0xFAFFFA, 0x102014, 0x5E9A78, 0xB4C6B8, 0xA5D0B4, 0xD8E4DA, 0x2F5A48}},
	{"Rose",     {0xDDD0D4, 0xFFFAFB, 0x201018, 0xB06A82, 0xC9B8BF, 0xE0B4C4, 0xE6DADE, 0x5A2F44}},
	{"Retro 95", {0xC0C0C0, 0xFFFFFF, 0x000000, 0x000080, 0x808080, 0x000080, 0xC0C0C0, 0x008080}},
};
const int NPRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);
const char *COLNAMES[NCOL] = {"Application", "Window", "Text", "Active title", "Inactive title", "Selection", "Panel", "Desktop"};
const char *DECORS[3] = {"Syllable", "Beish", "Classic"};
static const char *KEYS[NCOL] = {"face", "bg", "text", "tact", "tina", "sel", "panel", "desk"};

Col col_mix(Col a, Col b, double t)
{
	return (Col){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
Col col_fg(Col b)
{
	return (.299 * b.r + .587 * b.g + .114 * b.b) > .55 ? HEX(0x000000) : HEX(0xFFFFFF);
}
Col ui_tint(Col c, double t)
{
	Col o; double k = 127.0 / 255.0 * (1 - t);
	o.r = c.r * t + k; o.g = c.g * t + k; o.b = c.b * t + k;
	if (o.r < 0) o.r = 0; if (o.r > 1) o.r = 1; if (o.g < 0) o.g = 0; if (o.g > 1) o.g = 1; if (o.b < 0) o.b = 0; if (o.b > 1) o.b = 1;
	return o;
}
void theme_derive(Theme *t)
{
	Col f = t->col[C_FACE];
	if (t->ext) { /* Syllable's own defaults */
		t->light = HEX(0xFFFFFF); t->shadow = HEX(0x969696); t->menubg = HEX(0xD7D7D7); t->selmenu = HEX(0x6586F7);
		t->sbbg = HEX(0x787878); t->sbknob = f; t->focus = HEX(0x00CC00);
	} else {
		t->light = col_mix(f, HEX(0xFFFFFF), .8); t->shadow = col_mix(f, HEX(0x000000), .3);
		t->menubg = col_mix(f, HEX(0xFFFFFF), .1); t->selmenu = t->col[C_TACT]; t->sbbg = col_mix(f, HEX(0x000000), .45); t->sbknob = f; t->focus = HEX(0x00CC00);
	}
	t->dark = ui_tint(t->shadow, .5);
}
void theme_preset(Theme *t, int i)
{
	for (int k = 0; k < NCOL; k++) t->col[k] = HEX(PRESETS[i].c[k]);
	t->ext = i == 0;
	theme_derive(t);
}

static const char *home(void) { const char *h = getenv("HOME"); return h ? h : "/tmp"; }
const char *ui_cfgdir(void)
{
	static char b[512];
	snprintf(b, sizeof b, "%s/.config/tswm", home());
	return b;
}
static void mkdirs(const char *p)
{
	char b[1024]; snprintf(b, sizeof b, "%s", p);
	for (char *s = b + 1; *s; s++)
		if (*s == '/') { *s = 0; mkdir(b, 0755); *s = '/'; }
	mkdir(b, 0755);
}
const char *ui_datadir(void)
{
	static char b[512];
	char x[512] = "", e[480];
	ssize_t n = readlink("/proc/self/exe", e, sizeof e - 1);
	if (n > 0) { e[n] = 0; char *sl = strrchr(e, '/'); if (sl) { *sl = 0; snprintf(x, sizeof x, "%s/../data", e); } }
	const char *c[5] = {getenv("TSWM_DATA"), x, NULL, "/usr/local/share/tswm", "/usr/share/tswm"};
	char u[512]; snprintf(u, sizeof u, "%s/.local/share/tswm", home()); c[2] = u;
	for (int i = 0; i < 5; i++) {
		if (!c[i] || !*c[i]) continue;
		char w[600]; snprintf(w, sizeof w, "%s/wallpapers", c[i]);
		if (access(w, R_OK) == 0) { if (!realpath(c[i], b)) snprintf(b, sizeof b, "%s", c[i]); return b; }
	}
	snprintf(b, sizeof b, "%s", u);
	return b;
}
const char *ui_wall_path(const char *w)
{
	static char b[1024];
	if (!strcmp(w, "none")) return NULL;
	if (!*w) w = "alpine-slope.png";
	if (w[0] == '/') return w;
	snprintf(b, sizeof b, "%s/wallpapers/%s", ui_datadir(), w);
	return b;
}
static int cmpstr(const void *a, const void *b) { return strcasecmp(a, b); }
int ui_wall_list(char names[][256], int max)
{
	int n = 0;
	char dirs[1][600];
	snprintf(dirs[0], 600, "%s/wallpapers", ui_datadir());
	for (int i = 0; i < 1; i++) {
		DIR *d = opendir(dirs[i]);
		if (!d) continue;
		int start = n;
		struct dirent *e;
		while ((e = readdir(d)) && n < max) {
			size_t l = strlen(e->d_name);
			if (l > 4 && (!strcasecmp(e->d_name + l - 4, ".png") || !strcasecmp(e->d_name + l - 4, ".jpg") || !strcasecmp(e->d_name + l - 5, ".jpeg") || !strcasecmp(e->d_name + l - 5, ".webp")) && l + strlen(dirs[i]) < 250)
				snprintf(names[n++], 256, "%s/%s", dirs[i], e->d_name);
		}
		closedir(d);
		qsort(names + start, n - start, 256, cmpstr);
	}
	return n;
}
void ui_spawn(const char *cmd)
{
	if (fork() == 0) {
		setsid();
		execlp("sh", "sh", "-c", cmd, (char *)NULL);
		_exit(1);
	}
}

int theme_save_to(const Theme *t, const char *path, int colors_only)
{
	char dir[1024]; snprintf(dir, sizeof dir, "%s", path);
	char *sl = strrchr(dir, '/'); if (sl) { *sl = 0; mkdirs(dir); }
	FILE *f = fopen(path, "w");
	if (!f) return -1;
	for (int i = 0; i < NCOL; i++)
		fprintf(f, "%s=%02X%02X%02X\n", KEYS[i], (int)(t->col[i].r * 255 + .5), (int)(t->col[i].g * 255 + .5), (int)(t->col[i].b * 255 + .5));
	fprintf(f, "decor=%d\n", t->decor);
	if (!colors_only) fprintf(f, "clock24=%d\nsecs=%d\nwall=%s\nfont=%s\nfsize=%d\nicons=%s\nmspeed=%d\nmleft=%d\nlock=%d\ndpms=%d\nwatermark=%d\n", t->clock24, t->clock_secs, t->wall, t->font, t->fsize, t->icons, t->mouse_speed, t->mouse_left, t->lock_min, t->dpms_min, t->watermark);
	fclose(f);
	return 0;
}
int theme_load_from(Theme *t, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return -1;
	char l[700];
	while (fgets(l, sizeof l, f)) {
		l[strcspn(l, "\n")] = 0;
		char *v = strchr(l, '='); if (!v) continue;
		*v++ = 0;
		for (int i = 0; i < NCOL; i++)
			if (!strcmp(l, KEYS[i])) t->col[i] = HEX((unsigned)strtoul(v, NULL, 16));
		if (!strcmp(l, "decor")) t->decor = atoi(v) % 3;
		else if (!strcmp(l, "clock24")) t->clock24 = atoi(v);
		else if (!strcmp(l, "secs")) t->clock_secs = atoi(v);
		else if (!strcmp(l, "wall")) snprintf(t->wall, sizeof t->wall, "%s", v);
		else if (!strcmp(l, "font")) snprintf(t->font, sizeof t->font, "%s", v);
		else if (!strcmp(l, "fsize")) { t->fsize = atoi(v); if (t->fsize < 40) t->fsize = t->fsize * 75 / 10; /* old pixel sizes */ }
		else if (!strcmp(l, "mspeed")) t->mouse_speed = atoi(v);
		else if (!strcmp(l, "mleft")) t->mouse_left = atoi(v);
		else if (!strcmp(l, "lock")) t->lock_min = atoi(v);
		else if (!strcmp(l, "dpms")) t->dpms_min = atoi(v);
		else if (!strcmp(l, "watermark")) t->watermark = atoi(v);
		else if (!strcmp(l, "icons")) snprintf(t->icons, sizeof t->icons, "%s", v);
	}
	fclose(f);
	theme_derive(t);
	return 0;
}
static void fonts_init(void)
{
	static int done;
	if (done) return;
	done = 1;
	char d[700]; snprintf(d, sizeof d, "%s/fonts", ui_datadir());
	DIR *dir = opendir(d); struct dirent *e;
	while (dir && (e = readdir(dir))) {
		size_t l = strlen(e->d_name);
		if (l > 4 && !strcmp(e->d_name + l - 4, ".ttf")) { char f[900]; snprintf(f, sizeof f, "%s/%s", d, e->d_name); FcConfigAppFontAddFile(NULL, (const FcChar8 *)f); }
	}
	if (dir) closedir(dir);
}
void theme_load(void)
{
	fonts_init();
	memset(&T, 0, sizeof T);
	T.clock24 = 1; T.clock_secs = 1; T.lock_min = 10; T.dpms_min = 15; T.watermark = 1; T.fsize = 85; snprintf(T.font, sizeof T.font, "DejaVu Sans");
	theme_preset(&T, 0);
	char p[600]; snprintf(p, sizeof p, "%s/theme.conf", ui_cfgdir());
	theme_load_from(&T, p);
	if (access(ui_preview_path(), R_OK) == 0) theme_load_from(&T, ui_preview_path());
}
const char *ui_preview_path(void)
{
	static char b[640]; snprintf(b, sizeof b, "%s/preview.conf", ui_cfgdir()); return b;
}
int theme_equal(const Theme *a, const Theme *b)
{
	for (int i = 0; i < NCOL; i++) if (a->col[i].r != b->col[i].r || a->col[i].g != b->col[i].g || a->col[i].b != b->col[i].b) return 0;
	return a->decor == b->decor && a->clock24 == b->clock24 && a->clock_secs == b->clock_secs && a->fsize == b->fsize && a->mouse_speed == b->mouse_speed
		&& a->mouse_left == b->mouse_left && a->lock_min == b->lock_min && a->dpms_min == b->dpms_min && a->watermark == b->watermark && !strcmp(a->wall, b->wall) && !strcmp(a->font, b->font) && !strcmp(a->icons, b->icons);
}
int theme_save(void)
{
	char p[600]; snprintf(p, sizeof p, "%s/theme.conf", ui_cfgdir());
	return theme_save_to(&T, p, 0);
}
void ui_notify_theme(Display *d)
{
	Atom a = XInternAtom(d, "TSWM_THEME", False);
	long v = time(NULL);
	XChangeProperty(d, DefaultRootWindow(d), a, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&v, 1);
	XFlush(d);
}

/* ---- drawing ---- */
void ui_set(cairo_t *c, Col k) { cairo_set_source_rgb(c, k.r, k.g, k.b); }
void ui_rect(cairo_t *c, double x, double y, double w, double h, Col k)
{
	ui_set(c, k); cairo_rectangle(c, x, y, w, h); cairo_fill(c);
}
void ui_vgrad(cairo_t *c, double x, double y, double w, double h, Col a, Col b)
{
	cairo_pattern_t *p = cairo_pattern_create_linear(0, y, 0, y + h);
	cairo_pattern_add_color_stop_rgb(p, 0, a.r, a.g, a.b);
	cairo_pattern_add_color_stop_rgb(p, 1, b.r, b.g, b.b);
	cairo_set_source(c, p); cairo_rectangle(c, x, y, w, h); cairo_fill(c);
	cairo_pattern_destroy(p);
}
static void hline(cairo_t *c, int x0, int x1, int y, Col k) { if (x1 >= x0) ui_rect(c, x0, y, x1 - x0 + 1, 1, k); }
static void vline(cairo_t *c, int x, int y0, int y1, Col k) { if (y1 >= y0) ui_rect(c, x, y0, 1, y1 - y0 + 1, k); }
/* View::DrawFrame */
void ui_frame(cairo_t *c, double fx, double fy, double fw, double fh, int st, Col erase)
{
	int x0 = (int)fx, y0 = (int)fy, x1 = (int)(fx + fw) - 1, y1 = (int)(fy + fh) - 1;
	int sunk = !(st & FR_RAISED) && (st & FR_RECESSED);
	Col fg = T.light, bg = T.shadow;
	if (st & FR_DISABLED) { fg = ui_tint(fg, .6); bg = ui_tint(bg, .4); }
	Col fgS = ui_tint(fg, .6), bgS = ui_tint(bg, .5);
	if (st & FR_FLAT) { Col k = sunk ? bg : fg; hline(c, x0, x1, y0, k); hline(c, x0, x1, y1, k); vline(c, x0, y0, y1, k); vline(c, x1, y0, y1, k); return; }
	Col tl = (st & FR_THIN) ? (sunk ? bg : fg) : (sunk ? bg : bgS);
	vline(c, x0, y0, y1, tl); hline(c, x0, x1, y0, tl);
	Col br = (st & FR_THIN) ? (sunk ? fg : bg) : (sunk ? bg : bgS);
	vline(c, x1, y0, y1, br); hline(c, x0, x1, y1, br);
	if (!(st & FR_THIN)) {
		if (st & FR_ETCHED) {
			Col a = sunk ? bg : fg, b = sunk ? fg : bg;
			vline(c, x0 + 1, y0 + 1, y1 - 1, a); hline(c, x0 + 1, x1 - 1, y0 + 1, a);
			vline(c, x1 - 1, y0 + 1, y1 - 1, b); hline(c, x0 + 1, x1 - 1, y1 - 1, b);
		} else {
			Col a = sunk ? bgS : fg, b = sunk ? fgS : bg;
			vline(c, x0 + 1, y0 + 1, y1 - 1, a); hline(c, x0 + 1, x1 - 1, y0 + 1, a);
			vline(c, x1 - 1, y0 + 2, y1 - 1, b); hline(c, x0 + 2, x1 - 1, y1 - 1, b);
		}
		if (!(st & FR_TRANSPARENT)) ui_rect(c, x0 + 2, y0 + 2, x1 - x0 - 3, y1 - y0 - 3, erase);
	} else if (!(st & FR_TRANSPARENT)) ui_rect(c, x0 + 1, y0 + 1, x1 - x0 - 1, y1 - y0 - 1, erase);
}
void ui_bevel(cairo_t *c, double x, double y, double w, double h, int st)
{
	if (st == 0) ui_frame(c, x, y, w, h, FR_RAISED, FACE);
	else if (st == 1) ui_frame(c, x, y, w, h, FR_RECESSED | FR_TRANSPARENT, FACE);
	else ui_frame(c, x, y, w, h, FR_ETCHED | FR_TRANSPARENT, FACE);
}
static cairo_font_options_t *fopt;
static void fontopt(cairo_t *c)
{
	if (!fopt) { fopt = cairo_font_options_create(); cairo_font_options_set_antialias(fopt, CAIRO_ANTIALIAS_GRAY); cairo_font_options_set_hint_style(fopt, CAIRO_HINT_STYLE_FULL); cairo_font_options_set_hint_metrics(fopt, CAIRO_HINT_METRICS_ON); }
	cairo_set_font_options(c, fopt);
}
/* points at 96dpi */
static void font(cairo_t *c, int bold)
{
	cairo_select_font_face(c, T.font[0] ? T.font : "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
	double pt = T.fsize > 0 ? T.fsize / 10.0 : 8.5;
	cairo_set_font_size(c, (bold ? pt - .5 : pt) * 96.0 / 72.0);
	fontopt(c);
}
static void font_title(cairo_t *c)
{
	cairo_select_font_face(c, "DejaVu Sans Condensed", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(c, 8.0 * 96.0 / 72.0); fontopt(c);
}
double ui_tw(cairo_t *c, const char *s, int bold)
{
	cairo_text_extents_t e; font(c, bold); cairo_text_extents(c, s, &e); return e.x_advance;
}
double ui_tw_title(cairo_t *c, const char *s)
{
	cairo_text_extents_t e; font_title(c); cairo_text_extents(c, s, &e); return e.x_advance;
}
void ui_text(cairo_t *c, double x, double cy, const char *s, Col k, int bold)
{
	cairo_font_extents_t fe; font(c, bold); cairo_font_extents(c, &fe);
	ui_set(c, k); cairo_move_to(c, floor(x), floor(cy + (fe.ascent - fe.descent) / 2 + .5)); cairo_show_text(c, s);
}
void ui_text_title(cairo_t *c, double x, double cy, const char *s, Col k)
{
	cairo_font_extents_t fe; font_title(c); cairo_font_extents(c, &fe);
	ui_set(c, k); cairo_move_to(c, floor(x), floor(cy + (fe.ascent - fe.descent) / 2 + .5)); cairo_show_text(c, s);
}
void ui_text_c(cairo_t *c, double cx, double cy, const char *s, Col k, int bold)
{
	ui_text(c, cx - ui_tw(c, s, bold) / 2, cy, s, k, bold);
}
void ui_text_fit(cairo_t *c, double x, double cy, const char *s, double maxw, Col k, int bold)
{
	if (ui_tw(c, s, bold) <= maxw) { ui_text(c, x, cy, s, k, bold); return; }
	char b[512]; snprintf(b, sizeof b, "%s", s);
	int n = strlen(b);
	while (n > 1) {
		n--;
		while (n > 1 && (b[n] & 0xC0) == 0x80) n--;
		b[n] = 0;
		char t[520]; snprintf(t, sizeof t, "%s...", b);
		if (ui_tw(c, t, bold) <= maxw) { ui_text(c, x, cy, t, k, bold); return; }
	}
}
static void text_m(cairo_t *c, double x, double cy, const char *s, Col k, int bold)
{
	const char *u = strchr(s, '_');
	if (!u) { ui_text(c, x, cy, s, k, bold); return; }
	char a[256], m[8] = {0}, b[256]; snprintf(a, sizeof a, "%.*s", (int)(u - s), s);
	int l = 1; while ((u[1 + l] & 0xC0) == 0x80) l++; snprintf(m, sizeof m, "%.*s", l, u + 1); snprintf(b, sizeof b, "%s", u + 1 + l);
	double wa = ui_tw(c, a, bold), wm = ui_tw(c, m, bold);
	cairo_font_extents_t fe; font(c, bold); cairo_font_extents(c, &fe);
	ui_text(c, x, cy, a, k, bold); ui_text(c, x + wa, cy, m, k, bold); ui_text(c, x + wa + wm, cy, b, k, bold);
	double by = floor(cy + (fe.ascent - fe.descent) / 2 + .5) + 2; ui_rect(c, floor(x + wa), by, wm, 1, k);
}
static double tw_m(cairo_t *c, const char *s, int bold)
{
	char b[300]; int j = 0; int seen = 0;
	for (const char *p = s; *p && j < 298; p++) { if (*p == '_' && !seen) { seen = 1; continue; } b[j++] = *p; }
	b[j] = 0; return ui_tw(c, b, bold);
}
void ui_button(cairo_t *c, double x, double y, double w, double h, const char *label, int st)
{
	if (st == 2 || st == 3) {
		Col k = st == 2 ? HEX(0x000000) : T.focus;
		hline(c, x, x + w - 1, y, k); hline(c, x, x + w - 1, y + h - 1, k); vline(c, x, y, y + h - 1, k); vline(c, x + w - 1, y, y + h - 1, k);
		x += 1; y += 1; w -= 2; h -= 2;
	}
	ui_frame(c, x, y, w, h, st == 1 ? FR_RECESSED : FR_RAISED, FACE);
	int o = st == 1;
	char b[300]; int j = 0, seen = 0; for (const char *p = label; *p && j < 298; p++) { if (*p == '_' && !seen) { seen = 1; continue; } b[j++] = *p; }
	b[j] = 0; double tw = ui_tw(c, b, 0);
	text_m(c, x + (w - tw) / 2 + o, y + h / 2 + o, label, HEX(0x000000), 0);
}
void ui_group(cairo_t *c, double x, double y, double w, double h, const char *label)
{
	ui_frame(c, x, y + 7, w, h - 7, FR_ETCHED | FR_TRANSPARENT, FACE);
	if (label) {
		double tw = tw_m(c, label, 0);
		ui_rect(c, x + 8, y, tw + 8, 15, FACE);
		text_m(c, x + 12, y + 7, label, col_fg(FACE), 0);
	}
}
void ui_field(cairo_t *c, double x, double y, double w, double h, const char *s, int caret)
{
	ui_frame(c, x, y, w, h, FR_RECESSED, BGC);
	cairo_save(c); cairo_rectangle(c, x + 2, y + 2, w - 4, h - 4); cairo_clip(c);
	double tw = ui_tw(c, s, 0), tx = x + 4;
	if (tw > w - 10) tx -= tw - (w - 10);
	Col fg = col_fg(BGC);
	ui_text(c, tx, y + h / 2, s, fg, 0);
	if (caret) ui_rect(c, tx + tw + 1, y + 3, 1, h - 6, fg);
	cairo_restore(c);
}
void ui_arrow(cairo_t *c, double cx, double cy, int dir, double s, Col k)
{
	ui_set(c, k);
	switch (dir) {
	case 0: cairo_move_to(c, cx - s, cy + s / 2); cairo_line_to(c, cx + s, cy + s / 2); cairo_line_to(c, cx, cy - s / 2); break;
	case 1: cairo_move_to(c, cx - s, cy - s / 2); cairo_line_to(c, cx + s, cy - s / 2); cairo_line_to(c, cx, cy + s / 2); break;
	case 2: cairo_move_to(c, cx + s / 2, cy - s); cairo_line_to(c, cx + s / 2, cy + s); cairo_line_to(c, cx - s / 2, cy); break;
	default: cairo_move_to(c, cx - s / 2, cy - s); cairo_line_to(c, cx - s / 2, cy + s); cairo_line_to(c, cx + s / 2, cy); break;
	}
	cairo_close_path(c); cairo_fill(c);
}
static void pxarrow(cairo_t *c, int cx, int cy, int dir, Col k)
{
	for (int i = 0; i < 5; i++) {
		int hw = 4 - i;
		if (dir == 1) ui_rect(c, cx - hw, cy - 2 + i, hw * 2 + 1, 1, k);
		else if (dir == 0) ui_rect(c, cx - hw, cy + 2 - i, hw * 2 + 1, 1, k);
		else if (dir == 2) ui_rect(c, cx + 2 - i, cy - hw, 1, hw * 2 + 1, k);
		else ui_rect(c, cx - 2 + i, cy - hw, 1, hw * 2 + 1, k);
	}
}
void ui_dropdown_ex(cairo_t *c, double x, double y, double w, double h, const char *s, int focus)
{
	double aw = floor((h + 1) * .9); if (aw < 14) aw = 14;
	ui_field(c, x, y, w - aw - 1, h, s, 0);
	double ax = x + w - aw, ay = y, ah = h;
	if (focus) { Col k = T.focus; hline(c, ax, ax + aw - 1, ay, k); hline(c, ax, ax + aw - 1, ay + ah - 1, k); vline(c, ax, ay, ay + ah - 1, k); vline(c, ax + aw - 1, ay, ay + ah - 1, k); ax += 1; ay += 1; aw -= 2; ah -= 2; }
	ui_frame(c, ax, ay, aw, ah, FR_RAISED, FACE);
	pxarrow(c, (int)(ax + aw / 2), (int)(ay + ah / 2), 1, HEX(0x000000));
}
void ui_dropdown(cairo_t *c, double x, double y, double w, double h, const char *s) { ui_dropdown_ex(c, x, y, w, h, s, 0); }
void ui_check(cairo_t *c, double x, double y, const char *label, int on)
{
	ui_frame(c, x, y, 13, 13, FR_RAISED | FR_TRANSPARENT, FACE);
	ui_rect(c, x + 2, y + 2, 9, 9, FACE);
	if (on) {
		cairo_set_line_width(c, 1); ui_set(c, HEX(0x000000));
		for (int d = 0; d < 2; d++) { cairo_move_to(c, x + 3 + d + .5, y + 3 + .5); cairo_line_to(c, x + 10 + d - .5, y + 10 - .5); cairo_move_to(c, x + 3 + d + .5, y + 10 - .5); cairo_line_to(c, x + 10 + d - .5, y + 3 + .5); }
		cairo_stroke(c);
	}
	text_m(c, x + 20, y + 6.5, label, HEX(0x000000), 0);
}
void ui_slider(cairo_t *c, double x, double y, double w, int v)
{
	int ty = (int)y + 4;
	ui_frame(c, x, ty, w, 7, FR_RECESSED | FR_TRANSPARENT, FACE);
	ui_rect(c, x + 2, ty + 2, w - 4, 3, T.sbbg);
	double kx = x + (w - 9) * v / 255.0;
	for (int i = 0; i <= (int)((w - 2) / 8); i++) ui_rect(c, x + 1 + i * 8, ty + 10, 1, 3, T.shadow);
	ui_frame(c, kx, y - 1, 9, 15, FR_RAISED, T.sbknob);
}
void ui_vscroll_thumb(double y, double h, double pos, double view, double total, double *ty, double *th)
{
	double tr = h - 32;
	double t = total <= view ? tr : tr * view / total;
	if (t < 20) t = 20;
	double mx = total - view;
	*th = t; *ty = y + 16 + (mx > 0 ? (tr - t) * pos / mx : 0);
}
void ui_vscroll(cairo_t *c, double x, double y, double w, double h, double pos, double view, double total)
{
	ui_rect(c, x, y, w, h, T.sbbg);
	ui_frame(c, x, y, w, 16, FR_RAISED, FACE); pxarrow(c, (int)(x + w / 2), (int)(y + 8), 0, HEX(0x000000));
	ui_frame(c, x, y + h - 16, w, 16, FR_RAISED, FACE); pxarrow(c, (int)(x + w / 2), (int)(y + h - 8), 1, HEX(0x000000));
	double ty, th; ui_vscroll_thumb(y, h, pos, view, total, &ty, &th);
	if (total > view) {
		ui_frame(c, x, ty, w, th, FR_RAISED, T.sbknob);
		if (th > 30) { double cy = ty + (th + 1) / 2; for (int i = -1; i <= 1; i++) { hline(c, x + 4, x + w - 5, cy + i * 5, T.light); hline(c, x + 4, x + w - 5, cy + i * 5 - 1, T.shadow); } }
	}
}

/* ---- toolbar ---- */
void ui_toolbar(cairo_t *c, int W, int back_ok, const char *vl)
{
	ui_vgrad(c, 0, 0, W, TB_H, col_mix(FACE, HEX(0xFFFFFF), .25), FACE);
	ui_rect(c, 0, TB_H - 1, W, 1, T.shadow);
	ui_rect(c, 3, 6, 3, TB_H - 12, T.shadow); ui_rect(c, 3, 6, 1, TB_H - 12, T.light);
	for (int i = 0; i < 4; i++) {
		int ic[4] = {IC_BACK, IC_UP, IC_REFRESH, IC_HOMEBTN};
		if (i == 0 && !back_ok) ui_icon_dim(c, ic[i], 12 + i * 34, 7, 30);
		else ui_icon(c, ic[i], 12 + i * 34, 7, 30);
	}
	ui_icon(c, IC_SEARCH, W - 84, 7, 28);
	ui_text(c, W - 56, TB_H / 2, vl, col_fg(FACE), 0);
	ui_arrow(c, W - 12, TB_H / 2 + 1, 1, 3.5, col_fg(FACE));
}
int ui_toolbar_hit(int x, int y, int W)
{
	if (y < 0 || y >= TB_H) return -1;
	for (int i = 0; i < 4; i++) if (x >= 10 + i * 34 && x < 10 + i * 34 + 33) return i;
	if (x >= W - 90) return 4;
	return -1;
}

/* ---- app framework ---- */
App *app_new(const char *title, const char *cls, int w, int h, int fixed)
{
	setlocale(LC_ALL, ""); XSetLocaleModifiers("");
	App *a = calloc(1, sizeof *a);
	a->d = XOpenDisplay(NULL);
	if (!a->d) { fprintf(stderr, "%s: cannot open display\n", cls); exit(1); }
	a->scr = DefaultScreen(a->d); a->W = w; a->H = h; a->dirty = 1;
	theme_load();
	Window root = RootWindow(a->d, a->scr);
	a->w = XCreateSimpleWindow(a->d, root, 0, 0, w, h, 0, 0, 0);
	XSetWindowAttributes at = {.background_pixmap = None};
	XChangeWindowAttributes(a->d, a->w, CWBackPixmap, &at);
	XSelectInput(a->d, a->w, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask);
	XSelectInput(a->d, root, PropertyChangeMask);
	app_title(a, title);
	XClassHint ch = {(char *)cls, (char *)cls}; XSetClassHint(a->d, a->w, &ch);
	a->wmdel = XInternAtom(a->d, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(a->d, a->w, &a->wmdel, 1);
	XSizeHints sh = {.flags = PMinSize, .min_width = 200, .min_height = 120};
	if (fixed) { sh.flags = PMinSize | PMaxSize; sh.min_width = sh.max_width = w; sh.min_height = sh.max_height = h; }
	XSetWMNormalHints(a->d, a->w, &sh);
	XIM im = XOpenIM(a->d, NULL, NULL, NULL);
	if (im) a->ic = XCreateIC(im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, a->w, XNFocusWindow, a->w, NULL);
	a->sf = cairo_xlib_surface_create(a->d, a->w, DefaultVisual(a->d, a->scr), w, h);
	a->cr = cairo_create(a->sf);
	XMapWindow(a->d, a->w);
	return a;
}
void app_title(App *a, const char *t)
{
	XStoreName(a->d, a->w, t);
	XChangeProperty(a->d, a->w, XInternAtom(a->d, "_NET_WM_NAME", False), XInternAtom(a->d, "UTF8_STRING", False), 8, PropModeReplace, (const unsigned char *)t, strlen(t));
}
void app_redraw(App *a) { a->dirty = 1; }
int app_isdbl(App *a, unsigned long t, int id)
{
	int d = id == a->lastid && t - a->lastt < 400;
	a->lastt = t; a->lastid = d ? -2 : id;
	return d;
}
static void app_paint(App *a)
{
	cairo_push_group(a->cr);
	ui_rect(a->cr, 0, 0, a->W, a->H, FACE);
	if (a->draw) a->draw(a);
	cairo_pop_group_to_source(a->cr);
	cairo_paint(a->cr);
	cairo_surface_flush(a->sf);
	XFlush(a->d);
	a->dirty = 0;
}
void app_run(App *a)
{
	Atom th = XInternAtom(a->d, "TSWM_THEME", False);
	while (!a->quit) {
		if (!XPending(a->d) && a->dirty) app_paint(a);
		if (a->tick && !XPending(a->d)) {
			struct pollfd pf = {ConnectionNumber(a->d), POLLIN, 0};
			if (poll(&pf, 1, a->tick_ms) == 0) { a->tick(a); a->dirty = 1; continue; }
		}
		XEvent e; XNextEvent(a->d, &e);
		switch (e.type) {
		case Expose: if (!e.xexpose.count) a->dirty = 1; break;
		case ConfigureNotify:
			if (e.xconfigure.window == a->w && (e.xconfigure.width != a->W || e.xconfigure.height != a->H)) {
				a->W = e.xconfigure.width; a->H = e.xconfigure.height;
				cairo_xlib_surface_set_size(a->sf, a->W, a->H); a->dirty = 1;
			}
			break;
		case ButtonPress: if (a->press) a->press(a, e.xbutton.x, e.xbutton.y, e.xbutton.button, e.xbutton.time); a->dirty = 1; break;
		case ButtonRelease: if (a->release) a->release(a, e.xbutton.x, e.xbutton.y, e.xbutton.button); a->dirty = 1; break;
		case MotionNotify:
			while (XCheckTypedWindowEvent(a->d, a->w, MotionNotify, &e));
			if (a->motion) a->motion(a, e.xmotion.x, e.xmotion.y, e.xmotion.state);
			a->dirty = 1; break;
		case KeyPress: {
			char buf[64]; KeySym ks = 0; int n;
			if (a->ic) { Status st; n = Xutf8LookupString(a->ic, &e.xkey, buf, 63, &ks, &st); if (st == XBufferOverflow || st == XLookupNone) n = 0; }
			else n = XLookupString(&e.xkey, buf, 63, &ks, NULL);
			buf[n < 0 ? 0 : n] = 0;
			if (a->key) a->key(a, ks, buf, n, e.xkey.state);
			a->dirty = 1; break;
		}
		case PropertyNotify:
			if (e.xproperty.atom == th) { theme_load(); if (a->reload) a->reload(a); a->dirty = 1; }
			break;
		case ClientMessage:
			if ((Atom)e.xclient.data.l[0] == a->wmdel) a->quit = 1;
			break;
		case SelectionClear: free(a->clip); a->clip = NULL; break;
		case SelectionRequest: {
			XSelectionRequestEvent *r = &e.xselectionrequest; XSelectionEvent n = {.type = SelectionNotify, .display = r->display, .requestor = r->requestor, .selection = r->selection, .target = r->target, .property = r->property, .time = r->time};
			Atom u8 = XInternAtom(a->d, "UTF8_STRING", False), tg = XInternAtom(a->d, "TARGETS", False);
			if (r->target == tg) { Atom l[3] = {tg, u8, XA_STRING}; XChangeProperty(a->d, r->requestor, r->property, XA_ATOM, 32, PropModeReplace, (unsigned char *)l, 3); }
			else if (a->clip && (r->target == u8 || r->target == XA_STRING)) XChangeProperty(a->d, r->requestor, r->property, r->target, 8, PropModeReplace, (unsigned char *)a->clip, strlen(a->clip));
			else n.property = None;
			XSendEvent(a->d, r->requestor, False, 0, (XEvent *)&n); break;
		}
		}
	}
}

void ui_edit(char *buf, int cap, KeySym k, const char *t, int n)
{
	int l = strlen(buf);
	if (k == 0xff08) { while (l > 0 && (buf[l - 1] & 0xC0) == 0x80) l--; if (l > 0) l--; buf[l] = 0; }
	else if (n > 0 && (unsigned char)t[0] >= 32 && t[0] != 127 && l + n < cap) { memcpy(buf + l, t, n); buf[l + n] = 0; }
}
void ui_open(const char *path)
{
	const char *ext = strrchr(path, '.'); char cmd[2200], q[2100]; int j = 0;
	for (const char *s = path; *s && j < 2080; s++) { if (*s == '\'') { q[j++] = '\''; q[j++] = '\\'; q[j++] = '\''; } q[j++] = *s; }
	q[j] = 0;
	const char *prog = "xdg-open";
	if (ext) {
		static const char *txt[] = {".txt", ".md", ".c", ".h", ".cpp", ".py", ".sh", ".lua", ".conf", ".cfg", ".log", ".json", ".nix", ".toml", ".ini", ".css", ".html", ".js", ".mk", ".yml", ".yaml", ".desktop", NULL};
		{ static const char *im[] = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".tif", ".tiff", NULL}; for (int i = 0; im[i]; i++) if (!strcasecmp(ext, im[i])) prog = "aview"; }
		for (int i = 0; txt[i]; i++) if (!strcasecmp(ext, txt[i])) prog = "aedit";
	} else {
		const char *b = strrchr(path, '/'); b = b ? b + 1 : path;
		if (!strcmp(b, "Makefile") || !strcmp(b, "README") || !strcmp(b, "LICENSE")) prog = "aedit";
	}
	snprintf(cmd, sizeof cmd, "%s '%s'", prog, q);
	ui_spawn(cmd);
}

void app_copy(App *a, const char *utf8)
{
	free(a->clip); a->clip = strdup(utf8);
	XSetSelectionOwner(a->d, XInternAtom(a->d, "CLIPBOARD", False), a->w, CurrentTime);
	XSetSelectionOwner(a->d, XA_PRIMARY, a->w, CurrentTime);
}

/* ---- installed apps ---- */
const char *UI_SECTIONS[6] = {"Other", "Office", "Media", "Preferences", "System Tools", "Internet"};
static int section_of(const char *cats)
{
	static const struct { const char *c; int s; } M[] = {
		{"Settings", 3}, {"DesktopSettings", 3}, {"HardwareSettings", 3}, {"PackageManager", 3},
		{"WebBrowser", 5}, {"Email", 5}, {"Network", 5}, {"Chat", 5}, {"InstantMessaging", 5}, {"FileTransfer", 5}, {"P2P", 5}, {"IRCClient", 5},
		{"AudioVideo", 2}, {"Audio", 2}, {"Video", 2}, {"Player", 2}, {"Recorder", 2}, {"Music", 2},
		{"Office", 1}, {"WordProcessor", 1}, {"Spreadsheet", 1}, {"Presentation", 1}, {"Calendar", 1}, {"ContactManagement", 1}, {"Viewer", 1},
		{"System", 4}, {"Monitor", 4}, {"TerminalEmulator", 4}, {"FileManager", 4}, {"Security", 4}, {"Emulator", 4},
	};
	char b[300]; snprintf(b, sizeof b, "%s", cats);
	int best = -1, bp = 99;
	for (char *t = strtok(b, ";"); t; t = strtok(NULL, ";"))
		for (unsigned i = 0; i < sizeof M / sizeof M[0]; i++)
			if (!strcmp(t, M[i].c) && (int)i < bp) { bp = i; best = M[i].s; }
	return best < 0 ? 0 : best;
}
static int appcmp(const void *a, const void *b) { return strcasecmp(((const AppEnt *)a)->name, ((const AppEnt *)b)->name); }
static void apps_dir(const char *dir, AppEnt *out, int *n, int max)
{
	DIR *d = opendir(dir); struct dirent *e;
	while (d && (e = readdir(d)) && *n < max) {
		size_t l = strlen(e->d_name);
		if (l < 9 || strcmp(e->d_name + l - 8, ".desktop")) continue;
		char p[1100]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		FILE *f = fopen(p, "r"); if (!f) continue;
		char ln[700], name[64] = "", exec[400] = "", icon[200] = "", cats[300] = ""; int in = 0, hide = 0, term = 0, app = 1;
		while (fgets(ln, sizeof ln, f)) {
			ln[strcspn(ln, "\n")] = 0;
			if (ln[0] == '[') { in = !strcmp(ln, "[Desktop Entry]"); continue; }
			if (!in) continue;
			if (!strncmp(ln, "Name=", 5) && !name[0]) snprintf(name, sizeof name, "%s", ln + 5);
			else if (!strncmp(ln, "Exec=", 5)) snprintf(exec, sizeof exec, "%s", ln + 5);
			else if (!strncmp(ln, "Icon=", 5)) snprintf(icon, sizeof icon, "%s", ln + 5);
			else if (!strncmp(ln, "Categories=", 11)) snprintf(cats, sizeof cats, "%s", ln + 11);
			else if (!strcmp(ln, "NoDisplay=true") || !strcmp(ln, "Hidden=true") || !strncmp(ln, "OnlyShowIn=", 11)) hide = 1;
			else if (!strcmp(ln, "Terminal=true")) term = 1;
			else if (!strncmp(ln, "Type=", 5) && strcmp(ln + 5, "Application")) app = 0;
		}
		fclose(f);
		if (hide || !app || !name[0] || !exec[0]) continue;
		int dup = 0; for (int i = 0; i < *n; i++) if (!strcasecmp(out[i].name, name)) dup = 1;
		if (dup) continue;
		for (char *s = exec; *s; s++) if (*s == '%' && s[1]) { if (s[1] == '%') { memmove(s, s + 1, strlen(s)); continue; } memmove(s, s + 2, strlen(s + 2) + 1); s--; }
		AppEnt *a = &out[(*n)++];
		snprintf(a->name, sizeof a->name, "%s", name); snprintf(a->icon, sizeof a->icon, "%s", icon);
		if (term) snprintf(a->exec, sizeof a->exec, "xterm -e %s", exec); else snprintf(a->exec, sizeof a->exec, "%s", exec);
		a->section = section_of(cats);
	}
	if (d) closedir(d);
}
int ui_apps_scan(AppEnt *out, int max)
{
	int n = 0; char u[600];
	snprintf(u, sizeof u, "%s/.local/share/applications", home());
	apps_dir(u, out, &n, max);
	apps_dir("/usr/local/share/applications", out, &n, max);
	apps_dir("/usr/share/applications", out, &n, max);
	apps_dir("/var/lib/flatpak/exports/share/applications", out, &n, max);
	snprintf(u, sizeof u, "%s/.local/share/flatpak/exports/share/applications", home());
	apps_dir(u, out, &n, max);
	qsort(out, n, sizeof *out, appcmp);
	return n;
}

/* ---- icon theme lookup ---- */
#include <gdk-pixbuf/gdk-pixbuf.h>
static char themes[8][64]; static int nthemes;
static void add_theme(const char *t) { if (!t || !*t || nthemes >= 8) return; for (int i = 0; i < nthemes; i++) if (!strcmp(themes[i], t)) return; snprintf(themes[nthemes++], 64, "%s", t); }
static void init_themes(void)
{
	add_theme(T.icons);
	char p[600]; snprintf(p, sizeof p, "%s/.config/gtk-3.0/settings.ini", home());
	FILE *f = fopen(p, "r"); char l[200];
	while (f && fgets(l, sizeof l, f)) if (!strncmp(l, "gtk-icon-theme-name=", 20)) { l[strcspn(l, "\n")] = 0; add_theme(l + 20); }
	if (f) fclose(f);
	add_theme("Adwaita"); add_theme("Yaru"); add_theme("breeze"); add_theme("hicolor");
}
static int exists(const char *p) { return access(p, R_OK) == 0; }
static int find_icon(const char *name, int size, char *out, size_t cap)
{
	static const int SZ[] = {0, 48, 64, 32, 96, 128, 256, 24, 22, 16, 512};
	static const char *EXT[] = {".png", ".svg", ".xpm"};
	static const char *CAT[] = {"apps", "status", "devices", "mimetypes", "actions", "places", "categories", "emblems"};
	const char *bases[2]; char b0[600]; snprintf(b0, sizeof b0, "%s/.local/share/icons", home()); bases[0] = b0; bases[1] = "/usr/share/icons";
	for (int k = 0; k < 8; k++)
		for (int t = 0; t < nthemes; t++) for (int b = 0; b < 2; b++) for (unsigned i = 0; i < sizeof SZ / sizeof SZ[0]; i++) {
			int sz = i ? SZ[i] : size;
			for (int e = 0; e < 3; e++) {
				snprintf(out, cap, "%s/%s/%dx%d/%s/%s%s", bases[b], themes[t], sz, sz, CAT[k], name, EXT[e]); if (exists(out)) return 1;
				snprintf(out, cap, "%s/%s/%s/%d/%s%s", bases[b], themes[t], CAT[k], sz, name, EXT[e]); if (exists(out)) return 1;
			}
		}
	for (int k = 0; k < 8; k++) for (int t = 0; t < nthemes; t++) for (int b = 0; b < 2; b++) {
		snprintf(out, cap, "%s/%s/scalable/%s/%s.svg", bases[b], themes[t], CAT[k], name); if (exists(out)) return 1;
		snprintf(out, cap, "%s/%s/%s/scalable/%s.svg", bases[b], themes[t], CAT[k], name); if (exists(out)) return 1;
	}
	for (int e = 0; e < 3; e++) { snprintf(out, cap, "/usr/share/pixmaps/%s%s", name, EXT[e]); if (exists(out)) return 1; }
	return 0;
}
static struct { char key[260]; cairo_surface_t *s; } icache[1024]; static int nic;
cairo_surface_t *ui_icon_load(const char *name, int size)
{
	if (!name || !*name) return NULL;
	char key[260]; snprintf(key, sizeof key, "%s@%d", name, size);
	for (int i = 0; i < nic; i++) if (!strcmp(icache[i].key, key)) return icache[i].s;
	if (!nthemes) init_themes();
	char path[900]; cairo_surface_t *surf = NULL;
	int ok = name[0] == '/' ? (snprintf(path, sizeof path, "%s", name), exists(path)) : find_icon(name, size, path, sizeof path);
	GdkPixbuf *pb = ok ? gdk_pixbuf_new_from_file_at_size(path, size, size, NULL) : NULL;
	if (pb) {
		int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), ch = gdk_pixbuf_get_n_channels(pb), rs = gdk_pixbuf_get_rowstride(pb);
		const guchar *px = gdk_pixbuf_get_pixels(pb);
		surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		unsigned char *d = cairo_image_surface_get_data(surf); int ds = cairo_image_surface_get_stride(surf);
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
			const guchar *p = px + y * rs + x * ch; unsigned a = ch == 4 ? p[3] : 255;
			unsigned *o = (unsigned *)(d + y * ds + x * 4);
			*o = a << 24 | (p[0] * a / 255) << 16 | (p[1] * a / 255) << 8 | (p[2] * a / 255);
		}
		cairo_surface_mark_dirty(surf); g_object_unref(pb);
	}
	if (nic < 1024) { snprintf(icache[nic].key, sizeof icache[nic].key, "%s", key); icache[nic++].s = surf; }
	return surf;
}

const OwnApp UI_OWN[] = {
	{"AEdit", IC_NOTE, "aedit", 0}, {"AView", IC_VIEW, "aview", 0}, {"Calculator", IC_CALC, "calculator", 0},
	{"CharMap", IC_CHARMAP, "charmap", 0}, {"Files", IC_FOLDER, "files \"$HOME\"", 0}, {"Launch", IC_RUN, "launch", 0},
	{"About TSWM", IC_ABOUT, "about", 0},
	{"Appearance", IC_APPEAR, "preferences appearance", 3}, {"DateTime", IC_CLOCK, "preferences datetime", 3},
	{"Desktop", IC_DESKTOP, "preferences desktop", 3}, {"Font", IC_FONT, "preferences font", 3},
	{"Keyboard", IC_KEYBOARD, "preferences keyboard", 3}, {"Mouse", IC_MOUSE, "preferences mouse", 3}, {"Screen", IC_SCREEN, "preferences screen", 3}, {"Power", IC_POWER, "preferences power", 3},
	{"CPU Monitor", IC_MON, "cpumonitor", 4}, {"Memory Monitor", IC_MEM, "memmonitor", 4}, {"System Information", IC_ABOUT, "sysinfo", 4},
	{"Disk Manager", IC_DISK, "diskmanager", 4}, {"Terminal", IC_TERM, UI_TERM, 4},
};
const int UI_NOWN = sizeof UI_OWN / sizeof UI_OWN[0];

cairo_surface_t *ui_image_load(const char *path)
{
	GdkPixbuf *pb = gdk_pixbuf_new_from_file(path, NULL);
	if (!pb) return NULL;
	int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), ch = gdk_pixbuf_get_n_channels(pb), rs = gdk_pixbuf_get_rowstride(pb);
	const guchar *px = gdk_pixbuf_get_pixels(pb);
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	unsigned char *d = cairo_image_surface_get_data(surf); int ds = cairo_image_surface_get_stride(surf);
	for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
		const guchar *p = px + y * rs + x * ch; unsigned a = ch == 4 ? p[3] : 255;
		*(unsigned *)(d + y * ds + x * 4) = a << 24 | (p[0] * a / 255) << 16 | (p[1] * a / 255) << 8 | (p[2] * a / 255);
	}
	cairo_surface_mark_dirty(surf); g_object_unref(pb);
	return surf;
}

int ui_file_icon(const char *name, int dir)
{
	if (dir) return IC_FOLDER;
	const char *e = strrchr(name, '.'); if (!e || e == name) return IC_FILE;
	static const char *img[] = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".svg", ".tif", ".tiff", ".ico", NULL};
	static const char *aud[] = {".mp3", ".ogg", ".flac", ".wav", ".m4a", ".opus", ".mp4", ".mkv", ".webm", ".avi", ".mov", NULL};
	static const char *arc[] = {".zip", ".tar", ".gz", ".xz", ".bz2", ".zst", ".7z", ".rar", ".iso", ".deb", ".pkg", NULL};
	static const char *scr[] = {".sh", ".py", ".lua", ".pl", ".rb", ".js", NULL};
	for (int i = 0; img[i]; i++) if (!strcasecmp(e, img[i])) return IC_IMAGE;
	for (int i = 0; aud[i]; i++) if (!strcasecmp(e, aud[i])) return IC_AUDIO;
	for (int i = 0; arc[i]; i++) if (!strcasecmp(e, arc[i])) return IC_ARCHIVE;
	for (int i = 0; scr[i]; i++) if (!strcasecmp(e, scr[i])) return IC_SCRIPT;
	if (!strcasecmp(e, ".pdf")) return IC_PDF;
	return IC_FILE;
}
