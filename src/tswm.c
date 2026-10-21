#define _GNU_SOURCE
#include "sylui.h"
#include "comp.h"
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/scrnsaver.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <libgen.h>
#include <sys/stat.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { B = 4, TH = 16, PH = 31, NDESK = 4, ICW = 110, ICH = 77 };
enum { E_L = 1, E_R = 2, E_T = 4, E_B = 8, H_CLOSE = 16, H_MAX, H_MIN, H_TITLE };

typedef struct Client {
	Window win, frame;
	int x, y, w, h, desk, iconic, maxed, vis, unmaps, fixed, transient;
	int ox, oy, ow, oh, minw, minh, seq, ic, above, snap, full, skiptask;
	int fx, fy, fw, fh;
	char title[160], cls[64];
	cairo_surface_t *isurf;
	struct Client *next;
} Client;

static Display *dpy;
static Window root, panel;
static int scr, SW, SH, curdesk, running = 1, ncasc;
static Client *clients, *focused;
static Atom A_DEL, A_PROTO, A_NETNAME, A_UTF8, A_THEME, A_CMD;
static cairo_surface_t *wallimg; static Pixmap deskpm;
static int ak_show = AN_OPEN, ak_hide = AN_NONE, atx, aty;
static char wallkey[600];
static Client *menu_client;
static int clock_date;
static int dsel = -1, wm_ok = 1, prevdesk, seqno, sel_active, sel_n, sel_idx;
static Window selw;
static Time dlast, tlast; static int dlastid = -2; static Window tlast_w;
static struct { Client *c; int mode, sx, sy, ox, oy, ow, oh, snap; } drag;
static Cursor cur_norm, cur_move, cur_bot, cur_right, cur_br, cur_bl, cur_top, cur_left, cur_tl, cur_tr;

struct Ent { const char *label; int icon; const char *cmd; };
static const char *TERMCMD = UI_TERM;
static const struct Ent DESK[] = {
	{"Applications", IC_APPS, "applications"},
	{"Disks", IC_DISK, "files /"},
	{"Documents", IC_FOLDER, "files \"$HOME/Documents\""},
	{"Home", IC_HOME, "files \"$HOME\""},
	{"Preferences", IC_PREFS, "preferences"},
	{"Trash", IC_TRASH, "mkdir -p ~/.local/share/Trash/files; files ~/.local/share/Trash/files"},
	{"Terminal", IC_TERM, NULL},
};
#define NDESKI ((int)(sizeof DESK / sizeof DESK[0]))

typedef struct { char label[64]; int icon; cairo_surface_t *sic; char cmd[400]; int sub; char iconname[200]; } MItem;
typedef struct { MItem *it; int n; } Menu;
static Menu menus[10];

typedef struct { Window w; Menu *m; int x, y, W, H, per, hover, cal, mix, colw; int *iy, *ic, *ih; } Pop;
static Pop pop[2]; static int npop, cal_off;

static int xerr(Display *d, XErrorEvent *e) { return 0; }
static int xerr_start(Display *d, XErrorEvent *e) { wm_ok = 0; return 0; }

static int screenshot_later, reload_later, lock_later;
static void pin_action(int what);
static void wm_action(const char *cmd);
static void run_cmd(const char *cmd)
{
	if (!cmd || !*cmd) ui_spawn(TERMCMD);
	else if (!strcmp(cmd, "logout")) running = 0;
	else if (!strcmp(cmd, "lock")) lock_later = 1;
	else if (!strcmp(cmd, "internal:screenshot")) screenshot_later = 4;
	else if (!strcmp(cmd, "internal:reload")) reload_later = 1;
	else if (!strcmp(cmd, "internal:pinopen")) pin_action(0);
	else if (!strcmp(cmd, "internal:unpin")) pin_action(1);
	else if (!strncmp(cmd, "internal:w-", 11)) wm_action(cmd + 11);
	else ui_spawn(cmd);
}
static void run(const struct Ent *e) { run_cmd(e->cmd); }

/* ---------- desktop ---------- */
static void wall_load(void)
{
	const char *p = ui_wall_path(T.wall);
	char key[600]; snprintf(key, sizeof key, "%s", p ? p : "");
	if (!strcmp(key, wallkey) && (wallimg || !p)) return;
	snprintf(wallkey, sizeof wallkey, "%s", key);
	if (wallimg) cairo_surface_destroy(wallimg);
	wallimg = p ? ui_image_load(p) : NULL;
}
typedef struct { char label[64], cmd[600]; int icon; cairo_surface_t *sic; int x, y, sys, isdir; char path[PATH_MAX]; } DIcon;
static DIcon dicons[160]; static int ndicons;
static int dicon_drag = -1, dicon_moved, dicon_ox, dicon_oy, dicon_px, dicon_py;
static time_t desk_mtime;
static void dicon_pos_load(void)
{
	char p[700], l[800]; snprintf(p, sizeof p, "%s/desktop.conf", ui_cfgdir());
	FILE *f = fopen(p, "r");
	while (f && fgets(l, sizeof l, f)) {
		l[strcspn(l, "\n")] = 0; char *a1 = strchr(l, '\t'); if (!a1) continue; *a1++ = 0; int x, y; if (sscanf(a1, "%d %d", &x, &y) != 2) continue;
		for (int i = 0; i < ndicons; i++) if (!strcmp(dicons[i].label, l) && !dicons[i].isdir + 1) { dicons[i].x = x; dicons[i].y = y; }
	}
	if (f) fclose(f);
}
static void dicon_pos_save(void)
{
	char p[700]; snprintf(p, sizeof p, "%s/desktop.conf", ui_cfgdir());
	FILE *f = fopen(p, "w"); if (!f) return;
	for (int i = 0; i < ndicons; i++) fprintf(f, "%s\t%d %d\n", dicons[i].label, dicons[i].x, dicons[i].y);
	fclose(f);
}
static void dicons_build(void)
{
	DIcon old[160]; int nold = ndicons; memcpy(old, dicons, sizeof old);
	ndicons = 0;
	for (int i = 0; i < NDESKI; i++) {
		DIcon *d = &dicons[ndicons++]; memset(d, 0, sizeof *d);
		snprintf(d->label, 64, "%s", DESK[i].label); d->icon = DESK[i].icon; d->sys = 1; snprintf(d->cmd, sizeof d->cmd, "%s", DESK[i].cmd ? DESK[i].cmd : UI_TERM);
		{ int rows = (SH - PH) / ICH; if (rows < 1) rows = 1; d->x = 3 + ICW * (i / rows); d->y = 34 + ICH * (i % rows); }
	}
	char dir[PATH_MAX]; snprintf(dir, sizeof dir, "%s/Desktop", getenv("HOME") ? getenv("HOME") : "");
	DIR *dd = opendir(dir); struct dirent *e; int rows = (SH - PH) / ICH; if (rows < 1) rows = 1; int k = NDESKI;
	struct stat st; if (stat(dir, &st) == 0) desk_mtime = st.st_mtime;
	char *names[128]; int nn = 0;
	while (dd && (e = readdir(dd)) && nn < 128) if (e->d_name[0] != '.') names[nn++] = strdup(e->d_name);
	if (dd) closedir(dd);
	for (int a = 1; a < nn; a++) for (int b = a; b > 0 && strcasecmp(names[b], names[b - 1]) < 0; b--) { char *t = names[b]; names[b] = names[b - 1]; names[b - 1] = t; }
	for (int i = 0; i < nn && ndicons < 150; i++) {
		DIcon *d = &dicons[ndicons++]; memset(d, 0, sizeof *d);
		snprintf(d->path, sizeof d->path, "%s/%s", dir, names[i]); struct stat fs; int isdir = stat(d->path, &fs) == 0 && S_ISDIR(fs.st_mode);
		snprintf(d->label, 64, "%s", names[i]); d->isdir = isdir; d->icon = ui_file_icon(names[i], isdir);
		size_t l = strlen(names[i]);
		if (l > 8 && !strcmp(names[i] + l - 8, ".desktop")) { /* launcher file */
			FILE *f = fopen(d->path, "r"); char ln[600], exec[500] = "", icon[200] = "", name[64] = ""; int in = 0;
			while (f && fgets(ln, sizeof ln, f)) { ln[strcspn(ln, "\n")] = 0; if (ln[0] == '[') { in = !strcmp(ln, "[Desktop Entry]"); continue; } if (!in) continue;
				if (!strncmp(ln, "Name=", 5) && !name[0]) snprintf(name, sizeof name, "%s", ln + 5); else if (!strncmp(ln, "Exec=", 5)) snprintf(exec, sizeof exec, "%s", ln + 5); else if (!strncmp(ln, "Icon=", 5)) snprintf(icon, sizeof icon, "%s", ln + 5); }
			if (f) fclose(f);
			for (char *s2 = exec; *s2; s2++) if (*s2 == '%' && s2[1]) { memmove(s2, s2 + 2, strlen(s2 + 2) + 1); s2--; }
			if (name[0]) snprintf(d->label, 64, "%s", name); snprintf(d->cmd, sizeof d->cmd, "%s", exec); d->sic = ui_icon_load(icon, 48); d->icon = IC_FILE;
		} else if (isdir) { char q[2100]; int j = 0; for (const char *s2 = d->path; *s2 && j < 2080; s2++) { if (*s2 == '\'') { q[j++] = '\''; q[j++] = '\\'; q[j++] = '\''; } q[j++] = *s2; } q[j] = 0; snprintf(d->cmd, sizeof d->cmd, "files '%s'", q); }
		else snprintf(d->cmd, sizeof d->cmd, "open:%s", d->path);
		d->x = 3 + ICW * (k / rows); d->y = 34 + ICH * (k % rows); k++;
		free(names[i]);
	}
	dicon_pos_load();
	(void)old; (void)nold;
}
static void dicon_draw(cairo_t *c, DIcon *d, int selected)
{
	double x = d->x, y = d->y;
	if (selected) {
		cairo_set_source_rgba(c, T.col[C_SEL].r, T.col[C_SEL].g, T.col[C_SEL].b, .95);
		cairo_new_sub_path(c); double w = 108, h = 76, r = 6;
		cairo_arc(c, x + w - r, y + r, r, -M_PI / 2, 0); cairo_arc(c, x + w - r, y + h - r, r, 0, M_PI / 2);
		cairo_arc(c, x + r, y + h - r, r, M_PI / 2, M_PI); cairo_arc(c, x + r, y + r, r, M_PI, 1.5 * M_PI);
		cairo_close_path(c); cairo_fill(c);
	}
	double ix = x + 30, iy = y + 6;
	if (d->sic) { double iw = cairo_image_surface_get_width(d->sic), ih = cairo_image_surface_get_height(d->sic), sc = 48.0 / (iw > ih ? iw : ih); cairo_save(c); cairo_new_path(c); cairo_translate(c, ix + (48 - iw * sc) / 2, iy + (48 - ih * sc) / 2); cairo_scale(c, sc, sc); cairo_set_source_surface(c, d->sic, 0, 0); cairo_paint(c); cairo_restore(c); }
	else ui_icon(c, d->icon, ix, iy, 48);
	char lab[64]; snprintf(lab, sizeof lab, "%s", d->label);
	cairo_save(c); cairo_rectangle(c, x, y + 50, 108, 24); cairo_clip(c);
	double tw = ui_tw(c, lab, 0);
	if (tw > 102) { ui_text_fit(c, x + 4, y + 59, lab, 100, HEX(0x000000), 0); ui_text_fit(c, x + 3, y + 58, lab, 100, HEX(0xFFFFFF), 0); }
	else { ui_text_c(c, x + 55, y + 59, lab, HEX(0x000000), 0); ui_text_c(c, x + 54, y + 58, lab, HEX(0xFFFFFF), 0); }
	cairo_restore(c);
}
static cairo_surface_t *deskbase; static char deskbase_key[800]; static int deskbase_w, deskbase_h;
static void desk_render(void)
{
	wall_load();
	char key[800]; snprintf(key, sizeof key, "%s|%d|%d|%.3f%.3f%.3f", wallkey, SW, SH, T.col[C_DESK].r, T.col[C_DESK].g, T.col[C_DESK].b);
	if (!deskbase || strcmp(key, deskbase_key)) {
		if (deskbase) cairo_surface_destroy(deskbase);
		deskbase = cairo_image_surface_create(CAIRO_FORMAT_RGB24, SW, SH); cairo_t *b = cairo_create(deskbase);
		if (wallimg) {
			double iw = cairo_image_surface_get_width(wallimg), ih = cairo_image_surface_get_height(wallimg), k = fmax(SW / iw, SH / ih);
			cairo_translate(b, (SW - iw * k) / 2, (SH - ih * k) / 2); cairo_scale(b, k, k); cairo_set_source_surface(b, wallimg, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(b), CAIRO_FILTER_BEST); cairo_paint(b);
		} else ui_rect(b, 0, 0, SW, SH, T.col[C_DESK]);
		cairo_destroy(b); snprintf(deskbase_key, sizeof deskbase_key, "%s", key);
	}
	if (!ndicons) dicons_build();
	Pixmap pm = XCreatePixmap(dpy, root, SW, SH, DefaultDepth(dpy, scr));
	cairo_surface_t *s = cairo_xlib_surface_create(dpy, pm, DefaultVisual(dpy, scr), SW, SH);
	cairo_t *c = cairo_create(s);
	cairo_set_source_surface(c, deskbase, 0, 0); cairo_paint(c);
	if (T.watermark) {
		static cairo_surface_t *wm; static int tried;
		if (!tried) { tried = 1; char p[900]; snprintf(p, sizeof p, "%s/icons/tswm-logo.png", ui_datadir()); wm = ui_image_load(p); }
		if (wm) { double iw = cairo_image_surface_get_width(wm), ih = cairo_image_surface_get_height(wm), k = 210.0 / iw;
			cairo_save(c); cairo_translate(c, SW - 210 - 18, SH - ih * k - 16); cairo_scale(c, k, k); cairo_set_source_surface(c, wm, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(c), CAIRO_FILTER_BEST); cairo_paint_with_alpha(c, .7); cairo_restore(c); }
	}
	for (int i = 0; i < ndicons; i++) dicon_draw(c, &dicons[i], i == dsel);
	cairo_destroy(c); cairo_surface_destroy(s);
	XSetWindowBackgroundPixmap(dpy, root, pm); XClearWindow(dpy, root);
	if (deskpm) XFreePixmap(dpy, deskpm);
	deskpm = pm; comp_desktop(deskpm);
}
static int desk_hit(int x, int y)
{
	for (int i = ndicons - 1; i >= 0; i--) if (x >= dicons[i].x && x < dicons[i].x + 108 && y >= dicons[i].y && y < dicons[i].y + 76) return i;
	return -1;
}
static void dicon_run(int i)
{
	DIcon *d = &dicons[i];
	if (!strncmp(d->cmd, "open:", 5)) ui_open(d->cmd + 5); else run_cmd(d->cmd);
}

/* ---------- EWMH ---------- */
static Atom N_SUPPORTED, N_CHECK, N_CLIENTS, N_STACK, N_ACTIVE, N_NDESK, N_CURDESK, N_DESKNAMES, N_WORKAREA, N_WMDESK, N_STATE, N_FULL, N_ABOVE, N_MAXV, N_MAXH, N_HIDDEN, N_SKIPTASK, N_ATTN, N_CLOSE, N_EXTENTS, N_TYPE, N_WMNAME, N_CHANGE_STATE, N_MOVERES;
static Atom N_T_DOCK, N_T_DESKTOP, N_T_NOTIFY, N_T_TOOLTIP, N_T_MENU, N_T_DROP, N_T_POPUP, N_T_COMBO, N_T_SPLASH, N_T_DND;
static Window ewmh_win;
static void ewmh_init(void)
{
#define A(v, n) v = XInternAtom(dpy, n, False)
	A(N_SUPPORTED, "_NET_SUPPORTED"); A(N_CHECK, "_NET_SUPPORTING_WM_CHECK"); A(N_CLIENTS, "_NET_CLIENT_LIST"); A(N_STACK, "_NET_CLIENT_LIST_STACKING");
	A(N_ACTIVE, "_NET_ACTIVE_WINDOW"); A(N_NDESK, "_NET_NUMBER_OF_DESKTOPS"); A(N_CURDESK, "_NET_CURRENT_DESKTOP"); A(N_DESKNAMES, "_NET_DESKTOP_NAMES");
	A(N_WORKAREA, "_NET_WORKAREA"); A(N_WMDESK, "_NET_WM_DESKTOP"); A(N_STATE, "_NET_WM_STATE"); A(N_FULL, "_NET_WM_STATE_FULLSCREEN");
	A(N_ABOVE, "_NET_WM_STATE_ABOVE"); A(N_MAXV, "_NET_WM_STATE_MAXIMIZED_VERT"); A(N_MAXH, "_NET_WM_STATE_MAXIMIZED_HORZ"); A(N_HIDDEN, "_NET_WM_STATE_HIDDEN");
	A(N_SKIPTASK, "_NET_WM_STATE_SKIP_TASKBAR"); A(N_ATTN, "_NET_WM_STATE_DEMANDS_ATTENTION"); A(N_CLOSE, "_NET_CLOSE_WINDOW"); A(N_MOVERES, "_NET_MOVERESIZE_WINDOW"); A(N_EXTENTS, "_NET_FRAME_EXTENTS");
	A(N_TYPE, "_NET_WM_WINDOW_TYPE"); A(N_WMNAME, "_NET_WM_NAME"); A(N_CHANGE_STATE, "WM_CHANGE_STATE");
	A(N_T_DOCK, "_NET_WM_WINDOW_TYPE_DOCK"); A(N_T_DESKTOP, "_NET_WM_WINDOW_TYPE_DESKTOP"); A(N_T_NOTIFY, "_NET_WM_WINDOW_TYPE_NOTIFICATION"); A(N_T_TOOLTIP, "_NET_WM_WINDOW_TYPE_TOOLTIP");
	A(N_T_MENU, "_NET_WM_WINDOW_TYPE_MENU"); A(N_T_DROP, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU"); A(N_T_POPUP, "_NET_WM_WINDOW_TYPE_POPUP_MENU"); A(N_T_COMBO, "_NET_WM_WINDOW_TYPE_COMBO"); A(N_T_SPLASH, "_NET_WM_WINDOW_TYPE_SPLASH"); A(N_T_DND, "_NET_WM_WINDOW_TYPE_DND");
#undef A
	ewmh_win = XCreateSimpleWindow(dpy, root, -10, -10, 1, 1, 0, 0, 0);
	Atom sup[] = {N_SUPPORTED, N_CHECK, N_CLIENTS, N_STACK, N_ACTIVE, N_NDESK, N_CURDESK, N_DESKNAMES, N_WORKAREA, N_WMDESK, N_STATE, N_FULL, N_ABOVE, N_MAXV, N_MAXH, N_HIDDEN, N_SKIPTASK, N_ATTN, N_CLOSE, N_MOVERES, N_EXTENTS, N_TYPE, N_WMNAME};
	XChangeProperty(dpy, root, N_SUPPORTED, XA_ATOM, 32, PropModeReplace, (unsigned char *)sup, sizeof sup / sizeof sup[0]);
	XChangeProperty(dpy, root, N_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&ewmh_win, 1);
	XChangeProperty(dpy, ewmh_win, N_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&ewmh_win, 1);
	XChangeProperty(dpy, ewmh_win, N_WMNAME, XInternAtom(dpy, "UTF8_STRING", False), 8, PropModeReplace, (unsigned char *)"TSWM", 4);
	long nd = NDESK; XChangeProperty(dpy, root, N_NDESK, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&nd, 1);
	const char names[] = "1\0002\0003\0004"; XChangeProperty(dpy, root, N_DESKNAMES, XInternAtom(dpy, "UTF8_STRING", False), 8, PropModeReplace, (unsigned char *)names, sizeof names - 1);
	long wa[NDESK * 4]; for (int i = 0; i < NDESK; i++) { wa[i * 4] = 0; wa[i * 4 + 1] = PH; wa[i * 4 + 2] = SW; wa[i * 4 + 3] = SH - PH; }
	XChangeProperty(dpy, root, N_WORKAREA, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)wa, NDESK * 4);
}
static void ewmh_state(Client *c)
{
	Atom st[6]; int n = 0;
	if (c->full) st[n++] = N_FULL;
	if (c->above) st[n++] = N_ABOVE;
	if (c->maxed) { st[n++] = N_MAXV; st[n++] = N_MAXH; }
	if (c->iconic) st[n++] = N_HIDDEN;
	if (c->skiptask) st[n++] = N_SKIPTASK;
	XChangeProperty(dpy, c->win, N_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char *)st, n);
	long d = c->desk < 0 ? (long)0xFFFFFFFF : c->desk; XChangeProperty(dpy, c->win, N_WMDESK, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&d, 1);
}
static Client *clients_head(void);
static void ewmh_lists(void)
{
	Window lst[256]; int n = 0;
	for (Client *c = clients_head(); c && n < 256; c = c->next) lst[n++] = c->win;
	XChangeProperty(dpy, root, N_CLIENTS, XA_WINDOW, 32, PropModeReplace, (unsigned char *)lst, n);
	Client *arr[256]; int m = 0; for (Client *c = clients_head(); c && m < 256; c = c->next) arr[m++] = c;
	for (int a = 1; a < m; a++) for (int b = a; b > 0 && arr[b]->seq < arr[b - 1]->seq; b--) { Client *t = arr[b]; arr[b] = arr[b - 1]; arr[b - 1] = t; }
	for (int i = 0; i < m; i++) lst[i] = arr[i]->win;
	XChangeProperty(dpy, root, N_STACK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)lst, m);
	Window act = focused ? focused->win : None; XChangeProperty(dpy, root, N_ACTIVE, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&act, 1);
	long cd = curdesk; XChangeProperty(dpy, root, N_CURDESK, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&cd, 1);
}

/* ---------- clients ---------- */
static Client *clients_head(void) { return clients; }
static Client *find(Window w)
{
	for (Client *c = clients; c; c = c->next) if (c->win == w || c->frame == w) return c;
	return NULL;
}
static void get_title(Client *c)
{
	Atom t; int f; unsigned long n, r; unsigned char *d = NULL;
	c->title[0] = 0;
	if (XGetWindowProperty(dpy, c->win, A_NETNAME, 0, 40, False, A_UTF8, &t, &f, &n, &r, &d) == Success && d) {
		snprintf(c->title, sizeof c->title, "%s", (char *)d); XFree(d);
	} else {
		char *nm = NULL;
		if (XFetchName(dpy, c->win, &nm) && nm) { snprintf(c->title, sizeof c->title, "%s", nm); XFree(nm); }
	}
	if (!c->title[0]) strcpy(c->title, "untitled");
}
static const struct { const char *cls; int ic; } CLSICON[] = {
	{"aedit", IC_NOTE}, {"aview", IC_VIEW}, {"calculator", IC_CALC}, {"charmap", IC_CHARMAP}, {"files", IC_FOLDER}, {"launch", IC_RUN},
	{"about", IC_ABOUT}, {"preferences", IC_PREFS}, {"cpumonitor", IC_MON}, {"memmonitor", IC_MON}, {"sysinfo", IC_ABOUT},
	{"diskmanager", IC_DISK}, {"applications", IC_APPS}, {"lock", IC_LOCK}, {"powerdialog", IC_POWER}, {"xterm", IC_TERM}, {"uxterm", IC_TERM},
};
static void resolve_icon(Client *c)
{
	c->ic = IC_FILE; c->isurf = NULL; c->cls[0] = 0;
	XClassHint ch;
	if (XGetClassHint(dpy, c->win, &ch)) {
		snprintf(c->cls, sizeof c->cls, "%s", ch.res_class ? ch.res_class : ""); char nm[64]; snprintf(nm, sizeof nm, "%s", ch.res_name ? ch.res_name : "");
		XFree(ch.res_name); XFree(ch.res_class);
		for (char *p = c->cls; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
		for (unsigned i = 0; i < sizeof CLSICON / sizeof CLSICON[0]; i++) if (!strcmp(c->cls, CLSICON[i].cls) || !strcmp(nm, CLSICON[i].cls)) { c->ic = CLSICON[i].ic; return; }
	}
	Atom t; int f; unsigned long n, r; unsigned char *d = NULL;
	if (XGetWindowProperty(dpy, c->win, XInternAtom(dpy, "_NET_WM_ICON", False), 0, 1 << 20, False, XA_CARDINAL, &t, &f, &n, &r, &d) == Success && d && n > 2) {
		unsigned long *p = (unsigned long *)d, best = 0, bw = 0, bh = 0; unsigned long i = 0;
		while (i + 2 < n) { unsigned long w = p[i], h = p[i + 1]; if (!w || !h || i + 2 + w * h > n) break; if (!best || (w >= 20 && w < bw) || bw < 20) { best = i; bw = w; bh = h; } i += 2 + w * h; }
		if (bw && bh) {
			cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bw, bh); unsigned char *o = cairo_image_surface_get_data(s); int st = cairo_image_surface_get_stride(s);
			for (unsigned long y = 0; y < bh; y++) for (unsigned long x = 0; x < bw; x++) {
				unsigned long v = p[best + 2 + y * bw + x]; unsigned a = v >> 24 & 255, r8 = (v >> 16 & 255) * a / 255, g8 = (v >> 8 & 255) * a / 255, b8 = (v & 255) * a / 255;
				*(unsigned *)(o + y * st + x * 4) = a << 24 | r8 << 16 | g8 << 8 | b8;
			}
			cairo_surface_mark_dirty(s); XFree(d); c->isurf = s; return;
		}
	}
	if (d) XFree(d);
	if (c->cls[0] && (c->isurf = ui_icon_load(c->cls, 24))) return;
}
static int visible(Client *c) { return (c->desk == curdesk || c->desk < 0) && !c->iconic; }
static void apply_vis(Client *c)
{
	int v = visible(c);
	if (v && !c->vis) { comp_hint(c->frame, ak_show, atx, aty); XMapWindow(dpy, c->frame); }
	if (!v && c->vis) { if (ak_hide) comp_ghost(c->frame, ak_hide, atx, aty); XUnmapWindow(dpy, c->frame); }
	c->vis = v;
}
static void panel_draw(void);
static void open_menu(int idx, int x, int y);
static void open_cal(void);
static void open_mixer(void);
static void wm_action(const char *cmd);
static void close_pops(void);

/* Photon decorator */
static Col blend(Col a, Col b, double v) { return (Col){a.r * v + b.r * (1 - v), a.g * v + b.g * (1 - v), a.b * v + b.b * (1 - v)}; }
static void H(cairo_t *c, int x0, int x1, int y, Col k) { if (x1 >= x0) ui_rect(c, x0, y, x1 - x0 + 1, 1, k); }
static void V(cairo_t *c, int x, int y0, int y1, Col k) { if (y1 >= y0) ui_rect(c, x, y0, 1, y1 - y0 + 1, k); }
static void pframe(cairo_t *c, int L, int Tp, int R, int Bt, int solid, int recessed)
{
	Col normal = FACE, dark = blend(T.shadow, normal, .8), shine, shadow;
	if (!recessed) { shine = blend(T.light, normal, .5); shadow = blend(dark, normal, .5); }
	else { shine = blend(dark, normal, .5); shadow = blend(T.light, normal, .5); }
	H(c, L - 1, R, Tp - 1, shadow); V(c, L - 1, Tp - 1, Bt, shadow);
	H(c, L, R + 1, Bt + 1, shine); V(c, R + 1, Tp, Bt + 1, shine);
	H(c, L, R, Tp, dark); V(c, L, Tp, Bt, dark); H(c, L, R, Bt, dark); V(c, R, Tp, Bt, dark);
	if (solid) ui_rect(c, L + 1, Tp + 1, R - L - 1, Bt - Tp - 1, normal);
	H(c, L + 1, R - 2, Tp + 1, shine); V(c, L + 1, Tp + 1, Bt - 2, shine);
	H(c, L + 2, R - 1, Bt - 1, shadow); V(c, R - 1, Tp + 2, Bt - 1, shadow);
}
static int nbuttons(Client *c) { return c->fixed ? 1 : 3; }
static int drag_right(Client *c, int fw) { return fw - 4 - (c->fixed ? 12 + 3 : 3 * 12 + 3 * 3) - 1; }
static void frame_draw(Client *c)
{
	if (!c->vis) return;
	int W = c->w + 2 * B, H_ = c->h + TH + 2 * B, act = c == focused;
	cairo_surface_t *s = cairo_xlib_surface_create(dpy, c->frame, DefaultVisual(dpy, scr), W, H_);
	cairo_t *cr = cairo_create(s);
	cairo_push_group(cr);
	Col sel = act ? T.col[C_TACT] : T.col[C_TINA];
	Col fillc = blend(sel, T.shadow, .82);
	int R = W - 1, Bm = H_ - 1, il = B - 1, ir = R - (B - 1), it = B + TH - 1, ib = Bm - (B - 1);
	ui_rect(cr, 0, 0, W, H_, fillc);
	ui_frame(cr, 0, 0, W, H_, FR_RAISED | FR_THIN | FR_TRANSPARENT, FACE);
	ui_frame(cr, il, it, ir - il + 1, ib - it + 1, FR_RECESSED | FR_THIN | FR_TRANSPARENT, FACE);
	if (!c->fixed) {
		Col k = HEX(0x000000), w = HEX(0xFFFFFF);
		V(cr, 15, ib + 1, Bm - 1, k); V(cr, R - 16, ib + 1, Bm - 1, k); H(cr, ir + 1, R - 1, Bm - 16, k); H(cr, 1, il - 0, Bm - 16, k);
		V(cr, 16, ib + 1, Bm - 1, w); V(cr, R - 15, ib + 1, Bm - 1, w); H(cr, ir + 1, R - 1, Bm - 15, w); H(cr, 1, il - 0, Bm - 15, w);
	}
	int dr = drag_right(c, W);
	Col gtop = blend(T.light, sel, .5), gbot = blend(T.shadow, sel, .5);
	Col tshine = blend(gbot, sel, .5), tshadow = blend(T.shadow, gbot, .5), dtshadow = blend(T.shadow, tshadow, .5);
	H(cr, 0, dr, 0, dtshadow); V(cr, 0, 0, 19, dtshadow); H(cr, 0, dr, 19, dtshadow);
	H(cr, 1, dr - 1, 1, tshine); V(cr, 1, 1, 18, tshine);
	H(cr, 2, dr, 18, tshadow); V(cr, dr, 1, 18, tshadow);
	{
		int sr = (int)((gtop.r - gbot.r) * 255) / 15, sg = (int)((gtop.g - gbot.g) * 255) / 15, sb = (int)((gtop.b - gbot.b) * 255) / 15;
		int r0 = (int)(gtop.r * 255), g0 = (int)(gtop.g * 255), b0 = (int)(gtop.b * 255);
		for (int i = 0; i < 16; i++) {
			Col k = i == 1 ? tshine : (Col){r0 / 255.0, g0 / 255.0, b0 / 255.0};
			H(cr, 2, dr - 1, 2 + i, k);
			r0 -= sr; g0 -= sg; b0 -= sb;
		}
	}
	cairo_save(cr); cairo_rectangle(cr, 3, 1, dr - 5, 18); cairo_clip(cr);
	ui_text_title(cr, 5, 11, c->title, HEX(0x000000));
	cairo_restore(cr);
	/* button panel */
	Col dsh = blend(T.shadow, FACE, .8);
	ui_rect(cr, dr, 1, W - dr + 0, 19, dsh);
	ui_frame(cr, dr + 1, 2, W - 1 - (dr + 1), 18, FR_RAISED | FR_THIN | FR_TRANSPARENT, FACE);
	ui_rect(cr, dr + 2, 3, W - 2 - (dr + 2), 16, FACE);
	H(cr, dr - 1, W, 0, dtshadow);
	int cl = R - 3 - 12, cr_ = R - 3; /* close: left..right */
	pframe(cr, cl, 5, cr_, 16, 1, 0); pframe(cr, cl + 4, 9, cr_ - 4, 12, 0, 0);
	if (!c->fixed) {
		int ml = cl - 3 - 12, mr = cl - 3;
		pframe(cr, ml, 5, mr, 16, 1, 0); pframe(cr, ml + 8, 13, mr, 16, 1, 0);
		int zl = ml - 3 - 12, zr = ml - 3;
		pframe(cr, zl, 5, zr, 16, 1, 0); pframe(cr, zl, 5, zr - 5, 11, 1, 0);
	}
	cairo_pop_group_to_source(cr); cairo_paint(cr);
	cairo_destroy(cr); cairo_surface_destroy(s); XFlush(dpy);
}
static int hit(Client *c, int x, int y)
{
	int W = c->w + 2 * B, Hh = c->h + TH + 2 * B, e = 0, R = W - 1, Bm = Hh - 1;
	if (!c->fixed && !c->maxed) {
		if (y < 2) e = x < 16 ? (E_L | E_T) : x > R - 16 ? (E_R | E_T) : E_T;
		else if (y > Bm - B) e = x < 16 ? (E_L | E_B) : x > R - 16 ? (E_R | E_B) : E_B;
		else if (y > Bm - 16) e = x < B ? (E_L | E_B) : x > R - 16 ? (E_R | E_B) : E_B;
		else if (x < B) e = E_L;
		else if (x > R - B) e = E_R;
		if (e) return e;
	}
	int cl = R - 3 - 12, cr_ = R - 3;
	if (y >= 5 && y <= 16) {
		if (x >= cl && x <= cr_) return H_CLOSE;
		if (!c->fixed) {
			int ml = cl - 15, mr = cl - 3, zl = ml - 15, zr = ml - 3;
			if (x >= ml && x <= mr) return H_MIN;
			if (x >= zl && x <= zr) return H_MAX;
		}
	}
	if (y < B + TH) return H_TITLE;
	return 0;
}
static void send_conf(Client *c)
{
	XConfigureEvent e = {.type = ConfigureNotify, .display = dpy, .event = c->win, .window = c->win,
		.x = c->x + B, .y = c->y + B + TH, .width = c->w, .height = c->h, .border_width = 0, .above = None, .override_redirect = False};
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&e);
}
static void place(Client *c, int x, int y, int w, int h)
{
	c->x = x; c->y = y; c->w = w; c->h = h;
	XMoveResizeWindow(dpy, c->frame, x, y, w + 2 * B, h + TH + 2 * B);
	XResizeWindow(dpy, c->win, w, h);
	send_conf(c); frame_draw(c);
}
static void set_fullscreen(Client *c, int on)
{
	if (on == c->full || c->fixed) return;
	comp_morph(c->frame, c->x, c->y, c->w + 2 * B, c->h + TH + 2 * B);
	if (on) { c->fx = c->x; c->fy = c->y; c->fw = c->w; c->fh = c->h; c->full = 1; place(c, -B, -(B + TH), SW, SH); XRaiseWindow(dpy, c->frame); }
	else { c->full = 0; place(c, c->fx, c->fy, c->fw, c->fh); }
	if (focused == c) { if (on) XUnmapWindow(dpy, panel); else { XMapWindow(dpy, panel); XRaiseWindow(dpy, panel); } }
	ewmh_state(c);
}
static void focus_client(Client *c)
{
	Client *old = focused;
	focused = c;
	if (c) {
		c->seq = ++seqno;
		XRaiseWindow(dpy, c->frame);
		for (Client *k = clients; k; k = k->next) if (k->above && k != c && k->vis) XRaiseWindow(dpy, k->frame);
		if (c->above) XRaiseWindow(dpy, c->frame);
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		frame_draw(c);
	} else XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
	if (old && old != c) frame_draw(old);
	if (focused && focused->full && focused->vis) XUnmapWindow(dpy, panel); else XMapWindow(dpy, panel);
	XRaiseWindow(dpy, panel);
	ewmh_lists();
	panel_draw();
}
static void focus_top(void)
{
	Client *best = NULL;
	for (Client *c = clients; c; c = c->next) if (visible(c) && (!best || c->seq > best->seq)) best = c;
	focus_client(best);
}
static void unmanage(Client *c)
{
	Client **p = &clients;
	while (*p && *p != c) p = &(*p)->next;
	if (*p) *p = c->next;
	if (c == focused) focused = NULL;
	if (drag.c == c) { drag.c = NULL; XUngrabPointer(dpy, CurrentTime); }
	if (c->vis) comp_ghost(c->frame, AN_CLOSE, 0, 0);
	XDestroyWindow(dpy, c->frame);
	free(c);
	focus_top(); ewmh_lists();
}
static void manage(Window w)
{
	XWindowAttributes wa;
	if (!XGetWindowAttributes(dpy, w, &wa) || wa.override_redirect || find(w)) return;
	Client *c = calloc(1, sizeof *c);
	c->win = w; c->desk = curdesk;
	XSizeHints sh; long sup;
	c->minw = 60; c->minh = 30;
	if (XGetWMNormalHints(dpy, w, &sh, &sup)) {
		if (sh.flags & PMinSize) { c->minw = sh.min_width; c->minh = sh.min_height; }
		if ((sh.flags & PMinSize) && (sh.flags & PMaxSize) && sh.min_width == sh.max_width && sh.min_height == sh.max_height) c->fixed = 1;
	}
	Window tr; c->transient = XGetTransientForHint(dpy, w, &tr);
	{
		Atom t; int f; unsigned long n, r; unsigned char *d = NULL;
		if (XGetWindowProperty(dpy, w, N_TYPE, 0, 8, False, XA_ATOM, &t, &f, &n, &r, &d) == Success && d) {
			Atom *ty = (Atom *)d; int bare = 0, splash = 0;
			for (unsigned long i = 0; i < n; i++) {
				if (ty[i] == N_T_DOCK || ty[i] == N_T_DESKTOP || ty[i] == N_T_NOTIFY || ty[i] == N_T_TOOLTIP || ty[i] == N_T_MENU || ty[i] == N_T_DROP || ty[i] == N_T_POPUP || ty[i] == N_T_COMBO || ty[i] == N_T_DND) bare = 1;
				if (ty[i] == N_T_SPLASH) splash = 1;
			}
			XFree(d);
			if (bare) { XMapWindow(dpy, w); free(c); return; }
			if (splash) { c->fixed = 1; c->skiptask = 1; }
		}
	}
	get_title(c); resolve_icon(c);
	int w0 = wa.width, h0 = wa.height;
	if (w0 > SW - 2 * B) w0 = SW - 2 * B;
	if (h0 > SH - PH - TH - 2 * B) h0 = SH - PH - TH - 2 * B;
	int x, y;
	if (c->fixed || c->transient) { x = (SW - w0 - 2 * B) / 2; y = PH + (SH - PH - h0 - TH - 2 * B) / 2; }
	else { x = 110 + 26 * (ncasc % 8); y = PH + 24 + 26 * (ncasc % 8); ncasc++; if (x + w0 > SW) x = 4; if (y + h0 + TH > SH) y = PH + 4; }
	c->x = x; c->y = y; c->w = w0; c->h = h0;
	c->frame = XCreateSimpleWindow(dpy, root, x, y, w0 + 2 * B, h0 + TH + 2 * B, 0, 0, 0);
	XSetWindowAttributes at = {.background_pixmap = None};
	XChangeWindowAttributes(dpy, c->frame, CWBackPixmap, &at);
	XSelectInput(dpy, c->frame, SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask);
	XSetWindowBorderWidth(dpy, w, 0);
	XResizeWindow(dpy, w, w0, h0);
	XSelectInput(dpy, w, PropertyChangeMask | StructureNotifyMask);
	XAddToSaveSet(dpy, w);
	if (wa.map_state == IsViewable) c->unmaps++;
	XReparentWindow(dpy, w, c->frame, B, B + TH);
	XGrabButton(dpy, Button1, AnyModifier, w, False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
	XDefineCursor(dpy, w, cur_norm);
	XMapWindow(dpy, w);
	Client **p = &clients; while (*p) p = &(*p)->next; *p = c;
	c->vis = 0; apply_vis(c);
	send_conf(c);
	{ long ext[4] = {B, B, B + TH, B}; XChangeProperty(dpy, w, N_EXTENTS, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)ext, 4); }
	{
		Atom t; int f; unsigned long n, r; unsigned char *d = NULL;
		if (XGetWindowProperty(dpy, w, N_STATE, 0, 8, False, XA_ATOM, &t, &f, &n, &r, &d) == Success && d) { for (unsigned long i = 0; i < n; i++) { if (((Atom *)d)[i] == N_FULL) c->full = -1; if (((Atom *)d)[i] == N_ABOVE) c->above = 1; if (((Atom *)d)[i] == N_SKIPTASK) c->skiptask = 1; } XFree(d); }
	}
	focus_client(c);
	ewmh_state(c);
	if (c->full == -1) { c->full = 0; set_fullscreen(c, 1); }
}
static void close_client(Client *c)
{
	Atom *pr; int n, ok = 0;
	if (XGetWMProtocols(dpy, c->win, &pr, &n)) { for (int i = 0; i < n; i++) if (pr[i] == A_DEL) ok = 1; XFree(pr); }
	if (ok) {
		XEvent e = {.xclient = {.type = ClientMessage, .window = c->win, .message_type = A_PROTO, .format = 32}};
		e.xclient.data.l[0] = A_DEL; e.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &e);
	} else XKillClient(dpy, c->win);
}
static void toggle_max(Client *c)
{
	if (c->fixed) return;
	comp_morph(c->frame, c->x, c->y, c->w + 2 * B, c->h + TH + 2 * B);
	if (c->maxed) { c->maxed = 0; place(c, c->ox, c->oy, c->ow, c->oh); }
	else {
		c->ox = c->x; c->oy = c->y; c->ow = c->w; c->oh = c->h; c->maxed = 1;
		place(c, 0, PH, SW - 2 * B, SH - PH - TH - 2 * B);
	}
}
static void task_center(Client *c, int *x, int *y);
static void iconify(Client *c)
{
	task_center(c, &atx, &aty); ak_hide = AN_MIN;
	c->iconic = 1; apply_vis(c); ak_hide = AN_NONE;
	if (c == focused) focus_top(); else panel_draw();
}
static void restore(Client *c)
{
	c->iconic = 0;
	if (c->desk != curdesk) { prevdesk = curdesk; curdesk = c->desk; ak_show = ak_hide = AN_FADE; for (Client *k = clients; k; k = k->next) apply_vis(k); ak_show = AN_OPEN; ak_hide = AN_NONE; }
	task_center(c, &atx, &aty); ak_show = AN_RESTORE; apply_vis(c); ak_show = AN_OPEN;
	focus_client(c);
}
static void switch_desk(int n)
{
	if (n == curdesk || n < 0 || n >= NDESK) return;
	prevdesk = curdesk; curdesk = n;
	ak_show = ak_hide = AN_FADE;
	for (Client *c = clients; c; c = c->next) apply_vis(c);
	ak_show = AN_OPEN; ak_hide = AN_NONE;
	focus_top(); ewmh_lists();
}

/* ---- tray ---- */
static struct { int vol, mute, bright, hasbl, bat, charging, hasbat, haswifi, netup; char ssid[64]; double t; } tray;
static int mix_drag, mix_last_ms;
static int clockw(void);
static void panel_layout(void);
static int read_int(const char *path, int def)
{
	FILE *f = fopen(path, "r"); int v = def; if (f) { if (fscanf(f, "%d", &v) != 1) v = def; fclose(f); } return v;
}
static void tray_update(int full)
{
	char b[300];
	FILE *f = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
	if (f) { if (fgets(b, sizeof b, f)) { double v = 0; if (sscanf(b, "Volume: %lf", &v) == 1) tray.vol = (int)(v * 100 + .5); tray.mute = strstr(b, "MUTED") != NULL; } pclose(f); }
	DIR *d = opendir("/sys/class/backlight"); struct dirent *e; tray.hasbl = 0;
	while (d && (e = readdir(d))) if (e->d_name[0] != '.') {
		char p[300]; snprintf(p, sizeof p, "/sys/class/backlight/%s/max_brightness", e->d_name); int mx = read_int(p, 0);
		snprintf(p, sizeof p, "/sys/class/backlight/%s/brightness", e->d_name); int cu = read_int(p, 0);
		if (mx > 0) { tray.hasbl = 1; tray.bright = cu * 100 / mx; break; }
	}
	if (d) closedir(d);
	d = opendir("/sys/class/power_supply"); tray.hasbat = 0;
	while (d && (e = readdir(d))) if (!strncmp(e->d_name, "BAT", 3)) {
		char p[300]; snprintf(p, sizeof p, "/sys/class/power_supply/%s/capacity", e->d_name); tray.bat = read_int(p, 0); tray.hasbat = 1;
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/status", e->d_name); FILE *sf = fopen(p, "r"); char st[32] = ""; if (sf) { if (fgets(st, sizeof st, sf)) {} fclose(sf); }
		tray.charging = !strncmp(st, "Charging", 8) || !strncmp(st, "Full", 4);
	}
	if (d) closedir(d);
	d = opendir("/sys/class/net"); tray.haswifi = 0; tray.netup = 0;
	while (d && (e = readdir(d))) if (e->d_name[0] != '.' && strcmp(e->d_name, "lo")) {
		char p[300]; snprintf(p, sizeof p, "/sys/class/net/%s/wireless", e->d_name); int wl = access(p, F_OK) == 0;
		snprintf(p, sizeof p, "/sys/class/net/%s/operstate", e->d_name); FILE *sf = fopen(p, "r"); char st[16] = ""; if (sf) { if (fgets(st, sizeof st, sf)) {} fclose(sf); }
		if (wl) tray.haswifi = 1;
		if (!strncmp(st, "up", 2) && (wl || strncmp(e->d_name, "docker", 6))) tray.netup = 1;
	}
	if (d) closedir(d);
	if (full) {
		tray.ssid[0] = 0;
		f = popen("nmcli -t -f ACTIVE,SSID dev wifi 2>/dev/null", "r");
		while (f && fgets(b, sizeof b, f)) if (!strncmp(b, "yes:", 4)) { b[strcspn(b, "\n")] = 0; snprintf(tray.ssid, sizeof tray.ssid, "%s", b + 4); break; }
		if (f) pclose(f);
	}
	tray.t = 1;
}
/* ---- dock layout ---- */
static struct { int clock_x, clock_w, bat_x, bat_w, mix_x, pager_x, addr_x, addr_w, end; } PL;
static char addr_buf[256]; static int addr_focus;
static void panel_layout(void)
{
	cairo_surface_t *m = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1); cairo_t *mc = cairo_create(m);
	PL.clock_w = (int)ui_tw(mc, "Wed Oct 21, 2026", 0) + 16; cairo_destroy(mc); cairo_surface_destroy(m);
	PL.clock_x = SW - 4 - PL.clock_w;
	PL.bat_w = tray.hasbat ? 44 : 0; PL.bat_x = PL.clock_x - 4 - PL.bat_w;
	PL.mix_x = (tray.hasbat ? PL.bat_x : PL.clock_x) - 4 - 24;
	PL.addr_w = 150; PL.addr_x = PL.mix_x - 4 - PL.addr_w - 0;
	PL.pager_x = PL.addr_x - 8 - 44;
	PL.end = PL.pager_x - 8;
}
static int tray_hit(int x)
{
	if (x >= PL.mix_x && x < PL.mix_x + 24) return 2;
	if (tray.hasbat && x >= PL.bat_x && x < PL.bat_x + PL.bat_w) return 3;
	return 0;
}
static void tray_apply_volume(void) { char c[96]; snprintf(c, sizeof c, "wpctl set-volume @DEFAULT_AUDIO_SINK@ %d%%", tray.vol); ui_spawn(c); }
static void tray_apply_bright(void) { char c[64]; snprintf(c, sizeof c, "brightnessctl -q set %d%%", tray.bright < 1 ? 1 : tray.bright); ui_spawn(c); }

/* ---- tooltips ---- */
static Window tipw; static int hover_id = -1; static double hover_t; static char hover_label[200]; static int hover_x;
static double mono_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void tip_hide(void) { if (tipw) { XDestroyWindow(dpy, tipw); tipw = 0; } }
static void tip_show(void)
{
	if (tipw || !hover_label[0]) return;
	cairo_surface_t *m = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1); cairo_t *mc = cairo_create(m);
	int w = (int)ui_tw(mc, hover_label, 0) + 16, h = 22; cairo_destroy(mc); cairo_surface_destroy(m);
	int x = hover_x - w / 2; if (x < 2) x = 2; if (x + w > SW - 2) x = SW - w - 2;
	XSetWindowAttributes at = {.override_redirect = True, .background_pixmap = None};
	tipw = XCreateWindow(dpy, root, x, PH + 3, w, h, 0, CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixmap, &at);
	comp_hint(tipw, AN_FADE, 0, 0); XMapRaised(dpy, tipw);
	cairo_surface_t *s = cairo_xlib_surface_create(dpy, tipw, DefaultVisual(dpy, scr), w, h); cairo_t *c = cairo_create(s);
	ui_rect(c, 0, 0, w, h, HEX(0xFFFFE1)); ui_set(c, HEX(0x000000)); cairo_set_line_width(c, 1); cairo_rectangle(c, .5, .5, w - 1, h - 1); cairo_stroke(c);
	ui_text(c, 8, h / 2, hover_label, HEX(0x000000), 0);
	cairo_destroy(c); cairo_surface_destroy(s); XFlush(dpy);
}

/* ---- pins ---- */
typedef struct { char label[64], cmd[400], iconname[200]; int icon; cairo_surface_t *sic; } Pin;
static Pin pins[16]; static int npins, pin_target;
static void pin_set(Pin *p, const char *label, const char *cmd, int icon, const char *iconname)
{
	snprintf(p->label, 64, "%s", label); snprintf(p->cmd, 400, "%s", cmd); snprintf(p->iconname, 200, "%s", iconname ? iconname : ""); p->icon = icon;
	p->sic = p->iconname[0] ? ui_icon_load(p->iconname, 24) : NULL;
}
static void pins_save(void)
{
	char d[700]; snprintf(d, sizeof d, "%s/pinned.conf", ui_cfgdir());
	FILE *f = fopen(d, "w"); if (!f) return;
	for (int i = 0; i < npins; i++) fprintf(f, "%s\t%s\t%d\t%s\n", pins[i].label, pins[i].cmd, pins[i].icon, pins[i].iconname);
	fclose(f);
}
static void pins_load(void)
{
	npins = 0;
	char d[700], l[900]; snprintf(d, sizeof d, "%s/pinned.conf", ui_cfgdir());
	FILE *f = fopen(d, "r");
	while (f && fgets(l, sizeof l, f) && npins < 16) {
		l[strcspn(l, "\n")] = 0;
		char *a = strchr(l, '\t'); if (!a) continue; *a++ = 0;
		char *b = strchr(a, '\t'); if (!b) continue; *b++ = 0;
		char *c = strchr(b, '\t'); if (!c) continue; *c++ = 0;
		pin_set(&pins[npins++], l, a, atoi(b), c);
	}
	if (f) { fclose(f); return; }
}
static void pin_action(int what)
{
	if (pin_target >= npins) return;
	if (what == 0) { ui_spawn(pins[pin_target].cmd); return; }
	memmove(pins + pin_target, pins + pin_target + 1, (npins - pin_target - 1) * sizeof pins[0]); npins--; pins_save(); panel_draw();
}
static int tasks_x0(void) { return 34 + npins * 30; }

/* ---------- panel ---------- */
struct Task { Client *c; int x, w; };
static struct Task tasks[64]; static int ntasks;
static int clockw(void) { return PL.clock_w; }
static void layout_tasks(void)
{
	ntasks = 0;
	int x0 = tasks_x0();
	for (Client *c = clients; c && ntasks < 64; c = c->next)
		if ((c->desk == curdesk || c->desk < 0) && !c->skiptask) { tasks[ntasks].c = c; tasks[ntasks].x = x0 + ntasks * 30; tasks[ntasks].w = 24; ntasks++; }
}
static void task_center(Client *c, int *x, int *y)
{
	layout_tasks(); *x = 200; *y = PH / 2;
	for (int i = 0; i < ntasks; i++) if (tasks[i].c == c) { *x = tasks[i].x + tasks[i].w / 2; *y = PH / 2; }
}
static void dock_gradient(cairo_t *c, int x, int w, int h, Col top, Col bottom)
{
	int sr = (int)((top.r - bottom.r) * 255) / 30, sg = (int)((top.g - bottom.g) * 255) / 30, sb = (int)((top.b - bottom.b) * 255) / 30;
	int r = (int)(top.r * 255), g = (int)(top.g * 255), b = (int)(top.b * 255);
	for (int i = 0; i < h; i++) { ui_rect(c, x, i, w, 1, (Col){r / 255.0, g / 255.0, b / 255.0}); r -= sr; g -= sg; b -= sb; }
}
static void panel_draw(void)
{
	panel_layout();
	cairo_surface_t *s = cairo_xlib_surface_create(dpy, panel, DefaultVisual(dpy, scr), SW, PH);
	cairo_t *c = cairo_create(s);
	cairo_push_group(c);
	Col dtop = blend(T.light, T.col[C_TINA], .5), dbot = blend(T.shadow, T.col[C_TINA], .5);
	dock_gradient(c, 0, SW, 30, dtop, dbot);
	ui_rect(c, 0, 30, SW, 1, T.dark);
	if (npop && pop[0].m == &menus[0]) { Col st = blend(T.light, T.col[C_TACT], .5), sb = blend(T.shadow, T.col[C_TACT], .5); dock_gradient(c, 0, 30, 30, st, sb); }
	ui_icon(c, IC_ABOUT, 3, 3, 24);
	for (int i = 0; i < npins; i++) {
		int x = 34 + i * 30;
		if (pins[i].sic) {
			double iw = cairo_image_surface_get_width(pins[i].sic), ih = cairo_image_surface_get_height(pins[i].sic), sc = 24.0 / (iw > ih ? iw : ih);
			cairo_save(c); cairo_new_path(c); cairo_translate(c, x + (24 - iw * sc) / 2, 4 + (24 - ih * sc) / 2); cairo_scale(c, sc, sc);
			cairo_set_source_surface(c, pins[i].sic, 0, 0); cairo_paint(c); cairo_restore(c);
		} else if (pins[i].icon == IC_FILE) ui_badge(c, x, 4, 24, pins[i].label);
		else ui_icon(c, pins[i].icon, x, 4, 24);
	}
	layout_tasks();
	for (int i = 0; i < ntasks; i++) {
		Client *k = tasks[i].c; int x = tasks[i].x;
		cairo_save(c); cairo_new_path(c);
		if (k->iconic) cairo_push_group(c);
		if (k->isurf) {
			double iw = cairo_image_surface_get_width(k->isurf), ih = cairo_image_surface_get_height(k->isurf), sc = 24.0 / (iw > ih ? iw : ih);
			cairo_translate(c, x + (24 - iw * sc) / 2, 4 + (24 - ih * sc) / 2); cairo_scale(c, sc, sc); cairo_set_source_surface(c, k->isurf, 0, 0); cairo_paint(c);
		} else if (k->ic == IC_FILE) ui_badge(c, x, 4, 24, k->cls[0] ? k->cls : k->title);
		else ui_icon(c, k->ic, x, 4, 24);
		if (k->iconic) { cairo_pop_group_to_source(c); cairo_paint_with_alpha(c, .5); }
		cairo_restore(c);
	}
	{
		int ay = 5, ah = 20;
		ui_dropdown_ex(c, PL.addr_x, ay, PL.addr_w, ah, addr_buf, addr_focus);
		if (addr_focus) { cairo_surface_t *dummy = NULL; (void)dummy; double tw = ui_tw(c, addr_buf, 0); if (tw < PL.addr_w - 34) ui_rect(c, PL.addr_x + 4 + tw + 1, ay + 4, 1, ah - 8, HEX(0x000000)); }
	}
	/* desktop switcher */
	for (int i = 0; i < NDESK; i++) {
		int x = PL.pager_x + (i % 2) * 22, y = 4 + (i / 2) * 12; int on = i == curdesk;
		ui_rect(c, x, y, 21, 11, on ? HEX(0xD02020) : HEX(0x3C80D0));
		ui_set(c, HEX(0x103060)); cairo_set_line_width(c, 1); cairo_rectangle(c, x + .5, y + .5, 20, 10); cairo_stroke(c);
		char n[4]; snprintf(n, 4, "%d", i + 1);
		cairo_select_font_face(c, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL); cairo_set_font_size(c, 8);
		cairo_text_extents_t e; cairo_text_extents(c, n, &e);
		ui_set(c, HEX(0xFFFFFF)); cairo_move_to(c, x + 10.5 - e.x_advance / 2, y + 9); cairo_show_text(c, n);
	}
	if (tray.mute) { cairo_push_group(c); ui_icon(c, IC_SPEAKER, PL.mix_x, 4, 24); cairo_pop_group_to_source(c); cairo_paint_with_alpha(c, .45); } else ui_icon(c, IC_SPEAKER, PL.mix_x, 4, 24);
	if (tray.hasbat) { /* DockBattery::Paint */
		int bx = PL.bat_x, by = 3, bw = PL.bat_w;
		Col border = tray.charging ? T.light : HEX(0x000000);
		ui_set(c, border); cairo_set_line_width(c, 1);
		cairo_move_to(c, bx + 2.5, by + 5.5); cairo_line_to(c, bx + bw - 3.5, by + 5.5); cairo_line_to(c, bx + bw - 3.5, by + 8.5); cairo_line_to(c, bx + bw - 1.5, by + 8.5); cairo_line_to(c, bx + bw - 1.5, by + 15.5);
		cairo_line_to(c, bx + bw - 3.5, by + 15.5); cairo_line_to(c, bx + bw - 3.5, by + 18.5); cairo_line_to(c, bx + 2.5, by + 18.5); cairo_close_path(c); cairo_stroke(c);
		Col fill = tray.bat > 60 ? HEX(0x8AD7AE) : tray.bat > 20 ? HEX(0xE7EFAA) : HEX(0xCF8292);
		int fw = (bw - 8) * ((tray.bat > 95 ? 95 : tray.bat) + 5) / 100;
		ui_rect(c, bx + 3, by + 6, fw + 1, 12, fill);
		if (tray.bat > 95) ui_rect(c, bx + bw - 4, by + 9, 2, 6, fill);
	}
	{
		int cx = PL.clock_x, cw = PL.clock_w;
		cairo_save(c); cairo_rectangle(c, cx, 3, cw, 25); cairo_clip(c);
		dock_gradient(c, cx, cw, 30, dtop, dbot); cairo_restore(c);
		ui_frame(c, cx, 3, cw, 25, FR_RECESSED | FR_TRANSPARENT, FACE);
		time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); char tb[40];
		if (clock_date) strftime(tb, sizeof tb, "%a %b %d, %Y", &tm); else strftime(tb, sizeof tb, "%H:%M:%S", &tm);
		ui_text_c(c, cx + cw / 2, 16, tb, HEX(0x000000), 0);
	}
	cairo_pop_group_to_source(c); cairo_paint(c);
	cairo_destroy(c); cairo_surface_destroy(s); XFlush(dpy);
}
static void panel_click(int x, int y, Time tm, int button)
{
	tip_hide(); hover_id = -1;
	if (x < 30) { if (button == 1) open_menu(0, 0, PH); return; }
	if (button == 1) { if (x >= PL.clock_x && x < PL.clock_x + PL.clock_w) { clock_date = !clock_date; panel_draw(); return; } }
	if (x >= PL.addr_x && x < PL.addr_x + PL.addr_w && button == 1) { addr_focus = 1; XSetInputFocus(dpy, panel, RevertToPointerRoot, CurrentTime); panel_draw(); return; }
	if (addr_focus) { addr_focus = 0; XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime); }
	{ int th = tray_hit(x);
		if (th && button == 1) { tray_update(1); open_mixer(); return; }
		if (th == 2 && (button == 4 || button == 5)) { tray.vol += button == 4 ? 5 : -5; if (tray.vol < 0) tray.vol = 0; if (tray.vol > 100) tray.vol = 100; tray_apply_volume(); panel_draw(); return; }
		if (th == 2 && button == 2) { ui_spawn("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle"); tray.mute = !tray.mute; panel_draw(); return; }
	}
	for (int i = 0; i < npins; i++) if (x >= 34 + i * 30 && x < 62 + i * 30) {
		if (button == 1) run_cmd(pins[i].cmd);
		else if (button == 3) { pin_target = i; open_menu(8, 34 + i * 30, PH); }
		return;
	}
	if (button != 1) return;
	for (int i = 0; i < ntasks; i++) if (x >= tasks[i].x && x < tasks[i].x + tasks[i].w) {
		Client *k = tasks[i].c;
		if (k->iconic) restore(k); else if (k == focused) iconify(k); else focus_client(k);
		return;
	}
	if (x >= PL.pager_x && x < PL.pager_x + 44) { int d = (x - PL.pager_x) / 22 + ((y - 4) / 12) * 2; if (d >= 0 && d < NDESK) switch_desk(d); }
}
static void cal_draw(cairo_t *c)
{
	int w = 196; Col fg = col_fg(FACE);
	time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
	int today = tm.tm_mday, tmon = tm.tm_mon, tyr = tm.tm_year;
	int m = tm.tm_mon + cal_off, y = tm.tm_year + 1900;
	while (m < 0) { m += 12; y--; } while (m > 11) { m -= 12; y++; }
	static const char *MN[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
	char h[40]; snprintf(h, sizeof h, "%s %d", MN[m], y);
	ui_arrow(c, 14, 18, 2, 4, fg); ui_arrow(c, w - 14, 18, 3, 4, fg); ui_text_c(c, w / 2, 18, h, fg, 1);
	static const char *DN[7] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
	for (int i = 0; i < 7; i++) ui_text_c(c, 16 + i * 27, 40, DN[i], T.shadow, 0);
	struct tm f = {.tm_year = y - 1900, .tm_mon = m, .tm_mday = 1, .tm_hour = 12}; mktime(&f);
	int wd = (f.tm_wday + 6) % 7, dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (m == 1 && (y % 4 == 0 && (y % 100 || y % 400 == 0))) dim[1] = 29;
	for (int d = 1; d <= dim[m]; d++) {
		int pos = wd + d - 1, x = 16 + (pos % 7) * 27, yy = 62 + (pos / 7) * 18; char b[4]; snprintf(b, 4, "%d", d);
		Col k = fg;
		if (d == today && m == tmon && y - 1900 == tyr) { ui_rect(c, x - 11, yy - 8, 22, 16, T.col[C_SEL]); k = col_fg(T.col[C_SEL]); }
		ui_text_c(c, x, yy, b, k, 0);
	}
}
static void build_menus(void)
{
	for (int m = 0; m < 10; m++) { free(menus[m].it); menus[m].it = NULL; menus[m].n = 0; }
	AppEnt *list = calloc(1024, sizeof *list); int n = ui_apps_scan(list, 1024);
	for (int s = 0; s < 6; s++) {
		MItem *it = calloc(UI_NOWN + n + 1, sizeof *it); int k = 0;
		for (int i = 0; i < UI_NOWN; i++) if (UI_OWN[i].sec == s) { snprintf(it[k].label, 64, "%s", UI_OWN[i].name); it[k].icon = UI_OWN[i].icon; snprintf(it[k].cmd, 400, "%s", UI_OWN[i].cmd); it[k].sub = -1; k++; }
		for (int i = 0; i < n; i++) if (list[i].section == s) {
			int dup = 0; for (int j = 0; j < k; j++) if (!strcasecmp(it[j].label, list[i].name)) dup = 1;
			if (dup) continue;
			snprintf(it[k].label, 64, "%s", list[i].name); snprintf(it[k].cmd, 400, "%s", list[i].exec); it[k].sub = -1; it[k].icon = IC_FILE;
			snprintf(it[k].iconname, 200, "%s", list[i].icon); it[k].sic = ui_icon_load(list[i].icon, 18); k++;
		}
		for (int a = 1; a < k; a++) for (int b = a; b > 0 && strcasecmp(it[b].label, it[b - 1].label) < 0; b--) { MItem t = it[b]; it[b] = it[b - 1]; it[b - 1] = t; }
		menus[1 + s].it = it; menus[1 + s].n = k;
	}
	free(list);
	{
		static const struct { const char *l; int ic; const char *c; } CTX[] = {
			{"Terminal", IC_TERM, UI_TERM}, {"Files", IC_FOLDER, "files \"$HOME\""}, {"Applications...", IC_APPS, "applications"}, {"", 0, ""},
			{"Change Wallpaper", IC_DESKTOP, "preferences desktop"}, {"Preferences", IC_PREFS, "preferences"}, {"Take Screenshot", IC_VIEW, "internal:screenshot"}, {"", 0, ""},
			{"About TSWM", IC_ABOUT, "about"}, {"Reload", IC_REFRESH, "internal:reload"},
		};
		int n2 = sizeof CTX / sizeof CTX[0]; MItem *ci = calloc(n2, sizeof *ci);
		for (int i = 0; i < n2; i++) { snprintf(ci[i].label, 64, "%s", CTX[i].l); ci[i].icon = CTX[i].ic; snprintf(ci[i].cmd, 400, "%s", CTX[i].c); ci[i].sub = -1; }
		menus[7].it = ci; menus[7].n = n2;
	}
	{
		MItem *pm = calloc(2, sizeof *pm);
		snprintf(pm[0].label, 64, "Open"); pm[0].icon = IC_FOLDER; snprintf(pm[0].cmd, 400, "internal:pinopen"); pm[0].sub = -1;
		snprintf(pm[1].label, 64, "Unpin from top bar"); pm[1].icon = IC_TRASH; snprintf(pm[1].cmd, 400, "internal:unpin"); pm[1].sub = -1;
		menus[8].it = pm; menus[8].n = 2;
	}
	MItem *mm = calloc(14, sizeof *mm); int k = 0;
	for (int s = 0; s < 6; s++) if (menus[1 + s].n) {
		snprintf(mm[k].label, 64, "%s", UI_SECTIONS[s]); mm[k].icon = IC_FOLDER; mm[k].sub = 1 + s;
		static cairo_surface_t *dock_ic[6]; char pth[900]; snprintf(pth, sizeof pth, "%s/icons/dock-%s.png", ui_datadir(), UI_SECTIONS[s]);
		if (!dock_ic[s]) dock_ic[s] = ui_image_load(pth); mm[k].sic = dock_ic[s]; k++;
	}
	mm[k].sub = -1; k++; /* separator */
	snprintf(mm[k].label, 64, "About..."); mm[k].icon = IC_ABOUT; snprintf(mm[k].cmd, 400, "about"); mm[k].sub = -1; k++;
	snprintf(mm[k].label, 64, "Lock Screen"); mm[k].icon = IC_LOCK; snprintf(mm[k].cmd, 400, "lock"); mm[k].sub = -1; k++;
	snprintf(mm[k].label, 64, "Quit..."); mm[k].icon = IC_LOGOUT; snprintf(mm[k].cmd, 400, "powerdialog"); mm[k].sub = -1; k++;
	menus[0].it = mm; menus[0].n = k;
}
static int mix_rows(void) { return 2 + (tray.hasbl ? 1 : 0) + (tray.hasbat ? 1 : 0); }
static int mi_h(MItem *it) { return it->label[0] ? 32 : 10; }
static void pop_geom(Pop *p)
{
	if (p->mix) { p->W = 256; p->H = 24 + (tray.hasbl ? 0 : 0) + 96 + (tray.hasbl ? 62 : 0) + (tray.hasbat ? 34 : 0) + 38; return; }
	if (p->cal) { p->W = 196; p->H = 178; return; }
	cairo_surface_t *m = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1); cairo_t *mc = cairo_create(m);
	int mx = 0, sub = 0, n = p->m->n;
	for (int k = 0; k < n; k++) { int tw = (int)ui_tw(mc, p->m->it[k].label, 0); if (tw > mx) mx = tw; if (p->m->it[k].sub >= 0) sub = 1; }
	cairo_destroy(mc); cairo_surface_destroy(m);
	p->colw = 31 + mx + (sub ? 32 : 10); if (p->colw > 330) p->colw = 330;
	free(p->iy); free(p->ic); free(p->ih); p->iy = calloc(n + 1, sizeof(int)); p->ic = calloc(n + 1, sizeof(int)); p->ih = calloc(n + 1, sizeof(int));
	int avail = SH - PH - 4, y = 1, col = 0, maxy = 0;
	for (int k = 0; k < n; k++) {
		int h = mi_h(&p->m->it[k]);
		if (y + h > avail && y > 1) { col++; y = 1; }
		p->iy[k] = y; p->ic[k] = col; p->ih[k] = h; y += h; if (y > maxy) maxy = y;
	}
	p->per = col + 1; p->W = (col + 1) * p->colw + 2; p->H = maxy + 1;
}
static void mixer_draw(cairo_t *c, Pop *p)
{
	Col fg = col_fg(FACE); char b[64]; int y = 10;
	ui_icon(c, tray.mute ? IC_MUTE : IC_SPEAKER, 12, y, 24); ui_text(c, 44, y + 12, "Volume", fg, 1); snprintf(b, sizeof b, "%d%%", tray.vol); ui_text(c, p->W - 14 - ui_tw(c, b, 0), y + 12, b, fg, 0);
	ui_slider(c, 16, y + 34, p->W - 32, tray.vol * 255 / 100);
	ui_check(c, 16, y + 58, "Mute", tray.mute); y += 88;
	if (tray.hasbl) {
		ui_icon(c, IC_BRIGHT, 12, y, 24); ui_text(c, 44, y + 12, "Brightness", fg, 1); snprintf(b, sizeof b, "%d%%", tray.bright); ui_text(c, p->W - 14 - ui_tw(c, b, 0), y + 12, b, fg, 0);
		ui_slider(c, 16, y + 34, p->W - 32, tray.bright * 255 / 100); y += 62;
	}
	if (tray.hasbat) { ui_icon(c, tray.charging ? IC_PLUG : IC_BATTERY, 12, y, 24); snprintf(b, sizeof b, "Battery %d%%%s", tray.bat, tray.charging ? ", charging" : ""); ui_text(c, 44, y + 12, b, fg, 0); y += 34; }
	ui_icon(c, tray.netup ? IC_WIFI : IC_NOWIFI, 12, y, 24);
	ui_text_fit(c, 44, y + 12, tray.netup ? (tray.ssid[0] ? tray.ssid : "Connected") : "Not connected", 110, fg, 0);
	ui_button(c, p->W - 94, y + 2, 82, 22, "Settings", 0);
}
static void pop_draw(int i)
{
	Pop *p = &pop[i];
	cairo_surface_t *s = cairo_xlib_surface_create(dpy, p->w, DefaultVisual(dpy, scr), p->W, p->H);
	cairo_t *c = cairo_create(s);
	if (p->mix) { ui_bevel(c, 0, 0, p->W, p->H, 0); mixer_draw(c, p); }
	else if (p->cal) { ui_bevel(c, 0, 0, p->W, p->H, 0); cal_draw(c); }
	else {
		ui_rect(c, 0, 0, p->W, p->H, T.menubg);
		for (int k = 0; k < p->m->n; k++) {
			MItem *it = &p->m->it[k]; int x = 1 + p->ic[k] * p->colw, y = p->iy[k], h = p->ih[k]; Col fg = HEX(0x000000);
			if (!it->label[0]) { ui_rect(c, x + 3, y + 4, p->colw - 6, 1, T.shadow); ui_rect(c, x + 3, y + 5, p->colw - 6, 1, T.light); continue; }
			if (k == p->hover) ui_rect(c, x, y, p->colw, h, T.selmenu);
			if (it->sic) {
				double iw = cairo_image_surface_get_width(it->sic), ih = cairo_image_surface_get_height(it->sic), sc = 24.0 / (iw > ih ? iw : ih);
				cairo_save(c); cairo_new_path(c); cairo_translate(c, x + 4 + (24 - iw * sc) / 2, y + 5 + (24 - ih * sc) / 2); cairo_scale(c, sc, sc);
				cairo_set_source_surface(c, it->sic, 0, 0); cairo_paint(c); cairo_restore(c);
			} else if (it->icon == IC_FILE) ui_badge(c, x + 4, y + 5, 24, it->label);
			else ui_icon(c, it->icon, x + 4, y + 5, 24);
			ui_text_fit(c, x + 31, y + h / 2, it->label, p->colw - 31 - (it->sub >= 0 ? 32 : 6), fg, 0);
			if (it->sub >= 0) ui_arrow(c, x + p->colw - 12, y + h / 2, 3, 4, fg);
		}
		ui_frame(c, 0, 0, p->W, p->H, FR_RAISED | FR_THIN | FR_TRANSPARENT, FACE);
	}
	cairo_destroy(c); cairo_surface_destroy(s); XFlush(dpy);
}
static Window pop_window(int x, int y, int w, int h)
{
	Window win = XCreateSimpleWindow(dpy, root, x, y, w, h, 0, 0, 0);
	XSetWindowAttributes at = {.override_redirect = True, .background_pixmap = None};
	XChangeWindowAttributes(dpy, win, CWOverrideRedirect | CWBackPixmap, &at);
	XSelectInput(dpy, win, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
	comp_hint(win, AN_FADE, 0, 0);
	XMapRaised(dpy, win);
	return win;
}
static void close_pops(void)
{
	if (!npop) return;
	XUngrabPointer(dpy, CurrentTime);
	for (int i = 0; i < npop; i++) { XDestroyWindow(dpy, pop[i].w); free(pop[i].iy); free(pop[i].ic); free(pop[i].ih); pop[i].iy = pop[i].ic = pop[i].ih = NULL; }
	npop = 0;
}
static void close_sub(void) { if (npop > 1) { XDestroyWindow(dpy, pop[1].w); free(pop[1].iy); free(pop[1].ic); free(pop[1].ih); pop[1].iy = pop[1].ic = pop[1].ih = NULL; npop = 1; } }
static void open_menu(int idx, int x, int y)
{
	int level = idx == 0 || idx == 7 || idx == 8 || idx == 9 ? 0 : 1;
	if (level == 0) close_pops(); else close_sub();
	Pop *p = &pop[level]; memset(p, 0, sizeof *p); p->m = &menus[idx]; p->hover = -1; pop_geom(p);
	if (x + p->W > SW) x = SW - p->W; if (y + p->H > SH) y = SH - p->H; if (y < PH) y = PH;
	p->x = x; p->y = y; p->w = pop_window(x, y, p->W, p->H);
	if (level == 0) XGrabPointer(dpy, p->w, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	npop = level + 1;
}
static void open_mixer(void)
{
	close_pops();
	Pop *p = &pop[0]; memset(p, 0, sizeof *p); p->mix = 1; pop_geom(p);
	p->x = SW - p->W - 4; p->y = PH; p->w = pop_window(p->x, p->y, p->W, p->H);
	XGrabPointer(dpy, p->w, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	npop = 1; mix_drag = 0;
}
static void open_cal(void)
{
	close_pops();
	Pop *p = &pop[0]; memset(p, 0, sizeof *p); p->cal = 1; pop_geom(p);
	p->x = SW - 56 - 196 - 6; p->y = PH; p->w = pop_window(p->x, p->y, p->W, p->H);
	XGrabPointer(dpy, p->w, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	npop = 1;
}
static int pop_at(int rx, int ry, int *item)
{
	for (int i = npop - 1; i >= 0; i--) {
		Pop *p = &pop[i]; int x = rx - p->x, y = ry - p->y;
		if (x < 0 || y < 0 || x >= p->W || y >= p->H) continue;
		*item = -1;
		if (!p->cal && !p->mix) for (int k = 0; k < p->m->n; k++) if (x >= 1 + p->ic[k] * p->colw && x < 1 + (p->ic[k] + 1) * p->colw && y >= p->iy[k] && y < p->iy[k] + p->ih[k]) { *item = k; break; }
		return i;
	}
	return -1;
}
static int mix_slider_y(int which) { return 10 + (which == 0 ? 34 : 88 + 34); }
static void mix_set(Pop *p, int rx, int which)
{
	int v = (rx - p->x - 16 - 5) * 100 / (p->W - 32 - 10); if (v < 0) v = 0; if (v > 100) v = 100;
	struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); int ms = (int)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
	if (which == 0) tray.vol = v; else tray.bright = v;
	if (ms - mix_last_ms > 100) { mix_last_ms = ms; if (which == 0) tray_apply_volume(); else tray_apply_bright(); }
	pop_draw(0); panel_draw();
}
static void pop_motion(int rx, int ry)
{
	if (npop && pop[0].mix) { if (mix_drag) mix_set(&pop[0], rx, mix_drag - 1); return; }
	int it, i = pop_at(rx, ry, &it);
	if (i < 0 || pop[i].cal) return;
	if (pop[i].hover != it) { pop[i].hover = it; pop_draw(i); }
	if (i == 0) {
		if (it >= 0 && pop[0].m->it[it].sub >= 0) {
			if (npop > 1 && pop[1].m == &menus[pop[0].m->it[it].sub]) return;
			open_menu(pop[0].m->it[it].sub, pop[0].x + 1 + (pop[0].ic[it] + 1) * pop[0].colw - 2, pop[0].y + pop[0].iy[it] - 1);
			pop_draw(1);
		} else close_sub();
	}
}
static void pop_click(int rx, int ry, int button)
{
	int it, i = pop_at(rx, ry, &it);
	if (i < 0) { close_pops(); return; }
	if (pop[i].mix) {
		Pop *p = &pop[i]; int y = ry - p->y, x = rx - p->x;
		if (button != 1) return;
		if (y >= mix_slider_y(0) - 6 && y < mix_slider_y(0) + 16) { mix_drag = 1; mix_set(p, rx, 0); }
		else if (y >= 10 + 58 - 2 && y < 10 + 58 + 16 && x < 110) { ui_spawn("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle"); tray.mute = !tray.mute; pop_draw(0); panel_draw(); }
		else if (tray.hasbl && y >= mix_slider_y(1) - 6 && y < mix_slider_y(1) + 16) { mix_drag = 2; mix_set(p, rx, 1); }
		else if (x >= p->W - 94 && y >= p->H - 36) { close_pops(); ui_spawn("xterm -e nmtui"); }
		return;
	}
	if (pop[i].cal) { if (ry - pop[i].y < 30) { cal_off += rx - pop[i].x < pop[i].W / 2 ? -1 : 1; pop_draw(i); } else close_pops(); return; }
	if (it < 0) return;
	MItem *m = &pop[i].m->it[it];
	if (button == 3) {
		if (m->cmd[0] && strncmp(m->cmd, "internal:", 9) && strcmp(m->cmd, "logout") && npins < 16) {
			int dup = 0; for (int k = 0; k < npins; k++) if (!strcmp(pins[k].cmd, m->cmd)) dup = 1;
			if (!dup) { pin_set(&pins[npins++], m->label, m->cmd, m->icon, m->iconname); pins_save(); }
		}
		close_pops(); panel_draw(); return;
	}
	if (button != 1) return;
	if (m->cmd[0]) { char c[400]; snprintf(c, sizeof c, "%s", m->cmd); close_pops(); run_cmd(c); }
}

/* ---- keys ---- */
static const struct { KeySym ks; unsigned mod; } KEYS[] = {
	{XK_Tab, Mod1Mask}, {XK_Tab, Mod1Mask | ShiftMask}, {XK_grave, Mod1Mask},
	{XK_F1, Mod1Mask}, {XK_F2, Mod1Mask}, {XK_F3, Mod1Mask}, {XK_F4, Mod1Mask},
	{XK_BackSpace, ControlMask | Mod1Mask}, {XK_Print, 0},
};
#define NKEYS ((int)(sizeof KEYS / sizeof KEYS[0]))
static void grab_keys(void)
{
	unsigned lock[4] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
	for (int i = 0; i < NKEYS; i++) {
		KeyCode kc = XKeysymToKeycode(dpy, KEYS[i].ks);
		for (int j = 0; j < 4; j++) XGrabKey(dpy, kc, KEYS[i].mod | lock[j], root, True, GrabModeAsync, GrabModeAsync);
	}
}
static void screenshot(void)
{
	char d[600], p[700]; time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
	snprintf(d, sizeof d, "%s/Pictures", getenv("HOME") ? getenv("HOME") : "/tmp"); mkdir(d, 0755);
	strftime(p, sizeof p, "/screenshot-%Y%m%d-%H%M%S.png", &tm); strncat(d, p, sizeof d - strlen(d) - 1);
	if (comp_on()) { comp_dump(d); return; }
	cairo_surface_t *s = cairo_xlib_surface_create(dpy, root, DefaultVisual(dpy, scr), SW, SH);
	cairo_surface_write_to_png(s, d); cairo_surface_destroy(s);
}
static void apply_mouse(void)
{
	if (T.mouse_speed < 1) return;
	char c[64]; snprintf(c, sizeof c, "xset m %d 4", T.mouse_speed * 3 / 2 + 1); ui_spawn(c);
	ui_spawn(T.mouse_left ? "xmodmap -e 'pointer = 3 2 1'" : "xmodmap -e 'pointer = 1 2 3'");
}
static pid_t lock_pid;
static void start_lock(void)
{
	if (lock_pid > 0 && kill(lock_pid, 0) == 0) return;
	lock_pid = fork();
	if (lock_pid == 0) { setsid(); execlp("lock", "lock", (char *)NULL); _exit(1); }
}
static void apply_power(void)
{
	char c[96];
	if (T.dpms_min > 0) { snprintf(c, sizeof c, "xset s off +dpms dpms %d %d %d", T.dpms_min * 60, T.dpms_min * 60, T.dpms_min * 60); ui_spawn(c); }
	else ui_spawn("xset s off -dpms");
}
static void idle_check(void)
{
	static int xss = -1; static XScreenSaverInfo *info;
	if (xss < 0) { int ev, er; xss = XScreenSaverQueryExtension(dpy, &ev, &er); info = xss ? XScreenSaverAllocInfo() : NULL; }
	if (!xss || !info || T.lock_min <= 0) return;
	if (XScreenSaverQueryInfo(dpy, root, info) && info->idle > (unsigned long)T.lock_min * 60000UL) start_lock();
}
static void theme_refresh(void)
{
	Theme old = T; theme_load();
	if (old.dpms_min != T.dpms_min) apply_power();
	if (old.mouse_speed != T.mouse_speed || old.mouse_left != T.mouse_left) apply_mouse();
	if (strcmp(old.wall, T.wall) || strcmp(old.font, T.font) || old.fsize != T.fsize || memcmp(&old.col[C_DESK], &T.col[C_DESK], sizeof(Col)) || memcmp(&old.col[C_SEL], &T.col[C_SEL], sizeof(Col))) desk_render();
	panel_draw();
	for (Client *c = clients; c; c = c->next) frame_draw(c);
}
static void reload_all(void)
{
	theme_load(); apply_mouse(); pins_load(); pins_load(); build_menus(); desk_render(); panel_draw();
	for (Client *c = clients; c; c = c->next) frame_draw(c);
}
static Client *sel_list[64];
static void sel_draw(void)
{
	int w = 380, h = sel_n * 24 + 12;
	cairo_surface_t *s = cairo_xlib_surface_create(dpy, selw, DefaultVisual(dpy, scr), w, h);
	cairo_t *c = cairo_create(s);
	ui_bevel(c, 0, 0, w, h, 0);
	for (int i = 0; i < sel_n; i++) {
		Col fg = col_fg(FACE);
		if (i == sel_idx) { ui_rect(c, 4, 6 + i * 24, w - 8, 24, T.col[C_SEL]); fg = col_fg(T.col[C_SEL]); }
		ui_text_fit(c, 12, 18 + i * 24, sel_list[i]->title, w - 24, fg, 0);
	}
	cairo_destroy(c); cairo_surface_destroy(s); XFlush(dpy);
}
static void sel_end(int commit)
{
	XUngrabKeyboard(dpy, CurrentTime); XDestroyWindow(dpy, selw); sel_active = 0;
	if (commit && sel_idx < sel_n) { Client *n = sel_list[sel_idx]; if (n->iconic) restore(n); else focus_client(n); }
}
static void sel_start(int back)
{
	sel_n = 0;
	for (Client *c = clients; c && sel_n < 64; c = c->next) if (c->desk == curdesk) sel_list[sel_n++] = c;
	if (sel_n < 2) return;
	for (int a = 1; a < sel_n; a++) for (int b = a; b > 0 && sel_list[b]->seq > sel_list[b - 1]->seq; b--) { Client *t = sel_list[b]; sel_list[b] = sel_list[b - 1]; sel_list[b - 1] = t; }
	sel_idx = back ? sel_n - 1 : 1;
	int w = 380, h = sel_n * 24 + 12;
	selw = pop_window((SW - w) / 2, (SH - h) / 2, w, h);
	sel_active = 1;
	XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
	sel_draw();
}
static void on_key(XKeyEvent *e)
{
	KeySym ks = XkbKeycodeToKeysym(dpy, e->keycode, 0, 0);
	unsigned m = e->state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
	if (e->type == KeyRelease) {
		if (sel_active && (ks == XK_Alt_L || ks == XK_Alt_R || ks == XK_Meta_L)) sel_end(1);
		return;
	}
	if (sel_active) {
		if (ks == XK_Tab || ks == XK_ISO_Left_Tab) { sel_idx = (sel_idx + ((m & ShiftMask) || ks == XK_ISO_Left_Tab ? sel_n - 1 : 1)) % sel_n; sel_draw(); }
		else if (ks == XK_Escape) sel_end(0);
		return;
	}
	if (m & ControlMask && m & Mod1Mask) {
		if (ks == XK_BackSpace) reload_all();
	} else if (m == Mod1Mask || m == (Mod1Mask | ShiftMask)) {
		if (ks == XK_Tab) sel_start(m & ShiftMask);
		else if (ks >= XK_F1 && ks <= XK_F4) switch_desk(ks - XK_F1);
		else if (ks == XK_grave) switch_desk(prevdesk);
	} else if (ks == XK_Print) screenshot();
}

/* ---------- events ---------- */
static void set_cursor(Client *c, int h)
{
	Cursor k = cur_norm;
	switch (h) {
	case E_L: case E_R: k = h == E_L ? cur_left : cur_right; break;
	case E_T: k = cur_top; break; case E_B: k = cur_bot; break;
	case E_L | E_T: k = cur_tl; break; case E_R | E_T: k = cur_tr; break;
	case E_L | E_B: k = cur_bl; break; case E_R | E_B: k = cur_br; break;
	}
	XDefineCursor(dpy, c->frame, k);
}
static void snap_rect(int mode, int *x, int *y, int *w, int *h)
{
	*y = PH; *h = SH - PH; *w = SW / 2; *x = mode == 2 ? SW - SW / 2 : 0;
	if (mode == 3) { *x = 0; *w = SW; }
}
static void apply_snap(Client *c, int mode)
{
	if (c->fixed) return;
	if (mode == 3) { if (!c->maxed) toggle_max(c); return; }
	if (!c->maxed && !c->snap) { c->ox = c->x; c->oy = c->y; c->ow = c->w; c->oh = c->h; }
	int x, y, w, h; snap_rect(mode, &x, &y, &w, &h);
	comp_morph(c->frame, c->x, c->y, c->w + 2 * B, c->h + TH + 2 * B);
	c->maxed = 0; c->snap = mode;
	place(c, x, y, w - 2 * B, h - TH - 2 * B);
}
static void do_drag(int rx, int ry)
{
	Client *c = drag.c; if (!c) return;
	int dx = rx - drag.sx, dy = ry - drag.sy;
	if (drag.mode == 1) {
		c->x = drag.ox + dx; c->y = drag.oy + dy; if (c->y < PH - TH) c->y = PH - TH;
		XMoveWindow(dpy, c->frame, c->x, c->y); send_conf(c);
		int sm = c->fixed ? 0 : rx <= 1 ? 1 : rx >= SW - 2 ? 2 : ry <= 1 ? 3 : 0;
		if (sm != drag.snap) { drag.snap = sm; if (sm) { int x, y, w, h; snap_rect(sm, &x, &y, &w, &h); comp_preview(1, x, y, w, h); } else comp_preview(0, 0, 0, 0, 0); }
		return;
	}
	int x = drag.ox, y = drag.oy, w = drag.ow, h = drag.oh, m = drag.mode;
	if (m & E_R) w += dx;
	if (m & E_B) h += dy;
	if (m & E_L) w -= dx;
	if (m & E_T) h -= dy;
	if (w < c->minw) w = c->minw;
	if (h < c->minh) h = c->minh;
	if (m & E_L) x = drag.ox + drag.ow - w;
	if (m & E_T) y = drag.oy + drag.oh - h;
	place(c, x, y, w, h);
}
static void open_winmenu(Client *c, int x, int y);
static void apply_snap(Client *c, int mode);
static void on_button(XButtonEvent *e)
{
	if (e->window == panel) { if (e->type == ButtonPress) panel_click(e->x, e->y, e->time, e->button); return; }
	if (npop && (e->window == pop[0].w || (npop > 1 && e->window == pop[1].w))) {
		if (e->type == ButtonPress) pop_click(e->x_root, e->y_root, e->button);
		else if (e->type == ButtonRelease && mix_drag) { if (mix_drag == 1) tray_apply_volume(); else tray_apply_bright(); mix_drag = 0; }
		return;
	}
	if (e->window == root && e->type == ButtonRelease) {
		if (dicon_drag >= 0 && dicon_moved) {
			DIcon *d = &dicons[dicon_drag]; d->x = (d->x - 3 + ICW / 2) / ICW * ICW + 3; d->y = 34 + (d->y - 34 + ICH / 2) / ICH * ICH; if (d->x < 3) d->x = 3; if (d->y < 34) d->y = 34;
			int over = -1; for (int k = 0; k < ndicons; k++) if (k != dicon_drag && d->x == dicons[k].x && d->y == dicons[k].y) over = k;
			if (over >= 0 && !d->sys && dicons[over].sys && !strcmp(dicons[over].label, "Trash")) { char *const av[] = {"gio", "trash", "--", d->path, NULL}; if (fork() == 0) { setsid(); execvp("gio", av); _exit(1); } dicons_build(); }
			else if (over >= 0) d->y += ICH;
			dicon_pos_save(); desk_render();
		}
		dicon_drag = -1; return;
	}
	if (e->window == root) {
		if (e->type == ButtonPress && e->button == 3) { dsel = -1; desk_render(); open_menu(7, e->x_root, e->y_root); return; }
		if (e->type != ButtonPress || e->button != 1) return;
		int i = desk_hit(e->x, e->y);
		if (i != dsel) { dsel = i; desk_render(); }
		if (i >= 0) {
			if (i == dlastid && e->time - dlast < 400) { dicon_run(i); dlastid = -2; dicon_drag = -1; }
			else { dlastid = i; dlast = e->time; dicon_drag = i; dicon_moved = 0; dicon_ox = e->x - dicons[i].x; dicon_oy = e->y - dicons[i].y; dicon_px = e->x; dicon_py = e->y; }
		}
		return;
	}
	Client *c = find(e->window);
	if (!c) return;
	if (e->window == c->win) {
		if (e->type == ButtonPress) { focus_client(c); XAllowEvents(dpy, ReplayPointer, e->time); }
		return;
	}
	if (e->type == ButtonRelease) {
		if (drag.c) { comp_alpha(drag.c->frame, 1.0); comp_preview(0, 0, 0, 0, 0); if (drag.snap && drag.mode == 1) apply_snap(drag.c, drag.snap); drag.snap = 0; drag.c = NULL; XUngrabPointer(dpy, CurrentTime); }
		return;
	}
	if (e->button == 3) { int h3 = hit(c, e->x, e->y); if (h3 == H_TITLE) { focus_client(c); open_winmenu(c, e->x_root, e->y_root); } return; }
	if (e->button != 1) return;
	focus_client(c);
	int h = hit(c, e->x, e->y);
	if (h == H_CLOSE) close_client(c);
	else if (h == H_MAX) toggle_max(c);
	else if (h == H_MIN) iconify(c);
	else if (h == H_TITLE) {
		if (tlast_w == c->frame && e->time - tlast < 350) { toggle_max(c); tlast_w = 0; return; }
		tlast = e->time; tlast_w = c->frame;
		if (c->maxed) return;
		drag.c = c; drag.mode = 1; drag.sx = e->x_root; drag.sy = e->y_root; drag.ox = c->x; drag.oy = c->y;
		XGrabPointer(dpy, c->frame, False, ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	} else if (h) {
		drag.c = c; drag.mode = h; drag.sx = e->x_root; drag.sy = e->y_root; drag.ox = c->x; drag.oy = c->y; drag.ow = c->w; drag.oh = c->h;
		XGrabPointer(dpy, c->frame, False, ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	}
}
static void open_winmenu(Client *c, int x, int y)
{
	free(menus[9].it); menus[9].it = calloc(12, sizeof(MItem)); int n = 0; MItem *it = menus[9].it; menu_client = c;
#define WI(lbl, ico, cm) do { snprintf(it[n].label, 64, "%s", lbl); it[n].icon = ico; snprintf(it[n].cmd, 400, "internal:w-%s", cm); it[n].sub = -1; n++; } while (0)
	WI("Minimize", IC_DESKTOP, "min");
	if (!c->fixed) WI(c->maxed ? "Restore" : "Maximize", IC_VIEW, "max");
	WI(c->above ? "Always on top  [on]" : "Always on top", IC_UP, "top");
	WI(c->desk < 0 ? "All desktops  [on]" : "All desktops", IC_FOLDER, "all");
	for (int d = 0; d < NDESK; d++) if (d != c->desk) { char l[40], cm[8]; snprintf(l, sizeof l, "Move to desktop %d", d + 1); snprintf(cm, sizeof cm, "d%d", d); WI(l, IC_FOLDER, cm); }
	n++; /* separator */
	WI("Close", IC_TRASH, "close");
#undef WI
	menus[9].n = n;
	open_menu(9, x, y);
}
static void wm_action(const char *a)
{
	Client *c = menu_client; if (!c) return;
	if (!strcmp(a, "min")) iconify(c);
	else if (!strcmp(a, "max")) toggle_max(c);
	else if (!strcmp(a, "top")) { c->above = !c->above; if (c->above) XRaiseWindow(dpy, c->frame); XRaiseWindow(dpy, panel); }
	else if (!strcmp(a, "all")) { c->desk = c->desk < 0 ? curdesk : -1; panel_draw(); }
	else if (!strcmp(a, "close")) close_client(c);
	else if (a[0] == 'd' && a[1] >= '0' && a[1] <= '9') { ak_hide = AN_FADE; c->desk = a[1] - '0'; apply_vis(c); ak_hide = AN_NONE; if (c == focused) focus_top(); panel_draw(); }
}
static void panel_hover(int x)
{
	int id = -1; const char *lab = "";
	char buf[200];
	if (x < 30) { id = 1; lab = "Menu"; }
	for (int i = 0; i < npins && id < 0; i++) if (x >= 34 + i * 30 && x < 62 + i * 30) { id = 100 + i; lab = pins[i].label; }
	for (int i = 0; i < ntasks && id < 0; i++) if (x >= tasks[i].x && x < tasks[i].x + tasks[i].w) { id = 200 + i; lab = tasks[i].c->title; }
	if (id < 0 && x >= PL.pager_x && x < PL.pager_x + 44) { int d = (x - PL.pager_x) / 22; snprintf(buf, sizeof buf, "Desktop %d", d + 1); id = 300 + d; lab = buf; }
	if (id < 0) { int th = tray_hit(x); if (th == 1) { id = 400; snprintf(buf, sizeof buf, tray.netup ? "Network: connected" : "Network: not connected"); lab = buf; } else if (th == 2) { id = 401; snprintf(buf, sizeof buf, "Volume: %d%%%s", tray.vol, tray.mute ? " (muted)" : ""); lab = buf; } else if (th == 3) { id = 402; snprintf(buf, sizeof buf, "Battery: %d%%%s", tray.bat, tray.charging ? " (charging)" : ""); lab = buf; } }
	if (id < 0 && x >= PL.clock_x && x < PL.clock_x + PL.clock_w) { time_t t = time(NULL); strftime(buf, sizeof buf, "%A, %d %B %Y", localtime(&t)); id = 2; lab = buf; }
	if (id == hover_id) return;
	tip_hide(); hover_id = id; hover_t = mono_now(); hover_x = x;
	snprintf(hover_label, sizeof hover_label, "%s", lab);
}
static void panel_key(XKeyEvent *e)
{
	if (e->type != KeyPress) return;
	char b[32]; KeySym ks; int n = XLookupString(e, b, sizeof b - 1, &ks, NULL); b[n < 0 ? 0 : n] = 0;
	if (ks == XK_Return || ks == XK_KP_Enter) { if (addr_buf[0]) { char cmd[600]; if (strstr(addr_buf, "://") || !strncmp(addr_buf, "www.", 4)) snprintf(cmd, sizeof cmd, "xdg-open '%s'", addr_buf); else snprintf(cmd, sizeof cmd, "%s", addr_buf); ui_spawn(cmd); addr_buf[0] = 0; } addr_focus = 0; XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime); }
	else if (ks == XK_Escape) { addr_buf[0] = 0; addr_focus = 0; XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime); }
	else ui_edit(addr_buf, sizeof addr_buf, ks == XK_BackSpace ? 0xff08 : ks, b, n);
	panel_draw();
}
static void handle(XEvent *e)
{
	switch (e->type) {
	case MapRequest: manage(e->xmaprequest.window); break;
	case ConfigureNotify:
		if (e->xconfigure.window == root && (e->xconfigure.width != SW || e->xconfigure.height != SH)) {
			SW = e->xconfigure.width; SH = e->xconfigure.height;
			XResizeWindow(dpy, panel, SW, PH); desk_render(); panel_draw();
			for (Client *c = clients; c; c = c->next) if (c->maxed) place(c, 0, PH, SW - 2 * B, SH - PH - TH - 2 * B);
		}
		break;
	case ConfigureRequest: {
		XConfigureRequestEvent *r = &e->xconfigurerequest;
		Client *c = find(r->window);
		if (c && c->win == r->window) {
			if (c->maxed) { send_conf(c); break; }
			int w = r->value_mask & CWWidth ? r->width : c->w, h = r->value_mask & CWHeight ? r->height : c->h;
			if (w != c->w || h != c->h) place(c, c->x, c->y, w, h); else send_conf(c);
		} else if (!c) {
			XWindowChanges wc = {.x = r->x, .y = r->y, .width = r->width, .height = r->height, .border_width = r->border_width, .sibling = r->above, .stack_mode = r->detail};
			XConfigureWindow(dpy, r->window, r->value_mask, &wc);
		}
		break;
	}
	case UnmapNotify: {
		Client *c = find(e->xunmap.window);
		if (c && c->win == e->xunmap.window) {
			if (c->unmaps > 0) c->unmaps--; else unmanage(c);
		}
		break;
	}
	case DestroyNotify: { Client *c = find(e->xdestroywindow.window); if (c && c->win == e->xdestroywindow.window) unmanage(c); break; }
	case PropertyNotify:
		if (e->xproperty.window == root && e->xproperty.atom == A_CMD && e->xproperty.state == PropertyNewValue) {
			Atom t; int f; unsigned long n, r; unsigned char *d = NULL;
			if (XGetWindowProperty(dpy, root, A_CMD, 0, 64, True, A_UTF8, &t, &f, &n, &r, &d) == Success && d) { run_cmd((char *)d); XFree(d); }
		} else if (e->xproperty.window == root && e->xproperty.atom == A_THEME) {
			theme_refresh();
		} else {
			Client *c = find(e->xproperty.window);
			if (c && c->win == e->xproperty.window && (e->xproperty.atom == A_NETNAME || e->xproperty.atom == XA_WM_NAME)) { get_title(c); frame_draw(c); panel_draw(); }
			else if (c && c->win == e->xproperty.window && e->xproperty.atom == XInternAtom(dpy, "_NET_WM_ICON", False)) { resolve_icon(c); panel_draw(); }
		}
		break;
	case Expose:
		if (e->xexpose.count) break;
		if (e->xexpose.window == panel) panel_draw();
		else if (sel_active && e->xexpose.window == selw) sel_draw();
		else if (npop && e->xexpose.window == pop[0].w) pop_draw(0);
		else if (npop > 1 && e->xexpose.window == pop[1].w) pop_draw(1);
		else { Client *c = find(e->xexpose.window); if (c && c->frame == e->xexpose.window) frame_draw(c); }
		break;
	case LeaveNotify: if (e->xcrossing.window == panel) { hover_id = -1; tip_hide(); } break;
	case ClientMessage: {
		XClientMessageEvent *m = &e->xclient;
		if (m->message_type == N_CURDESK) switch_desk((int)m->data.l[0]);
		else if (m->message_type == N_ACTIVE) { Client *c = find(m->window); if (c) { if (c->iconic) restore(c); else { if (c->desk >= 0 && c->desk != curdesk) switch_desk(c->desk); focus_client(c); } } }
		else if (m->message_type == N_CLOSE) { Client *c = find(m->window); if (c) close_client(c); }
		else if (m->message_type == N_MOVERES) { Client *c = find(m->window); if (c && !c->full) { long f = m->data.l[0]; int w = f & 4096 ? (int)m->data.l[3] : c->w, h = f & 8192 ? (int)m->data.l[4] : c->h; place(c, f & 1024 ? (int)m->data.l[1] : c->x, f & 2048 ? (int)m->data.l[2] : c->y, w, h); } }
		else if (m->message_type == N_CHANGE_STATE && m->data.l[0] == 3) { Client *c = find(m->window); if (c) iconify(c); }
		else if (m->message_type == N_WMDESK) { Client *c = find(m->window); if (c) { long d = m->data.l[0]; c->desk = d == (long)0xFFFFFFFF || d < 0 ? -1 : (int)d % NDESK; apply_vis(c); panel_draw(); ewmh_state(c); } }
		else if (m->message_type == N_STATE) {
			Client *c = find(m->window); if (!c) break;
			int act = (int)m->data.l[0];
			for (int k = 1; k <= 2; k++) {
				Atom p = (Atom)m->data.l[k]; if (!p) continue;
				if (p == N_FULL) set_fullscreen(c, act == 2 ? !c->full : act);
				else if (p == N_ABOVE) { c->above = act == 2 ? !c->above : act; if (c->above) XRaiseWindow(dpy, c->frame); XRaiseWindow(dpy, panel); }
				else if (p == N_MAXV || p == N_MAXH) { int want = act == 2 ? !c->maxed : act; if (want != c->maxed) toggle_max(c); }
				else if (p == N_HIDDEN) { if (act == 1 || (act == 2 && !c->iconic)) iconify(c); else if (c->iconic) restore(c); }
				else if (p == N_SKIPTASK) { c->skiptask = act == 2 ? !c->skiptask : act; panel_draw(); }
			}
			ewmh_state(c);
		}
		break;
	}
	case KeyPress: case KeyRelease:
		if (e->xkey.window == panel && addr_focus) { panel_key(&e->xkey); break; }
		on_key(&e->xkey); break;
	case ButtonPress: case ButtonRelease: on_button(&e->xbutton); break;
	case MotionNotify: {
		while (XCheckTypedEvent(dpy, MotionNotify, e));
		if (e->xmotion.window == panel) { panel_hover(e->xmotion.x); break; }
		if (e->xmotion.window == root && dicon_drag >= 0) {
			if (!dicon_moved && abs(e->xmotion.x - dicon_px) + abs(e->xmotion.y - dicon_py) < 6) break;
			dicon_moved = 1; dicons[dicon_drag].x = e->xmotion.x - dicon_ox; dicons[dicon_drag].y = e->xmotion.y - dicon_oy; desk_render(); break;
		}
		if (npop) {
			pop_motion(e->xmotion.x_root, e->xmotion.y_root);
		} else if (drag.c) do_drag(e->xmotion.x_root, e->xmotion.y_root);
		else { Client *c = find(e->xmotion.window); if (c && c->frame == e->xmotion.window) set_cursor(c, hit(c, e->xmotion.x, e->xmotion.y)); }
		break;
	}
	}
}

static Cursor mkcursor(int kind)
{
	enum { S = 24 };
	XcursorImage *im = XcursorImageCreate(S, S);
	cairo_surface_t *s = cairo_image_surface_create_for_data((unsigned char *)im->pixels, CAIRO_FORMAT_ARGB32, S, S, S * 4);
	cairo_t *c = cairo_create(s);
	if (kind == 0) {
		im->xhot = 2; im->yhot = 1;
		for (int pass = 0; pass < 2; pass++) {
			double o = pass == 0 ? 1.4 : 0;
			cairo_move_to(c, 2 + o, 1 + o); cairo_line_to(c, 2 + o, 18 + o); cairo_line_to(c, 6.5 + o, 14.2 + o); cairo_line_to(c, 9.6 + o, 21 + o);
			cairo_line_to(c, 12.6 + o, 19.7 + o); cairo_line_to(c, 9.6 + o, 13 + o); cairo_line_to(c, 15.5 + o, 13 + o); cairo_close_path(c);
			if (pass == 0) { cairo_set_source_rgba(c, 0, 0, 0, .28); cairo_fill(c); }
			else {
				cairo_set_source_rgb(c, 1, 1, 1); cairo_fill_preserve(c);
				cairo_set_source_rgb(c, 0, 0, 0); cairo_set_line_width(c, 1.3); cairo_set_line_join(c, CAIRO_LINE_JOIN_ROUND); cairo_stroke(c);
			}
		}
	} else {
		im->xhot = im->yhot = 12;
		static const double ang[5] = {0, 0, M_PI / 2, M_PI / 4, -M_PI / 4};
		cairo_translate(c, 12, 12); cairo_rotate(c, ang[kind]); cairo_translate(c, -12, -12);
		cairo_move_to(c, 1, 12); cairo_line_to(c, 7.5, 5.5); cairo_line_to(c, 7.5, 9.8); cairo_line_to(c, 16.5, 9.8); cairo_line_to(c, 16.5, 5.5);
		cairo_line_to(c, 23, 12); cairo_line_to(c, 16.5, 18.5); cairo_line_to(c, 16.5, 14.2); cairo_line_to(c, 7.5, 14.2); cairo_line_to(c, 7.5, 18.5); cairo_close_path(c);
		cairo_set_source_rgb(c, 1, 1, 1); cairo_fill_preserve(c);
		cairo_set_source_rgb(c, 0, 0, 0); cairo_set_line_width(c, 1.3); cairo_set_line_join(c, CAIRO_LINE_JOIN_ROUND); cairo_stroke(c);
	}
	cairo_destroy(c); cairo_surface_destroy(s);
	Cursor cu = XcursorImageLoadCursor(dpy, im);
	XcursorImageDestroy(im);
	return cu;
}
static volatile sig_atomic_t dump_flag;
static void on_usr1(int sig) { dump_flag = 1; }

int main(void)
{
	char exe[512]; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
	if (n > 0) {
		exe[n] = 0; char *dir = dirname(exe);
		const char *old = getenv("PATH"); char np[2048];
		snprintf(np, sizeof np, "%s:%s", dir, old ? old : "/usr/bin"); setenv("PATH", np, 1);
		if (!getenv("TSWM_DATA")) { char d[600]; snprintf(d, sizeof d, "%s/../data", dir); char w[700]; snprintf(w, sizeof w, "%s/wallpapers", d); if (access(w, R_OK) == 0) setenv("TSWM_DATA", d, 1); }
	}
	signal(SIGCHLD, SIG_IGN); signal(SIGUSR1, on_usr1);
	if (!(dpy = XOpenDisplay(NULL))) { fprintf(stderr, "tswm: cannot open display\n"); return 1; }
	scr = DefaultScreen(dpy); root = RootWindow(dpy, scr); SW = DisplayWidth(dpy, scr); SH = DisplayHeight(dpy, scr);
	XSetErrorHandler(xerr_start);
	XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask | ButtonReleaseMask | Button1MotionMask | PropertyChangeMask | KeyPressMask | KeyReleaseMask);
	XSync(dpy, False);
	if (!wm_ok) { fprintf(stderr, "tswm: another window manager is running\n"); return 1; }
	XSetErrorHandler(xerr);
	A_DEL = XInternAtom(dpy, "WM_DELETE_WINDOW", False); A_PROTO = XInternAtom(dpy, "WM_PROTOCOLS", False);
	A_NETNAME = XInternAtom(dpy, "_NET_WM_NAME", False); A_UTF8 = XInternAtom(dpy, "UTF8_STRING", False);
	A_THEME = XInternAtom(dpy, "TSWM_THEME", False);
	ewmh_init(); A_CMD = XInternAtom(dpy, "TSWM_CMD", False);
	cur_norm = mkcursor(0); cur_left = cur_right = mkcursor(1); cur_top = cur_bot = mkcursor(2);
	cur_tl = cur_br = mkcursor(3); cur_tr = cur_bl = mkcursor(4); cur_move = cur_norm;
	XDefineCursor(dpy, root, cur_norm);
	unlink(ui_preview_path()); /* drop stale preview */
	theme_load();
	XSetWindowAttributes at = {.override_redirect = True, .background_pixmap = None};
	panel = XCreateWindow(dpy, root, 0, 0, SW, PH, 0, CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixmap, &at);
	XSelectInput(dpy, panel, ExposureMask | ButtonPressMask | PointerMotionMask | LeaveWindowMask | KeyPressMask | KeyReleaseMask);
	comp_noanim(panel);
	comp_init(dpy, root, SW, SH);
	XMapRaised(dpy, panel);
	desk_render(); panel_draw();
	comp_splash();
	grab_keys();
	pins_load();
	build_menus();
	apply_mouse(); apply_power(); tray_update(0); ui_spawn("notifyd");
	{ char as[700]; snprintf(as, sizeof as, "%s/autostart", ui_cfgdir()); if (access(as, X_OK) == 0) ui_spawn(as); }
	{ /* adopt existing windows */
		Window r, p, *kids; unsigned nk;
		if (XQueryTree(dpy, root, &r, &p, &kids, &nk)) {
			for (unsigned i = 0; i < nk; i++) {
				XWindowAttributes wa;
				if (kids[i] != panel && XGetWindowAttributes(dpy, kids[i], &wa) && !wa.override_redirect && wa.map_state == IsViewable) { Client *c0 = clients; (void)c0; manage(kids[i]); }
			}
			XFree(kids);
		}
	}
	struct pollfd pf = {ConnectionNumber(dpy), POLLIN, 0};
	time_t last = 0;
	while (running) {
		while (XPending(dpy)) { XEvent e; XNextEvent(dpy, &e); comp_event(&e); handle(&e); }
		time_t now = time(NULL);
		if (now != last) { last = now; panel_draw(); }
		if (reload_later) { reload_later = 0; reload_all(); }
		if (lock_later) { lock_later = 0; start_lock(); }
		{ static time_t desk_t; if (now - desk_t >= 4) { desk_t = now; char dd2[PATH_MAX]; snprintf(dd2, sizeof dd2, "%s/Desktop", getenv("HOME") ? getenv("HOME") : ""); struct stat st2; if (stat(dd2, &st2) == 0 && st2.st_mtime != desk_mtime) { dicons_build(); desk_render(); } } }
		{ static time_t idle_t, tray_tm; if (now - idle_t >= 5) { idle_t = now; idle_check(); } if (now - tray_tm >= 4) { tray_tm = now; int ov = tray.vol, om = tray.mute, ob = tray.bat, on = tray.netup; tray_update(0); if (ov != tray.vol || om != tray.mute || ob != tray.bat || on != tray.netup) panel_draw(); } }
		if (hover_id >= 0 && !tipw && !npop && mono_now() - hover_t > .45) tip_show();
		comp_frame();
		if (screenshot_later && --screenshot_later == 0) screenshot();
		if (dump_flag) { dump_flag = 0; comp_dump("/tmp/tswm-dump.png"); }
		int to = comp_wait();
		if (hover_id >= 0 && !tipw && (to < 0 || to > 100)) to = 100;
		poll(&pf, 1, screenshot_later ? 40 : to < 0 ? 500 : to);
	}
	for (Client *c = clients; c; c = c->next) XMapWindow(dpy, c->win);
	XCloseDisplay(dpy);
	return 0;
}
