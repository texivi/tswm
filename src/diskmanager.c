#define _GNU_SOURCE
#include "sylui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

typedef struct { char dev[80], mnt[120], type[20]; double total, used; } Fs;
static Fs fs[32]; static int nfs;
static void scan(App *a)
{
	nfs = 0; FILE *f = fopen("/proc/mounts", "r"); char dev[256], mnt[256], type[64];
	while (f && fscanf(f, "%255s %255s %63s %*[^\n]", dev, mnt, type) == 3 && nfs < 32) {
		if (strncmp(dev, "/dev/", 5) || !strncmp(dev, "/dev/loop", 9) || strstr(mnt, "/snap/")) continue;
		int dup = 0; for (int i = 0; i < nfs; i++) if (!strcmp(fs[i].dev, dev)) dup = 1;
		if (dup) continue;
		struct statvfs v; if (statvfs(mnt, &v)) continue;
		Fs *e = &fs[nfs++]; snprintf(e->dev, 80, "%s", dev); snprintf(e->mnt, 120, "%s", mnt); snprintf(e->type, 20, "%s", type);
		e->total = (double)v.f_blocks * v.f_frsize; e->used = e->total - (double)v.f_bfree * v.f_frsize;
	}
	if (f) fclose(f);
}
static void sz(double b, char *o) { if (b < 1 << 30) snprintf(o, 32, "%.0f Mb", b / 1048576); else snprintf(o, 32, "%.1f Gb", b / 1073741824.0); }
static void draw(App *a)
{
	cairo_t *c = a->cr; int W = a->W; Col fg = col_fg(FACE);
	ui_rect(c, 8, 8, W - 16, a->H - 16, BGC); ui_bevel(c, 8, 8, W - 16, a->H - 16, 1);
	ui_rect(c, 9, 9, W - 18, 20, FACE); ui_bevel(c, 9, 9, W - 18, 20, 0);
	ui_text(c, 40, 19, "Device", fg, 0); ui_text(c, 180, 19, "Mounted on", fg, 0); ui_text(c, 310, 19, "Type", fg, 0); ui_text(c, 380, 19, "Used", fg, 0);
	Col tf = col_fg(BGC);
	for (int i = 0; i < nfs; i++) {
		int y = 34 + i * 44; Fs *e = &fs[i]; char b[80], u[32], t[32];
		ui_icon(c, IC_DISK, 14, y, 26);
		ui_text_fit(c, 46 - 6 + 0, y + 8, e->dev, 130, tf, 0); ui_text_fit(c, 180, y + 8, e->mnt, 124, tf, 0); ui_text(c, 310, y + 8, e->type, tf, 0);
		double pct = e->total > 0 ? 100 * e->used / e->total : 0;
		ui_rect(c, 380, y, W - 400, 14, BGC); ui_bevel(c, 380, y, W - 400, 14, 1);
		ui_rect(c, 382, y + 2, (W - 404) * pct / 100, 10, pct > 90 ? HEX(0xC83030) : T.col[C_TACT]);
		sz(e->used, u); sz(e->total, t); snprintf(b, sizeof b, "%s of %s (%.0f%%)", u, t, pct); ui_text(c, 380, y + 28, b, tf, 0);
	}
	if (!nfs) ui_text(c, 20, 40, "No disks found.", tf, 0);
}
static void key(App *a, KeySym k, const char *t, int n, unsigned s) { if (k == 0xff1b) a->quit = 1; }
int main(void)
{
	App *a = app_new("Disk Manager", "diskmanager", 620, 300, 0);
	a->draw = draw; a->key = key; a->tick = scan; a->tick_ms = 5000; scan(a);
	app_run(a);
	return 0;
}
