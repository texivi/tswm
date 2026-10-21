#define _GNU_SOURCE
#include "sylui.h"
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/extensions/Xrandr.h>

enum { V_MAIN, V_APPEAR, V_DESK, V_TIME, V_KEY, V_FONT, V_MOUSE, V_SCREEN, V_POWER };
enum { B_APPLY = 1, B_DEFAULT, B_SAVE, B_DEL, B_UNDO };
typedef struct { int x, y, w, h, id; const char *label; } Btn;
static int view, pb = -1, gsel = -1;

/* appearance */
static int dd, elem, slider = -1, namefocus;
static char tname[64] = "Syllable";
static char schemes[32][64]; static int nschemes;
/* desktop */
static char walls[64][256]; static int nwalls, wsel, wscroll;
static cairo_surface_t *prev; static char prevkey[300];
/* keyboard */
static const struct { const char *name, *code; } LAY[] = {
	{"English (US)", "us"}, {"English (UK)", "gb"}, {"German", "de"}, {"French", "fr"}, {"Spanish", "es"}, {"Italian", "it"},
	{"Portuguese", "pt"}, {"Dutch", "nl"}, {"Polish", "pl"}, {"Czech", "cz"}, {"Hungarian", "hu"}, {"Romanian", "ro"},
	{"Swedish", "se"}, {"Norwegian", "no"}, {"Danish", "dk"}, {"Finnish", "fi"}, {"Estonian", "ee"}, {"Greek", "gr"},
	{"Turkish", "tr"}, {"Russian", "ru"}, {"Ukrainian", "ua"}, {"Japanese", "jp"}};
#define NLAY ((int)(sizeof LAY / sizeof LAY[0]))
static int lsel, lscroll, lorig; static char ktest[256];
/* font */
static char fonts[256][64]; static int nfonts, fsel, fscroll;
/* screen */
static XRRScreenSize *sizes; static int nsizes, ssel, scur, sscroll;

#define edit ui_edit
static void scan_schemes(void)
{
	char d[600]; snprintf(d, sizeof d, "%s/schemes", ui_cfgdir()); nschemes = 0;
	DIR *dir = opendir(d); struct dirent *e;
	while (dir && (e = readdir(dir)) && nschemes < 32) {
		size_t l = strlen(e->d_name);
		if (l > 5 && !strcmp(e->d_name + l - 5, ".conf") && l - 5 < 64) { snprintf(schemes[nschemes], 64, "%.*s", (int)(l - 5), e->d_name); nschemes++; }
	}
	if (dir) closedir(dir);
}
static void scheme_path(char *b, const char *n) { snprintf(b, 700, "%s/schemes/%s.conf", ui_cfgdir(), n); }
static int safe_name(const char *n)
{
	if (!*n) return 0;
	for (; *n; n++) if (!(isalnum((unsigned char)*n) || *n == ' ' || *n == '-' || *n == '_')) return 0;
	return 1;
}
static const char *base(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }
static void load_preview(const char *path)
{
	if (!strcmp(path, prevkey)) return;
	snprintf(prevkey, sizeof prevkey, "%s", path);
	if (prev) { cairo_surface_destroy(prev); prev = NULL; }
	if (!*path) return;
	cairo_surface_t *im = ui_image_load(path);
	if (!im) return;
	prev = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 224, 140);
	cairo_t *c = cairo_create(prev);
	double iw = cairo_image_surface_get_width(im), ih = cairo_image_surface_get_height(im), k = fmax(224 / iw, 140 / ih);
	cairo_translate(c, (224 - iw * k) / 2, (140 - ih * k) / 2); cairo_scale(c, k, k);
	cairo_set_source_surface(c, im, 0, 0); cairo_pattern_set_filter(cairo_get_source(c), CAIRO_FILTER_BEST); cairo_paint(c);
	cairo_destroy(c); cairo_surface_destroy(im);
}
static void wall_select(int i)
{
	wsel = i;
	if (i == 0) { snprintf(T.wall, sizeof T.wall, "none"); load_preview(""); }
	else { snprintf(T.wall, sizeof T.wall, "%s", walls[i - 1]); load_preview(walls[i - 1]); }
}
static void init_view(App *a)
{
	pb = -1; dd = 0; gsel = -1;
	if (view == V_APPEAR) { scan_schemes(); }
	if (view == V_DESK) {
		nwalls = ui_wall_list(walls, 64); wsel = 0;
		const char *cur = ui_wall_path(T.wall);
		if (cur) for (int i = 0; i < nwalls; i++) if (!strcmp(walls[i], cur)) wsel = i + 1;
		if (wsel > 0) load_preview(walls[wsel - 1]);
		wscroll = wsel > 5 ? wsel - 5 : 0; if (wscroll > nwalls + 1 - 11) wscroll = nwalls + 1 - 11; if (wscroll < 0) wscroll = 0;
	}
	if (view == V_FONT) {
		FILE *f = popen("fc-list : family 2>/dev/null | sed 's/,.*//' | sort -u", "r"); char l[128]; nfonts = 0; snprintf(fonts[nfonts++], 64, "DejaVu Sans");
		while (f && fgets(l, sizeof l, f) && nfonts < 256) { l[strcspn(l, "\n")] = 0; if (l[0]) snprintf(fonts[nfonts++], 64, "%s", l); }
		if (f) pclose(f);
		for (int i = 0; i < nfonts; i++) if (!strcmp(fonts[i], T.font)) fsel = i;
		fscroll = fsel > 3 ? fsel - 3 : 0;
	}
	if (view == V_MOUSE && T.mouse_speed < 1) T.mouse_speed = 5;
	if (view == V_SCREEN) {
		XRRScreenConfiguration *sc = XRRGetScreenInfo(a->d, DefaultRootWindow(a->d)); Rotation r;
		sizes = XRRConfigSizes(sc, &nsizes); scur = XRRConfigCurrentConfiguration(sc, &r); ssel = scur;
	}
	if (view == V_KEY) {
		FILE *f = popen("setxkbmap -query 2>/dev/null", "r"); char l[200];
		while (f && fgets(l, sizeof l, f)) if (!strncmp(l, "layout:", 7)) {
			char *p = l + 7; while (*p == ' ') p++; p[strcspn(p, ",\n")] = 0;
			for (int i = 0; i < NLAY; i++) if (!strcmp(LAY[i].code, p)) lsel = i;
		}
		if (f) pclose(f);
		lorig = lsel;
	}
}

/* ---------- buttons ---------- */
static int btns(App *a, Btn *b)
{
	int W = a->W, H = a->H, n = 0;
	switch (view) {
	case V_APPEAR:
		b[n++] = (Btn){24, 272, 140, 24, B_SAVE, "Save Scheme"}; b[n++] = (Btn){176, 272, 140, 24, B_DEL, "Delete Scheme"};
		b[n++] = (Btn){W - 168, H - 34, 76, 24, B_APPLY, "Apply"}; b[n++] = (Btn){W - 84, H - 34, 76, 24, B_DEFAULT, "Default"}; break;
	case V_DESK: case V_TIME: case V_FONT: case V_MOUSE: case V_SCREEN: case V_POWER:
		b[n++] = (Btn){W - 168, H - 34, 76, 24, B_APPLY, "Apply"}; b[n++] = (Btn){W - 84, H - 34, 76, 24, B_DEFAULT, "Default"}; break;
	case V_KEY:
		b[n++] = (Btn){W - 252, H - 34, 76, 24, B_APPLY, "Apply"}; b[n++] = (Btn){W - 168, H - 34, 76, 24, B_UNDO, "Undo"}; b[n++] = (Btn){W - 84, H - 34, 76, 24, B_DEFAULT, "Default"}; break;
	}
	return n;
}
static int dirtyp;
static void act(App *a, int id)
{
	switch (id) {
	case B_APPLY:
		if (view == V_SCREEN) { XRRScreenConfiguration *sc = XRRGetScreenInfo(a->d, DefaultRootWindow(a->d)); Rotation r; XRRConfigCurrentConfiguration(sc, &r); XRRSetScreenConfig(a->d, sc, DefaultRootWindow(a->d), ssel, r, CurrentTime); XRRFreeScreenConfigInfo(sc); scur = ssel; }
		else if (view == V_KEY) { char c[64]; snprintf(c, sizeof c, "setxkbmap %s", LAY[lsel].code); ui_spawn(c); }
		else { theme_save(); unlink(ui_preview_path()); dirtyp = 0; ui_notify_theme(a->d); }
		break;
	case B_DEFAULT:
		if (view == V_APPEAR) { theme_preset(&T, 0); T.decor = 0; snprintf(tname, sizeof tname, "%s", PRESETS[0].name); }
		else if (view == V_DESK) { T.wall[0] = 0; init_view(a); }
		else if (view == V_TIME) { T.clock24 = 1; T.clock_secs = 1; }
		else if (view == V_FONT) { snprintf(T.font, sizeof T.font, "DejaVu Sans"); T.fsize = 85; init_view(a); }
		else if (view == V_MOUSE) { T.mouse_speed = 5; T.mouse_left = 0; }
		else if (view == V_POWER) { T.lock_min = 10; T.dpms_min = 15; }
		else if (view == V_SCREEN) ssel = scur;
		else if (view == V_KEY) { for (int i = 0; i < NLAY; i++) if (!strcmp(LAY[i].code, "us")) lsel = i; }
		break;
	case B_UNDO: lsel = lorig; break;
	case B_SAVE: {
		if (!safe_name(tname)) break;
		char p[700]; scheme_path(p, tname); theme_save_to(&T, p, 1); scan_schemes(); break;
	}
	case B_DEL: {
		char p[700]; scheme_path(p, tname); if (safe_name(tname)) unlink(p); scan_schemes(); break;
	}
	}
}

/* ---------- drawing ---------- */
static const struct { const char *n; int ic, v; } MAINI[] = {{"Appearance", IC_APPEAR, V_APPEAR}, {"DateTime", IC_CLOCK, V_TIME}, {"Desktop", IC_DESKTOP, V_DESK}, {"Font", IC_FONT, V_FONT}, {"Keyboard", IC_KEYBOARD, V_KEY}, {"Mouse", IC_MOUSE, V_MOUSE}, {"Screen", IC_SCREEN, V_SCREEN}, {"Power", IC_POWER, V_POWER}};
#define NMAIN 8
static int ddn(void) { return dd == 1 ? 3 : dd == 2 ? NPRESETS + nschemes : dd == 3 ? NCOL : 0; }
static const char *ddi(int i)
{
	if (dd == 1) return DECORS[i];
	if (dd == 2) return i < NPRESETS ? PRESETS[i].name : schemes[i - NPRESETS];
	return COLNAMES[i];
}
static void ddrect(int *x, int *y, int *w)
{
	*x = 70; *w = 250; *y = dd == 1 ? 50 : dd == 2 ? 108 : 146;
}
static void draw(App *a)
{
	cairo_t *c = a->cr; int W = a->W, H = a->H; Col fg = col_fg(FACE);
	if (view == V_MAIN) {
		ui_rect(c, 0, 0, W, H, BGC);
		for (int i = 0; i < NMAIN; i++) {
			int x = 14 + (i % 4) * 82, y = 14 + (i / 4) * 84;
			if (i == gsel) { cairo_set_source_rgba(c, T.col[C_SEL].r, T.col[C_SEL].g, T.col[C_SEL].b, .9); cairo_rectangle(c, x, y, 78, 76); cairo_fill(c); }
			ui_icon(c, MAINI[i].ic, x + 15, y + 6, 48);
			ui_text_c(c, x + 39, y + 64, MAINI[i].n, i == gsel ? col_fg(T.col[C_SEL]) : col_fg(BGC), 0);
		}
	} else if (view == V_APPEAR) {
		ui_group(c, 8, 6, W - 16, 52, "Window Decoration");
		ui_text(c, 20, 39, "Decor", fg, 0); ui_dropdown(c, 70, 28, 250, 22, DECORS[T.decor]);
		ui_group(c, 8, 66, W - 16, 240, "Colour Scheme");
		ui_text(c, 20, 97, "Theme", fg, 0); ui_dropdown(c, 70, 86, 250, 22, tname);
		if (namefocus) ui_rect(c, 72 + ui_tw(c, tname, 0) + 3, 90, 1, 14, HEX(0));
		ui_bevel(c, 16, 116, W - 32, 148, 2);
		ui_dropdown(c, 24, 124, 170, 22, COLNAMES[elem]);
		Col *p = &T.col[elem]; double v[3] = {p->r, p->g, p->b}; const char *nm[3] = {"Red", "Green", "Blue"};
		ui_text_c(c, 205, 158, "Dec", fg, 0); ui_text_c(c, 242, 158, "Hex", fg, 0);
		for (int i = 0; i < 3; i++) {
			int y = 170 + i * 26, iv = (int)(v[i] * 255 + .5); char s[8];
			ui_text(c, 26, y + 6, nm[i], fg, 0); ui_slider(c, 62, y, 120, iv);
			snprintf(s, 8, "%d", iv); ui_field(c, 190, y - 4, 30, 20, s, 0);
			snprintf(s, 8, "%02X", iv); ui_field(c, 226, y - 4, 32, 20, s, 0);
		}
		ui_rect(c, 272, 170, 44, 64, *p); ui_bevel(c, 270, 168, 48, 68, 1); ui_rect(c, 272, 170, 44, 64, *p);
	} else if (view == V_DESK) {
		ui_group(c, 8, 6, W - 16, H - 52, "Wallpaper");
		int n = nwalls + 1, rows = 11;
		ui_rect(c, 18, 26, 170, 220, BGC); ui_bevel(c, 18, 26, 170, 220, 1);
		cairo_save(c); cairo_rectangle(c, 20, 28, 150, 216); cairo_clip(c);
		for (int i = 0; i < rows && i + wscroll < n; i++) {
			int k = i + wscroll; Col tc = col_fg(BGC);
			if (k == wsel) { ui_rect(c, 20, 28 + i * 20, 150, 20, T.col[C_SEL]); tc = col_fg(T.col[C_SEL]); }
			char b[260]; if (k == 0) snprintf(b, sizeof b, "(None)"); else { snprintf(b, sizeof b, "%s", base(walls[k - 1])); b[strcspn(b, ".")] = 0; }
			ui_text_fit(c, 24, 38 + i * 20, b, 144, tc, 0);
		}
		cairo_restore(c);
		ui_vscroll(c, 172, 28, 16, 216, wscroll, rows, n);
		ui_rect(c, 200, 26, 224, 140, T.col[C_DESK]); ui_bevel(c, 199, 25, 226, 142, 1);
		if (prev) { cairo_set_source_surface(c, prev, 200, 26); cairo_rectangle(c, 200, 26, 224, 140); cairo_fill(c); }
		ui_check(c, 200, 206, "Show TSWM logo", T.watermark);
		{ char nm[260] = "None"; if (wsel > 0) { snprintf(nm, sizeof nm, "%s", base(walls[wsel - 1])); nm[strcspn(nm, ".")] = 0; } ui_text_fit(c, 200, 182, nm, 224, fg, 1); }
	} else if (view == V_TIME) {
		ui_group(c, 8, 6, W - 16, H - 52, "Clock");
		ui_check(c, 24, 32, "24-hour clock", T.clock24); ui_check(c, 24, 58, "Show seconds", T.clock_secs);
		time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); char b[32];
		strftime(b, 32, T.clock24 ? (T.clock_secs ? "%H:%M:%S" : "%H:%M") : (T.clock_secs ? "%I:%M:%S %p" : "%I:%M %p"), &tm);
		ui_text(c, 24, 98, "Preview", fg, 0); ui_field(c, 80, 88, 120, 22, b, 0);
	} else if (view == V_KEY) {
		ui_group(c, 8, 6, W - 16, 186, "Keyboard Layout");
		int rows = 7;
		ui_rect(c, 16, 26, W - 32, 158, BGC); ui_bevel(c, 16, 26, W - 32, 158, 1);
		cairo_save(c); cairo_rectangle(c, 18, 28, W - 54, 154); cairo_clip(c);
		for (int i = 0; i < rows + 1 && i + lscroll < NLAY; i++) {
			int k = i + lscroll; Col tc = col_fg(BGC);
			if (k == lsel) { ui_rect(c, 18, 28 + i * 22, W - 54, 22, T.col[C_SEL]); tc = col_fg(T.col[C_SEL]); }
			ui_text(c, 24, 39 + i * 22, LAY[k].name, tc, 0);
		}
		cairo_restore(c);
		ui_vscroll(c, W - 34, 28, 16, 154, lscroll, rows, NLAY);
		ui_group(c, 8, 198, W - 16, 58, "Test Area (Apply changes first)");
		ui_field(c, 16, 222, W - 32, 22, ktest, 1);
	}
	if (view == V_FONT) {
		ui_group(c, 8, 6, W - 16, 196, "Font");
		int rows = 7;
		ui_rect(c, 16, 26, W - 32, 158, BGC); ui_bevel(c, 16, 26, W - 32, 158, 1);
		cairo_save(c); cairo_rectangle(c, 18, 28, W - 54, 154); cairo_clip(c);
		for (int i = 0; i < rows && i + fscroll < nfonts; i++) {
			int k = i + fscroll; Col tc = col_fg(BGC);
			if (k == fsel) { ui_rect(c, 18, 28 + i * 22, W - 54, 22, T.col[C_SEL]); tc = col_fg(T.col[C_SEL]); }
			ui_text_fit(c, 24, 39 + i * 22, fonts[k], W - 66, tc, 0);
		}
		cairo_restore(c);
		ui_vscroll(c, W - 34, 28, 16, 154, fscroll, rows, nfonts);
		char sz[16]; snprintf(sz, sizeof sz, "%.2f", T.fsize / 10.0);
		ui_text(c, 16, 192, "Size", fg, 0); ui_button(c, 56, 183, 22, 20, "-", pb == 10 ? 1 : 0); ui_field(c, 82, 183, 36, 20, sz, 0); ui_button(c, 122, 183, 22, 20, "+", pb == 11 ? 1 : 0);
		ui_group(c, 8, 212, W - 16, 66, "Preview");
		ui_text(c, 20, 240, "The quick brown fox jumps over the lazy dog", col_fg(FACE), 0);
		ui_text(c, 20, 260, "0123456789 ABCDEFGHIJKLM abcdefghijklm", col_fg(FACE), 1);
	} else if (view == V_MOUSE) {
		ui_group(c, 8, 6, W - 16, H - 52, "Pointer");
		ui_text(c, 20, 40, "Speed", fg, 0); ui_text(c, 70, 58, "Slow", fg, 0); ui_text(c, W - 56, 58, "Fast", fg, 0);
		ui_slider(c, 70, 34, W - 110, (T.mouse_speed - 1) * 255 / 9);
		ui_check(c, 20, 86, "Left-handed buttons", T.mouse_left);
	} else if (view == V_SCREEN) {
		ui_group(c, 8, 6, W - 16, H - 52, "Display");
		ui_rect(c, 16, 26, W - 32, H - 90, BGC); ui_bevel(c, 16, 26, W - 32, H - 90, 1);
		for (int i = 0; i < nsizes && 28 + (i - sscroll) * 22 < H - 66; i++) {
			if (i < sscroll) continue; int y = 28 + (i - sscroll) * 22; Col tc = col_fg(BGC); char b[64];
			if (i == ssel) { ui_rect(c, 18, y, W - 36, 22, T.col[C_SEL]); tc = col_fg(T.col[C_SEL]); }
			snprintf(b, sizeof b, "%d x %d%s", sizes[i].width, sizes[i].height, i == scur ? "   (current)" : ""); ui_text(c, 24, y + 11, b, tc, 0);
		}
	}
	if (view == V_POWER) {
		ui_group(c, 8, 6, W - 16, H - 52, "Screen");
		static const int LK[] = {0, 1, 2, 5, 10, 15, 30}, DP[] = {0, 5, 10, 15, 30, 60};
		char s[48];
		ui_text(c, 20, 40, "Lock the screen after", fg, 0);
		if (T.lock_min) snprintf(s, sizeof s, "%d minutes", T.lock_min); else snprintf(s, sizeof s, "never");
		ui_button(c, 170, 29, 22, 22, "-", pb == 20 ? 1 : 0); ui_field(c, 196, 29, 90, 22, s, 0); ui_button(c, 290, 29, 22, 22, "+", pb == 21 ? 1 : 0);
		ui_text(c, 20, 76, "Turn the screen off after", fg, 0);
		if (T.dpms_min) snprintf(s, sizeof s, "%d minutes", T.dpms_min); else snprintf(s, sizeof s, "never");
		ui_button(c, 170, 65, 22, 22, "-", pb == 22 ? 1 : 0); ui_field(c, 196, 65, 90, 22, s, 0); ui_button(c, 290, 65, 22, 22, "+", pb == 23 ? 1 : 0);
		(void)LK; (void)DP;
	}
	Btn b[6]; int n = btns(a, b);
	for (int i = 0; i < n; i++) ui_button(c, b[i].x, b[i].y, b[i].w, b[i].h, b[i].label, pb == b[i].id ? 1 : (b[i].id == B_APPLY ? 2 : 0));
	if (view == V_APPEAR && dd) {
		int x, y, w; ddrect(&x, &y, &w); int cnt = ddn();
		ui_rect(c, x, y, w, cnt * 20 + 2, BGC); ui_bevel(c, x, y, w, cnt * 20 + 2, 1);
		for (int i = 0; i < cnt; i++) {
			Col tc = col_fg(BGC); int on = (dd == 1 && i == T.decor) || (dd == 3 && i == elem);
			if (on) { ui_rect(c, x + 2, y + 1 + i * 20, w - 4, 20, T.col[C_SEL]); tc = col_fg(T.col[C_SEL]); }
			ui_text(c, x + 8, y + 11 + i * 20, ddi(i), tc, 0);
		}
	}
}

/* ---------- input ---------- */
static void setslider(int i, int mx)
{
	double v = (mx - 62 - 5) / 110.0; v = v < 0 ? 0 : v > 1 ? 1 : v;
	double *ch[3] = {&T.col[elem].r, &T.col[elem].g, &T.col[elem].b}; *ch[i] = (int)(v * 255 + .5) / 255.0;
	theme_derive(&T);
}
static int in(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }
static void press_(App *a, int x, int y, int b, unsigned long t)
{
	int n = 0;
	if (b == 4 || b == 5) {
		int d = b == 4 ? -1 : 1;
		if (view == V_DESK) { wscroll += d * 2; if (wscroll > nwalls + 1 - 11) wscroll = nwalls + 1 - 11; if (wscroll < 0) wscroll = 0; }
		if (view == V_KEY) { lscroll += d * 2; if (lscroll > NLAY - 7) lscroll = NLAY - 7; if (lscroll < 0) lscroll = 0; }
		return;
	}
	if (b != 1) return;
	if (view == V_APPEAR && dd) {
		int rx, ry, rw; ddrect(&rx, &ry, &rw);
		if (in(x, y, rx, ry, rw, ddn() * 20 + 2)) {
			int i = (y - ry - 1) / 20;
			if (dd == 1) T.decor = i;
			else if (dd == 3) elem = i;
			else if (i < NPRESETS) { theme_preset(&T, i); snprintf(tname, sizeof tname, "%s", PRESETS[i].name); }
			else { char p[700]; scheme_path(p, schemes[i - NPRESETS]); theme_load_from(&T, p); snprintf(tname, sizeof tname, "%s", schemes[i - NPRESETS]); }
		}
		dd = 0; return;
	}
	Btn bt[6]; n = btns(a, bt);
	for (int i = 0; i < n; i++) if (in(x, y, bt[i].x, bt[i].y, bt[i].w, bt[i].h)) { pb = bt[i].id; return; }
	switch (view) {
	case V_MAIN:
		gsel = -1;
		for (int i = 0; i < NMAIN; i++) if (in(x, y, 14 + (i % 4) * 82, 14 + (i / 4) * 84, 78, 76)) {
			gsel = i;
			if (app_isdbl(a, t, i)) { char cmd[64]; static const char *id[NMAIN] = {"appearance", "datetime", "desktop", "font", "keyboard", "mouse", "screen", "power"}; snprintf(cmd, sizeof cmd, "preferences %s", id[i]); ui_spawn(cmd); }
		}
		break;
	case V_APPEAR:
		namefocus = 0;
		if (in(x, y, 70, 28, 250, 22)) dd = 1;
		else if (in(x, y, 303, 86, 17, 22)) dd = 2;
		else if (in(x, y, 70, 86, 233, 22)) namefocus = 1;
		else if (in(x, y, 24, 124, 170, 22)) dd = 3;
		else for (int i = 0; i < 3; i++) if (in(x, y, 56, 166 + i * 26, 132, 20)) { slider = i; setslider(i, x); }
		break;
	case V_DESK: {
		if (in(x, y, 200, 206, 160, 14)) { T.watermark = !T.watermark; break; }
		if (in(x, y, 20, 28, 150, 216)) { int i = (y - 28) / 20 + wscroll; if (i <= nwalls) wall_select(i); }
		else if (in(x, y, 172, 28, 16, 16)) { if (wscroll > 0) wscroll--; }
		else if (in(x, y, 172, 228, 16, 16)) { if (wscroll < nwalls + 1 - 11) wscroll++; }
		else if (in(x, y, 172, 44, 16, 184)) { wscroll += y < 136 ? -11 : 11; if (wscroll > nwalls + 1 - 11) wscroll = nwalls + 1 - 11; if (wscroll < 0) wscroll = 0; }
		break;
	}
	case V_TIME:
		if (in(x, y, 24, 32, 140, 14)) T.clock24 = !T.clock24;
		if (in(x, y, 24, 58, 140, 14)) T.clock_secs = !T.clock_secs;
		break;
	case V_FONT:
		if (in(x, y, 18, 28, a->W - 54, 154)) { int i = (y - 28) / 22 + fscroll; if (i < nfonts) { fsel = i; snprintf(T.font, sizeof T.font, "%s", fonts[i]); } }
		else if (in(x, y, a->W - 34, 28, 16, 16)) { if (fscroll > 0) fscroll--; }
		else if (in(x, y, a->W - 34, 166, 16, 16)) { if (fscroll < nfonts - 7) fscroll++; }
		else if (in(x, y, 56, 183, 22, 20)) { if (T.fsize > 60) T.fsize -= 5; }
		else if (in(x, y, 122, 183, 22, 20)) { if (T.fsize < 140) T.fsize += 5; }
		break;
	case V_POWER: {
		static const int LK[] = {0, 1, 2, 5, 10, 15, 30}, DP[] = {0, 5, 10, 15, 30, 60};
		int li = 0, di = 0; for (int i = 0; i < 7; i++) if (LK[i] == T.lock_min) li = i; for (int i = 0; i < 6; i++) if (DP[i] == T.dpms_min) di = i;
		if (in(x, y, 170, 29, 22, 22) && li > 0) T.lock_min = LK[li - 1]; else if (in(x, y, 290, 29, 22, 22) && li < 6) T.lock_min = LK[li + 1];
		else if (in(x, y, 170, 65, 22, 22) && di > 0) T.dpms_min = DP[di - 1]; else if (in(x, y, 290, 65, 22, 22) && di < 5) T.dpms_min = DP[di + 1];
		break;
	}
	case V_MOUSE:
		if (in(x, y, 20, 86, 180, 14)) T.mouse_left = !T.mouse_left;
		else if (in(x, y, 64, 28, a->W - 98, 24)) { slider = 9; T.mouse_speed = 1 + (int)((x - 70 - 5) * 9 / (double)(a->W - 120) + .5); if (T.mouse_speed < 1) T.mouse_speed = 1; if (T.mouse_speed > 10) T.mouse_speed = 10; }
		break;
	case V_SCREEN:
		for (int i = sscroll; i < nsizes; i++) { int yy = 28 + (i - sscroll) * 22; if (in(x, y, 18, yy, a->W - 36, 22)) ssel = i; }
		break;
	case V_KEY:
		if (in(x, y, 18, 28, a->W - 54, 154)) { int i = (y - 28) / 22 + lscroll; if (i < NLAY) lsel = i; }
		else if (in(x, y, a->W - 34, 28, 16, 16)) { if (lscroll > 0) lscroll--; }
		else if (in(x, y, a->W - 34, 166, 16, 16)) { if (lscroll < NLAY - 7) lscroll++; }
		break;
	}
}
static void motion_(App *a, int x, int y, unsigned st)
{
	if (slider == 9) { T.mouse_speed = 1 + (int)((x - 70 - 5) * 9 / (double)(a->W - 120) + .5); if (T.mouse_speed < 1) T.mouse_speed = 1; if (T.mouse_speed > 10) T.mouse_speed = 10; }
	else if (slider >= 0) setslider(slider, x);
}
static void release_(App *a, int x, int y, int b)
{
	slider = -1;
	Btn bt[6]; int n = btns(a, bt);
	for (int i = 0; i < n; i++) if (bt[i].id == pb && in(x, y, bt[i].x, bt[i].y, bt[i].w, bt[i].h)) act(a, pb);
	pb = -1;
}
static void key_(App *a, KeySym k, const char *t, int n, unsigned s)
{
	if (view == V_APPEAR && namefocus) edit(tname, sizeof tname, k, t, n);
	else if (view == V_KEY) edit(ktest, sizeof ktest, k, t, n);
	else if (k == 0xff1b) a->quit = 1;
}
static Theme lastT; static int pending;
static void write_preview(App *a)
{
	theme_save_to(&T, ui_preview_path(), 0); ui_notify_theme(a->d); dirtyp = 1; pending = 0;
}
static void live(App *a)
{
	if (view == V_MAIN || view == V_KEY || view == V_SCREEN) return;
	if (!theme_equal(&T, &lastT)) { lastT = T; write_preview(a); }
}
static void tickf(App *a) { if (pending) write_preview(a); }
static void press(App *a, int x, int y, int b, unsigned long t) { press_(a, x, y, b, t); live(a); }
static void motion(App *a, int x, int y, unsigned st) { motion_(a, x, y, st); live(a); }
static void release(App *a, int x, int y, int b) { release_(a, x, y, b); live(a); }
static void key(App *a, KeySym k, const char *t, int n, unsigned s) { key_(a, k, t, n, s); live(a); }
int main(int argc, char **argv)
{
	const char *v = argc > 1 ? argv[1] : "";
	const char *title = "/Applications/Preferences"; int w = 350, h = 196;
	if (!strcmp(v, "appearance")) { view = V_APPEAR; title = "Appearance"; w = 340; h = 350; }
	else if (!strcmp(v, "desktop")) { view = V_DESK; title = "Desktop"; w = 440; h = 300; }
	else if (!strcmp(v, "datetime")) { view = V_TIME; title = "DateTime"; w = 300; h = 190; }
	else if (!strcmp(v, "keyboard")) { view = V_KEY; title = "Keyboard"; w = 360; h = 330; }
	else if (!strcmp(v, "font")) { view = V_FONT; title = "Font"; w = 360; h = 330; }
	else if (!strcmp(v, "mouse")) { view = V_MOUSE; title = "Mouse"; w = 320; h = 170; }
	else if (!strcmp(v, "screen")) { view = V_SCREEN; title = "Screen"; w = 320; h = 260; }
	else if (!strcmp(v, "power")) { view = V_POWER; title = "Power"; w = 340; h = 150; }
	App *a = app_new(title, "preferences", w, h, view != V_MAIN);
	a->draw = draw; a->press = press; a->release = release; a->motion = motion; a->key = key; a->tick = tickf; a->tick_ms = 60;
	init_view(a);
	lastT = T;
	app_run(a);
	if (dirtyp) { unlink(ui_preview_path()); ui_notify_theme(a->d); } /* revert */
	return 0;
}
