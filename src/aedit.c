#define _GNU_SOURCE
#include "sylui.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXL 100000
static char **ln; static int nl, cy, cx, top, modified, prompt, pb = -1;
static char path[PATH_MAX], pbuf[PATH_MAX], status[120];
enum { BAR = 30, SB = 16, LH = 16 };
static double CWID;

static void setline(int i, const char *s) { free(ln[i]); ln[i] = strdup(s); }
static void insline(int i, const char *s)
{
	if (nl >= MAXL) return;
	memmove(ln + i + 1, ln + i, (nl - i) * sizeof *ln); ln[i] = strdup(s); nl++;
}
static void delline(int i) { free(ln[i]); memmove(ln + i, ln + i + 1, (nl - i - 1) * sizeof *ln); nl--; if (!nl) { ln[0] = strdup(""); nl = 1; } }
static void load(const char *p)
{
	FILE *f = fopen(p, "r"); if (!f) return;
	char *buf = malloc(4 << 20); size_t n = fread(buf, 1, (4 << 20) - 1, f); fclose(f); buf[n] = 0;
	for (int i = 0; i < nl; i++) free(ln[i]);
	nl = 0; char *s = buf;
	while (nl < MAXL) {
		char *e = strchr(s, '\n'); if (e) *e = 0;
		size_t l = strlen(s); if (l && s[l - 1] == '\r') s[l - 1] = 0;
		ln[nl++] = strdup(s);
		if (!e) break; s = e + 1;
	}
	free(buf);
}
static int save(const char *p)
{
	FILE *f = fopen(p, "w"); if (!f) { snprintf(status, sizeof status, "Cannot write %s", p); return -1; }
	for (int i = 0; i < nl; i++) { fputs(ln[i], f); if (i + 1 < nl) fputc('\n', f); }
	if (nl > 1 || ln[0][0]) fputc('\n', f);
	fclose(f); modified = 0; snprintf(status, sizeof status, "Saved."); return 0;
}
static void title(App *a)
{
	char t[PATH_MAX + 40]; snprintf(t, sizeof t, "%s%s - Text Editor", modified ? "*" : "", path[0] ? path : "Untitled");
	app_title(a, t);
}
static int rows(App *a) { return (a->H - BAR) / LH; }
static void scrollto(App *a)
{
	if (cy < top) top = cy;
	if (cy >= top + rows(a)) top = cy - rows(a) + 1;
	int m = nl - rows(a); if (m < 0) m = 0; if (top > m) top = m; if (top < 0) top = 0;
}
static void font(cairo_t *c) { cairo_select_font_face(c, "DejaVu Sans Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL); cairo_set_font_size(c, 12); }
static const char *BL[2] = {"Save", "Save As"};
static void draw(App *a)
{
	cairo_t *c = a->cr; int W = a->W, H = a->H; Col fg = col_fg(BGC);
	ui_rect(c, 0, BAR, W - SB, H - BAR, BGC);
	font(c); cairo_text_extents_t e; cairo_text_extents(c, "M", &e); CWID = e.x_advance;
	cairo_save(c); cairo_rectangle(c, 0, BAR, W - SB, H - BAR); cairo_clip(c); font(c);
	for (int i = top; i < nl && i < top + rows(a) + 1; i++) {
		double y = BAR + (i - top) * LH + 12; ui_set(c, fg); cairo_move_to(c, 4, y); cairo_show_text(c, ln[i]);
	}
	if (!prompt) {
		char sv = ln[cy][cx]; ln[cy][cx] = 0; cairo_text_extents_t te; cairo_text_extents(c, ln[cy], &te); ln[cy][cx] = sv;
		ui_rect(c, 4 + te.x_advance, BAR + (cy - top) * LH + 1, 1.5, LH - 2, fg);
	}
	cairo_restore(c);
	ui_vscroll(c, W - SB, BAR, SB, H - BAR, top, rows(a), nl);
	ui_rect(c, 0, 0, W, BAR, FACE); ui_rect(c, 0, BAR - 1, W, 1, T.shadow);
	if (prompt) { ui_text(c, 8, BAR / 2, "Save as:", col_fg(FACE), 0); ui_field(c, 66, 4, W - 76, 22, pbuf, 1); }
	else {
		for (int i = 0; i < 2; i++) ui_button(c, 6 + i * 70, 4, 64, 22, BL[i], pb == i ? 1 : 0);
		char s[200]; snprintf(s, sizeof s, "Ln %d, Col %d   %s", cy + 1, cx + 1, status); ui_text(c, 160, BAR / 2, s, col_fg(FACE), 0);
	}
}
static int colat(App *a, int x, int y)
{
	int i = (y - BAR) / LH + top; if (i < 0) i = 0; if (i >= nl) i = nl - 1; cy = i;
	int best = 0, l = strlen(ln[i]); double bd = 1e9;
	cairo_t *c = a->cr; font(c);
	for (int k = 0; k <= l; k++) {
		if (k < l && (ln[i][k] & 0xC0) == 0x80) continue;
		char sv = ln[i][k]; ln[i][k] = 0; cairo_text_extents_t te; cairo_text_extents(c, ln[i], &te); ln[i][k] = sv;
		double d = fabs(4 + te.x_advance - x); if (d < bd) { bd = d; best = k; }
	}
	return best;
}
static void press(App *a, int x, int y, int b, unsigned long t)
{
	if (b == 4 || b == 5) { top += b == 4 ? -3 : 3; int m = nl - rows(a); if (m < 0) m = 0; if (top > m) top = m; if (top < 0) top = 0; return; }
	if (b != 1) return;
	if (y < BAR) { if (!prompt) for (int i = 0; i < 2; i++) if (x >= 6 + i * 70 && x < 70 + i * 70 && y >= 4 && y < 26) pb = i; return; }
	if (x >= a->W - SB) { top += y < a->H / 2 ? -rows(a) : rows(a); int m = nl - rows(a); if (m < 0) m = 0; if (top > m) top = m; if (top < 0) top = 0; return; }
	cx = colat(a, x, y);
}
static void release(App *a, int x, int y, int b)
{
	if (pb >= 0 && y < BAR && x >= 6 + pb * 70 && x < 70 + pb * 70) {
		if (pb == 0 && path[0]) { save(path); title(a); }
		else { prompt = 1; snprintf(pbuf, sizeof pbuf, "%s", path[0] ? path : getenv("HOME") ? getenv("HOME") : "/"); if (!path[0]) strcat(pbuf, "/untitled.txt"); }
	}
	pb = -1;
}
static void key(App *a, KeySym k, const char *t, int n, unsigned s)
{
	if (prompt) {
		if (k == 0xff0d) { if (!save(pbuf)) { snprintf(path, sizeof path, "%s", pbuf); } prompt = 0; title(a); }
		else if (k == 0xff1b) prompt = 0;
		else ui_edit(pbuf, sizeof pbuf, k, t, n);
		return;
	}
	char *l = ln[cy]; int len = strlen(l);
	if (s & ControlMask) {
		if (k == 's' || k == 'S') { if (path[0]) { save(path); title(a); } else { prompt = 1; snprintf(pbuf, sizeof pbuf, "%s/untitled.txt", getenv("HOME") ? getenv("HOME") : "/tmp"); } }
		else if (k == 'q' || k == 'Q') a->quit = 1;
		else if (k == 'k' || k == 'K') { delline(cy); if (cy >= nl) cy = nl - 1; cx = 0; modified = 1; }
		return;
	}
	switch (k) {
	case 0xff51: if (cx > 0) { cx--; while (cx > 0 && (l[cx] & 0xC0) == 0x80) cx--; } else if (cy > 0) { cy--; cx = strlen(ln[cy]); } break;
	case 0xff53: if (cx < len) { cx++; while (l[cx] && (l[cx] & 0xC0) == 0x80) cx++; } else if (cy + 1 < nl) { cy++; cx = 0; } break;
	case 0xff52: if (cy > 0) { cy--; if (cx > (int)strlen(ln[cy])) cx = strlen(ln[cy]); } break;
	case 0xff54: if (cy + 1 < nl) { cy++; if (cx > (int)strlen(ln[cy])) cx = strlen(ln[cy]); } break;
	case 0xff50: cx = 0; break;
	case 0xff57: cx = len; break;
	case 0xff55: cy -= rows(a); if (cy < 0) cy = 0; if (cx > (int)strlen(ln[cy])) cx = strlen(ln[cy]); break;
	case 0xff56: cy += rows(a); if (cy >= nl) cy = nl - 1; if (cx > (int)strlen(ln[cy])) cx = strlen(ln[cy]); break;
	case 0xff0d: case 0xff8d: { char *rest = strdup(l + cx); l[cx] = 0; insline(cy + 1, rest); free(rest); cy++; cx = 0; modified = 1; break; }
	case 0xff08:
		if (cx > 0) { int st = cx - 1; while (st > 0 && (l[st] & 0xC0) == 0x80) st--; memmove(l + st, l + cx, len - cx + 1); cx = st; modified = 1; }
		else if (cy > 0) { int pl = strlen(ln[cy - 1]); char *m = malloc(pl + len + 1); strcpy(m, ln[cy - 1]); strcat(m, l); free(ln[cy - 1]); ln[cy - 1] = m; delline(cy); cy--; cx = pl; modified = 1; }
		break;
	case 0xffff:
		if (cx < len) { int en = cx + 1; while (l[en] && (l[en] & 0xC0) == 0x80) en++; memmove(l + cx, l + en, len - en + 1); modified = 1; }
		else if (cy + 1 < nl) { char *m = malloc(len + strlen(ln[cy + 1]) + 1); strcpy(m, l); strcat(m, ln[cy + 1]); free(ln[cy]); ln[cy] = m; delline(cy + 1); modified = 1; }
		break;
	case 0xff09: n = 4; t = "    "; /* fallthrough */
	default:
		if (n > 0 && ((unsigned char)t[0] >= 32 || k == 0xff09) && t[0] != 127) {
			char *m = malloc(len + n + 1); memcpy(m, l, cx); memcpy(m + cx, t, n); strcpy(m + cx + n, l + cx);
			free(ln[cy]); ln[cy] = m; cx += n; modified = 1; status[0] = 0;
		}
	}
	scrollto(a); title(a);
}
int main(int argc, char **argv)
{
	ln = calloc(MAXL + 2, sizeof *ln); ln[0] = strdup(""); nl = 1;
	if (argc > 1) { if (!realpath(argv[1], path)) snprintf(path, sizeof path, "%s", argv[1]); load(path); }
	App *a = app_new("Text Editor", "aedit", 640, 440, 0);
	a->draw = draw; a->press = press; a->release = release; a->key = key;
	title(a);
	app_run(a);
	return 0;
}
