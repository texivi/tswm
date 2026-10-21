#define _GNU_SOURCE
#include "sylui.h"
#include <X11/Xatom.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct { char name[256]; int dir, sel, icon; off_t size; time_t mtime; mode_t mode; } Ent;
static Ent *ents; static int nents, cur = -1, anchor = -1, listmode, hist_n, pathedit;
static int hidden_files;
static char path[PATH_MAX], hist[64][PATH_MAX], pathbuf[PATH_MAX];
static double scroll; static int dragthumb; static double dragoff;
static char pbuf[256], note[160]; static int pmode;
static int confirm_del;
static int props;                                             /* properties box open */
enum { CW = 92, CH = 84, RH = 22, SB = 16, SBAR = 28 };

/* context menu */
enum { M_OPEN = 1, M_OPEN_EDIT, M_OPEN_VIEW, M_OPEN_DEF, M_CUT, M_COPY, M_PASTE, M_RENAME, M_DELETE, M_PROPS, M_NEWDIR, M_NEWFILE,
       M_ALL, M_REFRESH, M_ICONS, M_LIST, M_HIDDEN, M_EMPTY, M_TERM, M_RESTORE, M_SEP = 99 };
typedef struct { const char *label; int id, on; } MI;
static MI menu[20]; static int nmenu, menu_x, menu_y, menu_hover = -1, menu_open;

/* drag and drop */
static int dnd, dnd_x, dnd_y, press_item = -1, press_x, press_y, rubber, rb_x0, rb_y0, press_plain;

static int cmp(const void *a, const void *b)
{
	const Ent *x = a, *y = b;
	if (x->dir != y->dir) return y->dir - x->dir;
	return strcasecmp(x->name, y->name);
}
static const char *ext_of(const char *n) { const char *e = strrchr(n, '.'); return e && e != n ? e : ""; }
static int icon_for(const char *name, int dir)
{
	if (dir) return IC_FOLDER;
	const char *e = ext_of(name);
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
static int is_thumb(const Ent *e)
{
	const char *x = ext_of(e->name);
	return !e->dir && e->size < (12 << 20) && (!strcasecmp(x, ".png") || !strcasecmp(x, ".jpg") || !strcasecmp(x, ".jpeg") || !strcasecmp(x, ".webp") || !strcasecmp(x, ".gif") || !strcasecmp(x, ".bmp"));
}
static void full(char *out, size_t n, const char *name) { snprintf(out, n, "%s/%s", strcmp(path, "/") ? path : "", name); }
static void trash_dir(char *b) { snprintf(b, PATH_MAX, "%s/.local/share/Trash/files", getenv("HOME") ? getenv("HOME") : "/tmp"); }
static int in_trash(void) { char t[PATH_MAX]; trash_dir(t); return !strncmp(path, t, strlen(t)); }
static int nsel(void) { int n = 0; for (int i = 0; i < nents; i++) n += ents[i].sel; return n; }
static void select_only(int i) { for (int k = 0; k < nents; k++) ents[k].sel = 0; if (i >= 0 && i < nents) ents[i].sel = 1; cur = anchor = i; }
static void load(App *a)
{
	free(ents); ents = NULL; nents = 0; cur = anchor = -1; scroll = 0;
	DIR *d = opendir(path);
	int cap = 0;
	struct dirent *e;
	while (d && (e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || (e->d_name[0] == '.' && !hidden_files)) continue;
		if (nents == cap) ents = realloc(ents, (cap = cap ? cap * 2 : 64) * sizeof *ents);
		Ent *n = &ents[nents++]; memset(n, 0, sizeof *n);
		snprintf(n->name, sizeof n->name, "%s", e->d_name);
		char f[PATH_MAX + 300]; struct stat st;
		full(f, sizeof f, e->d_name);
		if (stat(f, &st) == 0) { n->dir = S_ISDIR(st.st_mode); n->size = n->dir ? 0 : st.st_size; n->mtime = st.st_mtime; n->mode = st.st_mode; }
		n->icon = icon_for(n->name, n->dir);
	}
	if (d) closedir(d);
	qsort(ents, nents, sizeof *ents, cmp);
	app_title(a, path);
}
static void go(App *a, const char *p, int push)
{
	char r[PATH_MAX];
	if (!realpath(p, r) || access(r, R_OK | X_OK)) { snprintf(note, sizeof note, "Cannot open that folder"); return; }
	if (push && hist_n < 64) snprintf(hist[hist_n++], PATH_MAX, "%s", path);
	snprintf(path, sizeof path, "%s", r);
	load(a);
}
static void up(App *a) { char p[PATH_MAX]; snprintf(p, sizeof p, "%s/..", path); go(a, p, 1); }

/* ---- file operations ---- */
static int run_wait(char *const argv[])
{
	pid_t p = fork();
	if (p == 0) { execvp(argv[0], argv); _exit(127); }
	int st = 1; if (p > 0) waitpid(p, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}
static void unique_dest(const char *dir, const char *name, char *out, size_t n)
{
	snprintf(out, n, "%s/%s", dir, name);
	const char *e = strrchr(name, '.'); int stem = e && e != name ? (int)(e - name) : (int)strlen(name); const char *ext = e && e != name ? e : "";
	for (int i = 1; access(out, F_OK) == 0 && i < 1000; i++) {
		if (i == 1) snprintf(out, n, "%s/%.*s (copy)%s", dir, stem, name, ext);
		else snprintf(out, n, "%s/%.*s (copy %d)%s", dir, stem, name, i, ext);
	}
}
static int move_path(const char *src, const char *dst)
{
	if (!rename(src, dst)) return 0;
	if (errno != EXDEV) return -1;
	char *const argv[] = {"mv", "--", (char *)src, (char *)dst, NULL};
	return run_wait(argv);
}
static void mkdirs(const char *p)
{
	char b[PATH_MAX]; snprintf(b, sizeof b, "%s", p);
	for (char *s = b + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(b, 0755); *s = '/'; }
	mkdir(b, 0755);
}
static int rm_rec(const char *p)
{
	char *const argv[] = {"rm", "-rf", "--", (char *)p, NULL};
	return run_wait(argv);
}
static void clip_set(App *a, const char *mode)
{
	char buf[PATH_MAX * 8 + 16]; int l = snprintf(buf, sizeof buf, "%s", mode);
	for (int i = 0; i < nents && l < (int)sizeof buf - PATH_MAX - 4; i++) if (ents[i].sel) { char f[PATH_MAX + 300]; full(f, sizeof f, ents[i].name); l += snprintf(buf + l, sizeof buf - l, "\n%s", f); }
	XChangeProperty(a->d, DefaultRootWindow(a->d), XInternAtom(a->d, "TSWM_FILE_CLIP", False), XInternAtom(a->d, "UTF8_STRING", False), 8, PropModeReplace, (unsigned char *)buf, l);
	XFlush(a->d);
}
static char *clip_get(App *a)
{
	Atom t; int f; unsigned long n, r; unsigned char *d = NULL;
	if (XGetWindowProperty(a->d, DefaultRootWindow(a->d), XInternAtom(a->d, "TSWM_FILE_CLIP", False), 0, 65536, False, XInternAtom(a->d, "UTF8_STRING", False), &t, &f, &n, &r, &d) != Success || !d) return NULL;
	char *s = strdup((char *)d); XFree(d); return s;
}
static int clip_has(App *a) { char *s = clip_get(a); int ok = s && strchr(s, '\n'); free(s); return ok; }
static void paste_into(App *a, const char *dir)
{
	char *s = clip_get(a); if (!s) return;
	int cut = !strncmp(s, "cut", 3), ok = 0, bad = 0;
	char *save = NULL, *line = strtok_r(s, "\n", &save); /* first line is the mode */
	while ((line = strtok_r(NULL, "\n", &save))) {
		const char *b = strrchr(line, '/'); b = b ? b + 1 : line;
		char dst[PATH_MAX + 300];
		if (cut) { snprintf(dst, sizeof dst, "%s/%s", dir, b); if (!strcmp(line, dst)) continue; if (access(dst, F_OK) == 0) unique_dest(dir, b, dst, sizeof dst); if (move_path(line, dst)) bad++; else ok++; }
		else { unique_dest(dir, b, dst, sizeof dst); char *const argv[] = {"cp", "-a", "--", line, dst, NULL}; if (run_wait(argv)) bad++; else ok++; }
	}
	free(s);
	if (cut && ok) { XDeleteProperty(a->d, DefaultRootWindow(a->d), XInternAtom(a->d, "TSWM_FILE_CLIP", False)); }
	snprintf(note, sizeof note, bad ? "%d item(s) could not be pasted" : "Pasted %d item(s)", bad ? bad : ok);
	load(a);
}
static void info_path(char *out, size_t n, const char *trashed_name)
{
	snprintf(out, n, "%s/.local/share/Trash/info/%s.trashinfo", getenv("HOME") ? getenv("HOME") : "/tmp", trashed_name);
}
static void write_info(const char *trashed_name, const char *orig)
{
	char d[PATH_MAX], p[PATH_MAX + 300]; snprintf(d, sizeof d, "%s/.local/share/Trash/info", getenv("HOME") ? getenv("HOME") : "/tmp"); mkdirs(d);
	info_path(p, sizeof p, trashed_name);
	FILE *f = fopen(p, "w"); if (!f) return;
	time_t t = time(NULL); char ts[40]; strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%S", localtime(&t));
	fprintf(f, "[Trash Info]\nPath=%s\nDeletionDate=%s\n", orig, ts); fclose(f);
}
static void do_restore(App *a)
{
	int n = 0, bad = 0;
	for (int i = 0; i < nents; i++) if (ents[i].sel) {
		char ip[PATH_MAX + 300], l[PATH_MAX + 20], orig[PATH_MAX] = "", src[PATH_MAX + 300];
		info_path(ip, sizeof ip, ents[i].name);
		FILE *f = fopen(ip, "r");
		while (f && fgets(l, sizeof l, f)) if (!strncmp(l, "Path=", 5)) { l[strcspn(l, "\n")] = 0; snprintf(orig, sizeof orig, "%s", l + 5); }
		if (f) fclose(f);
		if (!orig[0]) { bad++; continue; }
		full(src, sizeof src, ents[i].name);
		char dir[PATH_MAX], dst[PATH_MAX + 300]; snprintf(dir, sizeof dir, "%s", orig); char *sl = strrchr(dir, '/'); const char *b = sl ? sl + 1 : orig; if (sl) *sl = 0; else strcpy(dir, "/");
		mkdirs(dir); unique_dest(dir, b, dst, sizeof dst);
		if (move_path(src, dst)) bad++; else { unlink(ip); n++; }
	}
	snprintf(note, sizeof note, bad ? "%d item(s) could not be restored" : "Restored %d item(s)", bad ? bad : n);
	load(a);
}
static void do_delete(App *a, int permanent)
{
	int n = 0, bad = 0; char td[PATH_MAX]; trash_dir(td);
	if (!in_trash() && !permanent) mkdirs(td);
	for (int i = 0; i < nents; i++) if (ents[i].sel) {
		char src[PATH_MAX + 300], dst[PATH_MAX + 400]; full(src, sizeof src, ents[i].name);
		if (permanent || in_trash()) { char ip[PATH_MAX + 300]; info_path(ip, sizeof ip, ents[i].name); if (rm_rec(src)) bad++; else { unlink(ip); n++; } }
		else { unique_dest(td, ents[i].name, dst, sizeof dst); if (move_path(src, dst)) bad++; else { const char *b = strrchr(dst, '/'); write_info(b ? b + 1 : dst, src); n++; } }
	}
	snprintf(note, sizeof note, bad ? "%d item(s) could not be removed" : (permanent || in_trash() ? "Deleted %d item(s)" : "Moved %d item(s) to Trash"), bad ? bad : n);
	load(a);
}
static void empty_trash(App *a)
{
	DIR *d = opendir(path); struct dirent *e; int n = 0;
	while (d && (e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; char f[PATH_MAX + 300]; full(f, sizeof f, e->d_name); if (!rm_rec(f)) n++; }
	if (d) closedir(d);
	{ char id[PATH_MAX]; snprintf(id, sizeof id, "%s/.local/share/Trash/info", getenv("HOME") ? getenv("HOME") : "/tmp"); DIR *di = opendir(id); while (di && (e = readdir(di))) { if (e->d_name[0] == '.') continue; char f[PATH_MAX + 300]; snprintf(f, sizeof f, "%s/%s", id, e->d_name); unlink(f); } if (di) closedir(di); }
	snprintf(note, sizeof note, "Trash emptied (%d items)", n); load(a);
}
static void start_prompt(int mode)
{
	int first = -1; for (int i = 0; i < nents; i++) if (ents[i].sel) { first = i; break; }
	if (mode == 2 && first < 0) return;
	pmode = mode; snprintf(pbuf, sizeof pbuf, "%s", mode == 2 ? ents[first].name : mode == 3 ? "New file.txt" : "New Folder");
}
static void commit_prompt(App *a)
{
	char to[PATH_MAX + 300], from[PATH_MAX + 300];
	if (pbuf[0] && !strchr(pbuf, '/')) {
		full(to, sizeof to, pbuf);
		if (pmode == 1) snprintf(note, sizeof note, mkdir(to, 0755) ? "Cannot create %.100s" : "Created folder %.100s", pbuf);
		else if (pmode == 3) { FILE *f = fopen(to, "wx"); if (f) { fclose(f); snprintf(note, sizeof note, "Created %.100s", pbuf); } else snprintf(note, sizeof note, "Cannot create %.100s", pbuf); }
		else { int first = -1; for (int i = 0; i < nents; i++) if (ents[i].sel) { first = i; break; } if (first >= 0) { full(from, sizeof from, ents[first].name); snprintf(note, sizeof note, rename(from, to) ? "Cannot rename" : "Renamed to %.100s", pbuf); } }
	}
	pmode = 0; load(a);
	for (int i = 0; i < nents; i++) if (!strcmp(ents[i].name, pbuf)) { select_only(i); break; }
}
static void open_item(App *a, int i, int how)
{
	if (i < 0 || i >= nents) return;
	char p[PATH_MAX + 300]; full(p, sizeof p, ents[i].name);
	if (ents[i].dir && how == M_OPEN) { go(a, p, 1); return; }
	char cmd[PATH_MAX * 2 + 64], q[PATH_MAX * 2]; int j = 0;
	for (char *s = p; *s && j < PATH_MAX * 2 - 6; s++) { if (*s == '\'') { q[j++] = '\''; q[j++] = '\\'; q[j++] = '\''; } q[j++] = *s; }
	q[j] = 0;
	if (how == M_OPEN_EDIT) snprintf(cmd, sizeof cmd, "aedit '%s'", q);
	else if (how == M_OPEN_VIEW) snprintf(cmd, sizeof cmd, "aview '%s'", q);
	else if (how == M_OPEN_DEF) snprintf(cmd, sizeof cmd, "xdg-open '%s'", q);
	else { ui_open(p); return; }
	ui_spawn(cmd);
}
static void props_text(char *out, size_t n, int i)
{
	Ent *e = &ents[i]; char tm[64], sz[40], md[16];
	strftime(tm, sizeof tm, "%Y-%m-%d %H:%M", localtime(&e->mtime));
	if (e->size < 1024) snprintf(sz, sizeof sz, "%ld bytes", (long)e->size); else if (e->size < 1 << 20) snprintf(sz, sizeof sz, "%.1f Kb", e->size / 1024.0); else snprintf(sz, sizeof sz, "%.1f Mb", e->size / 1048576.0);
	snprintf(md, sizeof md, "%c%c%c%c%c%c%c%c%c", e->mode & S_IRUSR ? 'r' : '-', e->mode & S_IWUSR ? 'w' : '-', e->mode & S_IXUSR ? 'x' : '-', e->mode & S_IRGRP ? 'r' : '-', e->mode & S_IWGRP ? 'w' : '-', e->mode & S_IXGRP ? 'x' : '-', e->mode & S_IROTH ? 'r' : '-', e->mode & S_IWOTH ? 'w' : '-', e->mode & S_IXOTH ? 'x' : '-');
	snprintf(out, n, "%s\n%s\n%s\n%s\n%s\n%s", e->name, e->dir ? "Directory" : ext_of(e->name)[0] ? ext_of(e->name) + 1 : "File", e->dir ? "-" : sz, tm, md, path);
}

/* ---- layout ---- */
static int cols(App *a) { int c = (a->W - SB - 8) / CW; return c < 1 ? 1 : c; }
static double total(App *a) { return listmode ? nents * RH : ceil((double)nents / cols(a)) * CH + 8; }
static double viewh(App *a) { return a->H - TB_H - SBAR - (listmode ? RH : 0); }
static void clampscroll(App *a)
{
	double m = total(a) - viewh(a);
	if (m < 0) m = 0; if (scroll > m) scroll = m; if (scroll < 0) scroll = 0;
}
static void item_rect(App *a, int i, int *x, int *y, int *w, int *h)
{
	if (listmode) { *x = 0; *y = TB_H + RH + i * RH - (int)scroll; *w = a->W - SB; *h = RH; return; }
	int nc = cols(a), ox = (a->W - SB - nc * CW) / 2;
	*x = ox + (i % nc) * CW + 4; *y = TB_H + 8 + (i / nc) * CH - 2 - (int)scroll; *w = CW - 8; *h = CH - 6;
}
static int hit_item(App *a, int x, int y)
{
	if (y < TB_H || y >= a->H - SBAR || x >= a->W - SB) return -1;
	for (int i = 0; i < nents; i++) { int rx, ry, rw, rh; item_rect(a, i, &rx, &ry, &rw, &rh); if (x >= rx && x < rx + rw && y >= ry && y < ry + rh && y >= TB_H + (listmode ? RH : 0)) return i; }
	return -1;
}
static void size_str(off_t s, char *b)
{
	if (s < 1024) snprintf(b, 32, "%ld B", (long)s);
	else if (s < 1 << 20) snprintf(b, 32, "%ld Kb", (long)(s >> 10));
	else if (s < 1 << 30) snprintf(b, 32, "%.1f Mb", s / 1048576.0);
	else snprintf(b, 32, "%.1f Gb", s / 1073741824.0);
}
static void draw_ent_icon(cairo_t *c, Ent *e, double x, double y, double s)
{
	if (is_thumb(e)) {
		char f[PATH_MAX + 300]; full(f, sizeof f, e->name);
		cairo_surface_t *t = ui_icon_load(f, (int)s);
		if (t) {
			double w = cairo_image_surface_get_width(t), h = cairo_image_surface_get_height(t);
			cairo_save(c); cairo_new_path(c);
			ui_rect(c, x + (s - w) / 2 - 2, y + (s - h) / 2 - 2, w + 4, h + 4, HEX(0xFFFFFF));
			ui_set(c, HEX(0x7A7E8A)); cairo_set_line_width(c, 1); cairo_rectangle(c, x + (s - w) / 2 - 1.5, y + (s - h) / 2 - 1.5, w + 3, h + 3); cairo_stroke(c);
			cairo_set_source_surface(c, t, x + (s - w) / 2, y + (s - h) / 2); cairo_paint(c); cairo_restore(c); return;
		}
	}
	ui_icon(c, e->icon, x, y, s);
}
static void build_menu(int on_item)
{
	nmenu = 0;
	int trash = in_trash();
	if (on_item) {
		int one = nsel() == 1, isdir = cur >= 0 && ents[cur].dir;
		menu[nmenu++] = (MI){"Open", M_OPEN, one};
		if (one && !isdir) { menu[nmenu++] = (MI){"Open in AEdit", M_OPEN_EDIT, 1}; menu[nmenu++] = (MI){"Open in AView", M_OPEN_VIEW, 1}; menu[nmenu++] = (MI){"Open with default", M_OPEN_DEF, 1}; }
		menu[nmenu++] = (MI){"", M_SEP, 0};
		menu[nmenu++] = (MI){"Cut", M_CUT, 1}; menu[nmenu++] = (MI){"Copy", M_COPY, 1};
		if (one && isdir) menu[nmenu++] = (MI){"Paste into folder", M_PASTE, 1};
		menu[nmenu++] = (MI){"", M_SEP, 0};
		menu[nmenu++] = (MI){"Rename", M_RENAME, one};
		if (trash) menu[nmenu++] = (MI){"Restore", M_RESTORE, 1};
		menu[nmenu++] = (MI){trash ? "Delete permanently" : "Move to Trash", M_DELETE, 1};
		menu[nmenu++] = (MI){"", M_SEP, 0};
		menu[nmenu++] = (MI){"Properties", M_PROPS, one};
	} else {
		menu[nmenu++] = (MI){"New Folder", M_NEWDIR, 1}; menu[nmenu++] = (MI){"New Text File", M_NEWFILE, 1};
		menu[nmenu++] = (MI){"", M_SEP, 0};
		menu[nmenu++] = (MI){"Paste", M_PASTE, 1};
		menu[nmenu++] = (MI){"Select All", M_ALL, 1};
		if (trash) menu[nmenu++] = (MI){"Empty Trash", M_EMPTY, 1};
		menu[nmenu++] = (MI){"", M_SEP, 0};
		menu[nmenu++] = (MI){"Icon view", M_ICONS, !listmode}; menu[nmenu++] = (MI){"List view", M_LIST, listmode};
		menu[nmenu++] = (MI){hidden_files ? "Hide hidden files" : "Show hidden files", M_HIDDEN, 1};
		menu[nmenu++] = (MI){"Refresh", M_REFRESH, 1};
	}
}
static int menu_h(void) { int h = 6; for (int i = 0; i < nmenu; i++) h += menu[i].id == M_SEP ? 8 : 22; return h; }
static int menu_item_at(int x, int y)
{
	if (x < menu_x || x >= menu_x + 200 || y < menu_y) return -1;
	int cy = menu_y + 3;
	for (int i = 0; i < nmenu; i++) { int h = menu[i].id == M_SEP ? 8 : 22; if (y >= cy && y < cy + h) return menu[i].id == M_SEP ? -1 : i; cy += h; }
	return -1;
}
static void open_menu(App *a, int x, int y, int on_item)
{
	build_menu(on_item);
	menu_x = x + 200 > a->W ? a->W - 200 : x; menu_y = y + menu_h() > a->H ? a->H - menu_h() : y; menu_hover = -1; menu_open = 1;
}
static void do_action(App *a, int id)
{
	switch (id) {
	case M_OPEN: if (cur >= 0) open_item(a, cur, M_OPEN); break;
	case M_OPEN_EDIT: case M_OPEN_VIEW: case M_OPEN_DEF: open_item(a, cur, id); break;
	case M_CUT: clip_set(a, "cut"); snprintf(note, sizeof note, "%d item(s) cut", nsel()); break;
	case M_COPY: clip_set(a, "copy"); snprintf(note, sizeof note, "%d item(s) copied", nsel()); break;
	case M_PASTE: { char d[PATH_MAX + 300]; if (cur >= 0 && ents[cur].sel && ents[cur].dir && nsel() == 1) full(d, sizeof d, ents[cur].name); else snprintf(d, sizeof d, "%s", path); paste_into(a, d); break; }
	case M_RENAME: start_prompt(2); break;
	case M_DELETE: if (in_trash()) confirm_del = 1; else do_delete(a, 0); break;
	case M_PROPS: if (cur >= 0) props = 1; break;
	case M_RESTORE: do_restore(a); break;
	case M_NEWDIR: start_prompt(1); break;
	case M_NEWFILE: start_prompt(3); break;
	case M_ALL: for (int i = 0; i < nents; i++) ents[i].sel = 1; break;
	case M_REFRESH: load(a); break;
	case M_ICONS: listmode = 0; scroll = 0; break;
	case M_LIST: listmode = 1; scroll = 0; break;
	case M_HIDDEN: hidden_files = !hidden_files; load(a); break;
	case M_EMPTY: empty_trash(a); break;
	}
}

static void draw(App *a)
{
	cairo_t *c = a->cr; int W = a->W, H = a->H, cw = W - SB;
	clampscroll(a);
	ui_rect(c, 0, TB_H, cw, H - TB_H, BGC);
	Col fg = col_fg(BGC), sf = col_fg(T.col[C_SEL]);
	cairo_save(c); cairo_rectangle(c, 0, TB_H + (listmode ? RH : 0), cw, H - TB_H - SBAR - (listmode ? RH : 0)); cairo_clip(c);
	for (int i = 0; i < nents; i++) {
		int x, y, w, h; item_rect(a, i, &x, &y, &w, &h);
		if (y + h < TB_H || y > H) continue;
		Col k = fg; Ent *e = &ents[i];
		if (e->sel) { cairo_set_source_rgba(c, T.col[C_SEL].r, T.col[C_SEL].g, T.col[C_SEL].b, .92); cairo_rectangle(c, x, y, w, h); cairo_fill(c); k = sf; }
		if (dnd && e->dir && !e->sel && menu_hover == i) { ui_set(c, HEX(0x000000)); cairo_set_line_width(c, 1.5); cairo_rectangle(c, x + .5, y + .5, w - 1, h - 1); cairo_stroke(c); }
		if (!listmode) {
			draw_ent_icon(c, e, x + (w - 40) / 2.0, y + 4, 40);
			if (ui_tw(c, e->name, 0) > w - 4) ui_text_fit(c, x + 3, y + 56, e->name, w - 6, k, 0); else ui_text_c(c, x + w / 2.0, y + 56, e->name, k, 0);
		} else {
			draw_ent_icon(c, e, 6, y + 2, 18);
			ui_text_fit(c, 30, y + RH / 2, e->name, cw - 330, k, 0);
			char sb[32] = ""; if (!e->dir) size_str(e->size, sb);
			char tm[32]; strftime(tm, sizeof tm, "%Y-%m-%d %H:%M", localtime(&e->mtime));
			ui_text(c, cw - 290, y + RH / 2, sb, k, 0); ui_text(c, cw - 220, y + RH / 2, tm, k, 0); ui_text(c, cw - 90, y + RH / 2, e->dir ? "Directory" : "File", k, 0);
		}
	}
	cairo_restore(c);
	if (listmode) {
		ui_rect(c, 0, TB_H, cw, RH, FACE); ui_bevel(c, 0, TB_H, cw, RH, 0);
		ui_text(c, 28, TB_H + RH / 2, "Name", col_fg(FACE), 0); ui_text(c, cw - 290, TB_H + RH / 2, "Size", col_fg(FACE), 0);
		ui_text(c, cw - 220, TB_H + RH / 2, "Modified", col_fg(FACE), 0); ui_text(c, cw - 90, TB_H + RH / 2, "Type", col_fg(FACE), 0);
	}
	if (rubber) { int x0 = rb_x0 < dnd_x ? rb_x0 : dnd_x, y0 = rb_y0 < dnd_y ? rb_y0 : dnd_y, x1 = rb_x0 < dnd_x ? dnd_x : rb_x0, y1 = rb_y0 < dnd_y ? dnd_y : rb_y0;
		cairo_set_source_rgba(c, T.col[C_SEL].r, T.col[C_SEL].g, T.col[C_SEL].b, .35); cairo_rectangle(c, x0, y0, x1 - x0, y1 - y0); cairo_fill(c);
		ui_set(c, T.col[C_SEL]); cairo_set_line_width(c, 1); cairo_rectangle(c, x0 + .5, y0 + .5, x1 - x0, y1 - y0); cairo_stroke(c); }
	ui_vscroll(c, cw, TB_H, SB, H - TB_H - SBAR, scroll, viewh(a), total(a));
	ui_toolbar(c, W, hist_n > 0, "View");
	if (pathedit) { ui_field(c, 150, 10, W - 150 - 100, 24, pathbuf, 1); } else ui_field(c, 150, 10, W - 150 - 100, 24, path, 0);
	/* status bar */
	ui_rect(c, 0, H - SBAR, W, SBAR, FACE); ui_rect(c, 0, H - SBAR, W, 1, T.shadow);
	if (pmode) { ui_text(c, 8, H - 14, pmode == 1 ? "New folder:" : pmode == 3 ? "New file:" : "Rename to:", col_fg(FACE), 0); ui_field(c, 96, H - 25, cw - 104, 22, pbuf, 1); }
	else if (confirm_del) { ui_text(c, 8, H - 14, "Delete the selected items permanently?", col_fg(FACE), 1); ui_button(c, W - 184, H - 25, 80, 22, "Delete", 2); ui_button(c, W - 96, H - 25, 80, 22, "Cancel", 0); }
	else {
		char st[260], sz[32] = ""; int ns = nsel(); off_t tot = 0; for (int i = 0; i < nents; i++) if (ents[i].sel && !ents[i].dir) tot += ents[i].size;
		if (ns) size_str(tot, sz);
		snprintf(st, sizeof st, "%d items%s%s%s%s%s", nents, ns ? ", " : "", ns ? "" : "", ns ? "selected: " : "", note[0] ? "   " : "", note);
		if (ns) snprintf(st, sizeof st, "%d items, %d selected (%s)%s%s", nents, ns, sz, note[0] ? "   " : "", note);
		ui_text(c, 8, H - 14, st, col_fg(FACE), 0);
	}
	if (dnd) { Ent *e = &ents[press_item]; cairo_save(c); cairo_push_group(c); draw_ent_icon(c, e, dnd_x - 20, dnd_y - 20, 40); cairo_pop_group_to_source(c); cairo_paint_with_alpha(c, .7); cairo_restore(c); }
	if (menu_open) {
		int h = menu_h();
		ui_bevel(c, menu_x, menu_y, 200, h, 0);
		int cy = menu_y + 3;
		for (int i = 0; i < nmenu; i++) {
			if (menu[i].id == M_SEP) { ui_rect(c, menu_x + 6, cy + 3, 188, 1, T.shadow); ui_rect(c, menu_x + 6, cy + 4, 188, 1, T.light); cy += 8; continue; }
			Col t = col_fg(FACE);
			if (i == menu_hover && menu[i].on) { ui_rect(c, menu_x + 3, cy, 194, 22, T.col[C_SEL]); t = col_fg(T.col[C_SEL]); }
			else if (!menu[i].on) t = col_mix(col_fg(FACE), FACE, .6);
			ui_text(c, menu_x + 22, cy + 11, menu[i].label, t, 0);
			if ((menu[i].id == M_ICONS || menu[i].id == M_LIST) && menu[i].on) ui_arrow(c, menu_x + 11, cy + 11, 3, 3, t);
			cy += 22;
		}
	}
	if (props && cur >= 0) {
		char b[900]; props_text(b, sizeof b, cur);
		int bw = 330, bh = 220, bx = (W - bw) / 2, by = (H - bh) / 2;
		ui_bevel(c, bx, by, bw, bh, 0); ui_rect(c, bx + 3, by + 3, bw - 6, 20, T.col[C_TACT]);
		ui_text(c, bx + 10, by + 13, "Properties", col_fg(T.col[C_TACT]), 1);
		draw_ent_icon(c, &ents[cur], bx + 14, by + 32, 40);
		static const char *lab[6] = {"Name", "Type", "Size", "Modified", "Permissions", "Location"};
		char *save = NULL, *l = strtok_r(b, "\n", &save);
		for (int i = 0; i < 6 && l; i++, l = strtok_r(NULL, "\n", &save)) { ui_text(c, bx + 66, by + 44 + i * 22, lab[i], T.shadow, 0); ui_text_fit(c, bx + 150, by + 44 + i * 22, l, bw - 164, col_fg(FACE), i == 0); }
		ui_button(c, bx + bw - 94, by + bh - 36, 80, 24, "OK", 2);
	}
}
static void rubber_select(App *a)
{
	int x0 = rb_x0 < dnd_x ? rb_x0 : dnd_x, y0 = rb_y0 < dnd_y ? rb_y0 : dnd_y, x1 = rb_x0 < dnd_x ? dnd_x : rb_x0, y1 = rb_y0 < dnd_y ? dnd_y : rb_y0;
	for (int i = 0; i < nents; i++) { int x, y, w, h; item_rect(a, i, &x, &y, &w, &h); ents[i].sel = !(x + w < x0 || x > x1 || y + h < y0 || y > y1); }
}
static void press(App *a, int x, int y, int b, unsigned long t)
{
	if (b == 4 || b == 5) { scroll += (b == 4 ? -1 : 1) * 66; return; }
	if (props) { int bw = 330, bh = 220, bx = (a->W - bw) / 2, by = (a->H - bh) / 2; if (b == 1 && x >= bx + bw - 94 && x < bx + bw - 14 && y >= by + bh - 36 && y < by + bh - 12) props = 0; return; }
	if (menu_open) {
		int i = menu_item_at(x, y); menu_open = 0;
		if (i >= 0 && menu[i].on) do_action(a, menu[i].id);
		return;
	}
	if (confirm_del && b == 1 && y >= a->H - 25 && y < a->H - 3) {
		if (x >= a->W - 184 && x < a->W - 104) do_delete(a, 1);
		confirm_del = 0; return;
	}
	pathedit = 0;
	if (b == 3) {
		int i = hit_item(a, x, y);
		if (i >= 0) { if (!ents[i].sel) select_only(i); cur = i; open_menu(a, x, y, 1); }
		else if (y >= TB_H && y < a->H - SBAR && x < a->W - SB) { select_only(-1); open_menu(a, x, y, 0); }
		return;
	}
	if (b != 1) return;
	if (x >= 150 && x < a->W - 100 && y >= 10 && y < 34) { pathedit = 1; snprintf(pathbuf, sizeof pathbuf, "%s", path); return; }
	int h = ui_toolbar_hit(x, y, a->W);
	if (h == 0 && hist_n > 0) { char p[PATH_MAX]; snprintf(p, sizeof p, "%s", hist[--hist_n]); go(a, p, 0); }
	else if (h == 1) up(a);
	else if (h == 2) load(a);
	else if (h == 3) go(a, getenv("HOME") ? getenv("HOME") : "/", 1);
	else if (h == 4) { build_menu(0); menu_hover = -1; menu_x = a->W - 200; menu_y = TB_H - 2; menu_open = 1; }
	if (h >= 0) return;
	if (x >= a->W - SB && y >= TB_H) {
		double ty, th, vh = a->H - TB_H - SBAR;
		ui_vscroll_thumb(TB_H, vh, scroll, viewh(a), total(a), &ty, &th);
		if (y < TB_H + 16) scroll -= 40; else if (y >= a->H - SBAR - 16) scroll += 40;
		else if (y >= ty && y < ty + th) { dragthumb = 1; dragoff = y - ty; }
		else scroll += (y < ty ? -1 : 1) * viewh(a);
		return;
	}
	int i = hit_item(a, x, y);
	press_item = i; press_x = x; press_y = y;
	if (i >= 0 && app_isdbl(a, t, i)) { open_item(a, i, M_OPEN); press_item = -1; }
}
static void press_mod(App *a, int x, int y, unsigned st)
{
	int i = hit_item(a, x, y);
	press_plain = !(st & (ControlMask | ShiftMask));
	if (i >= 0) {
		if (st & ControlMask) { ents[i].sel = !ents[i].sel; cur = anchor = i; }
		else if ((st & ShiftMask) && anchor >= 0) { int lo = anchor < i ? anchor : i, hi = anchor < i ? i : anchor; for (int k = 0; k < nents; k++) ents[k].sel = k >= lo && k <= hi; cur = i; }
		else if (!ents[i].sel) select_only(i);
		else cur = i;
	} else if (!(st & (ControlMask | ShiftMask)) && x < a->W - SB && y >= TB_H && y < a->H - SBAR) { select_only(-1); rubber = 1; rb_x0 = x; rb_y0 = y; dnd_x = x; dnd_y = y; }
}
static void release(App *a, int x, int y, int b)
{
	dragthumb = 0; rubber = 0;
	if (dnd && b == 1) {
		int t = hit_item(a, x, y);
		dnd = 0;
		if (t >= 0 && ents[t].dir && !ents[t].sel) {
			char d[PATH_MAX + 300]; full(d, sizeof d, ents[t].name);
			int ok = 0, bad = 0;
			for (int i = 0; i < nents; i++) if (ents[i].sel) { char s[PATH_MAX + 300], dst[PATH_MAX + 400]; full(s, sizeof s, ents[i].name); unique_dest(d, ents[i].name, dst, sizeof dst); if (move_path(s, dst)) bad++; else ok++; }
			snprintf(note, sizeof note, bad ? "%d item(s) could not be moved" : "Moved %d item(s) into %.80s", bad ? bad : ok, ents[t].name);
			load(a);
		}
	}
	if (!dnd && b == 1 && press_item >= 0 && press_plain && nsel() > 1 && hit_item(a, x, y) == press_item) select_only(press_item); /* collapse selection */
	dnd = 0; press_item = -1;
}
static void motion(App *a, int x, int y, unsigned st)
{
	if (menu_open) { int i = menu_item_at(x, y); menu_hover = i; return; }
	if (dragthumb) {
		double vh = a->H - TB_H - SBAR, tot = total(a), ty, th;
		ui_vscroll_thumb(TB_H, vh, 0, viewh(a), tot, &ty, &th);
		double range = vh - 32 - th, mx = tot - viewh(a);
		if (range > 0 && mx > 0) scroll = (y - dragoff - TB_H - 16) / range * mx;
		return;
	}
	if (rubber) { dnd_x = x; dnd_y = y; rubber_select(a); return; }
	if (press_item >= 0 && (st & Button1Mask)) {
		if (!dnd && (abs(x - press_x) > 6 || abs(y - press_y) > 6) && ents[press_item].sel) dnd = 1;
		if (dnd) { dnd_x = x; dnd_y = y; menu_hover = hit_item(a, x, y); }
	}
}
static void key(App *a, KeySym k, const char *t, int n, unsigned s)
{
	int nc = listmode ? 1 : cols(a);
	if (props) { if (k == 0xff1b || k == 0xff0d) props = 0; return; }
	if (pathedit) { if (k == 0xff0d) { go(a, pathbuf, 1); pathedit = 0; } else if (k == 0xff1b) pathedit = 0; else ui_edit(pathbuf, sizeof pathbuf, k, t, n); return; }
	if (pmode) { if (k == 0xff0d) commit_prompt(a); else if (k == 0xff1b) pmode = 0; else ui_edit(pbuf, sizeof pbuf, k, t, n); return; }
	if (confirm_del) { if (k == 0xff0d) { do_delete(a, 1); confirm_del = 0; } else if (k == 0xff1b) confirm_del = 0; return; }
	note[0] = 0;
	if (k == 0xff1b) { menu_open = 0; return; }
	if (s & ControlMask) {
		if (k == 'a' || k == 'A') do_action(a, M_ALL);
		else if (k == 'c' || k == 'C') { if (nsel()) do_action(a, M_COPY); }
		else if (k == 'x' || k == 'X') { if (nsel()) do_action(a, M_CUT); }
		else if (k == 'v' || k == 'V') do_action(a, M_PASTE);
		else if ((k == 'n' || k == 'N') && (s & ShiftMask)) start_prompt(1);
		else if (k == 'l' || k == 'L') { pathedit = 1; snprintf(pathbuf, sizeof pathbuf, "%s", path); }
		return;
	}
	if (k == 0xffbf) start_prompt(2); else if (k == 0xffc4) start_prompt(1); else if (k == 0xffff) { if (nsel()) { if ((s & ShiftMask) || in_trash()) confirm_del = 1; else do_delete(a, 0); } }
	else if (k == 0xff08) up(a);
	else if (k == 0xffc2) load(a);
	else if (k == 0xff0d) { if (cur >= 0) open_item(a, cur, M_OPEN); }
	else if (k == 0xff54 || k == 0xff52 || ((k == 0xff53 || k == 0xff51) && !listmode)) {
		int d = k == 0xff54 ? nc : k == 0xff52 ? -nc : k == 0xff53 ? 1 : -1, n = (cur < 0 ? 0 : cur + d);
		if (cur < 0) n = 0;
		if (n >= 0 && n < nents) { if (s & ShiftMask) { if (anchor < 0) anchor = cur < 0 ? 0 : cur; int lo = anchor < n ? anchor : n, hi = anchor < n ? n : anchor; for (int i = 0; i < nents; i++) ents[i].sel = i >= lo && i <= hi; cur = n; } else select_only(n); }
	}
}
/* read modifiers */
static void press_wrap(App *a, int x, int y, int b, unsigned long t)
{
	Window rw, cw; int rx, ry, wx, wy; unsigned mask = 0;
	XQueryPointer(a->d, a->w, &rw, &cw, &rx, &ry, &wx, &wy, &mask);
	if (b == 1 && !menu_open && !props && !(confirm_del && y >= a->H - 25)) press_mod(a, x, y, mask);
	press(a, x, y, b, t);
}
int main(int argc, char **argv)
{
	App *a = app_new("Files", "files", 700, 480, 0);
	a->draw = draw; a->press = press_wrap; a->release = release; a->motion = motion; a->key = key;
	const char *start = argc > 1 ? argv[1] : getenv("HOME");
	if (!start) start = "/";
	if (!realpath(start, path)) strcpy(path, "/");
	load(a);
	app_run(a);
	return 0;
}
