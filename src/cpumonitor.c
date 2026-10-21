#define _GNU_SOURCE
#include "sylui.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HN 60
#ifdef MEMMODE
#define TITLE "Memory Monitor"
#else
#define TITLE "CPU Monitor"
#endif
typedef struct { int pid; char name[32]; unsigned long ticks, prev; double cpu; long rss; } Proc;
static Proc procs[2048], *top[12]; static int np, ntop;
static double hist[HN], memp, load[3], up; static unsigned long pt, pi; static long memt, mema, hz, pagek;
static int first = 1;

static int cmpp(const void *a, const void *b)
{
	const Proc *x = *(Proc *const *)a, *y = *(Proc *const *)b;
#ifndef MEMMODE
	if (y->cpu != x->cpu) return y->cpu > x->cpu ? 1 : -1;
#endif
	return y->rss > x->rss ? 1 : -1;
}
static void sample(App *a)
{
	FILE *f = fopen("/proc/stat", "r");
	unsigned long u, n, s, i, w, q, sq, st, total = 0, idle = 0;
	if (f && fscanf(f, "cpu %lu %lu %lu %lu %lu %lu %lu %lu", &u, &n, &s, &i, &w, &q, &sq, &st) == 8) { total = u + n + s + i + w + q + sq + st; idle = i + w; }
	if (f) fclose(f);
	double dt = total - pt, di = idle - pi, cpu = dt > 0 ? 100.0 * (dt - di) / dt : 0;
	unsigned long dtot = total - pt; pt = total; pi = idle;
	f = fopen("/proc/meminfo", "r"); char l[200];
	while (f && fgets(l, sizeof l, f)) { sscanf(l, "MemTotal: %ld", &memt); sscanf(l, "MemAvailable: %ld", &mema); }
	if (f) fclose(f);
	memp = memt ? 100.0 * (memt - mema) / memt : 0;
	memmove(hist, hist + 1, sizeof hist - sizeof *hist);
#ifdef MEMMODE
	hist[HN - 1] = memp; (void)cpu;
#else
	hist[HN - 1] = cpu;
#endif
	f = fopen("/proc/loadavg", "r"); if (f) { fscanf(f, "%lf %lf %lf", &load[0], &load[1], &load[2]); fclose(f); }
	f = fopen("/proc/uptime", "r"); if (f) { fscanf(f, "%lf", &up); fclose(f); }
	/* processes */
	static Proc old[2048]; int nold = np; memcpy(old, procs, sizeof(Proc) * np); np = 0;
	DIR *d = opendir("/proc"); struct dirent *e;
	long ncpu = sysconf(_SC_NPROCESSORS_ONLN); if (ncpu < 1) ncpu = 1;
	while (d && (e = readdir(d)) && np < 2048) {
		int pid = atoi(e->d_name); if (pid <= 0) continue;
		char p[64], buf[1024]; snprintf(p, sizeof p, "/proc/%d/stat", pid);
		FILE *sf = fopen(p, "r"); if (!sf) continue;
		size_t r = fread(buf, 1, sizeof buf - 1, sf); fclose(sf); buf[r] = 0;
		char *lp = strchr(buf, '('), *rp = strrchr(buf, ')'); if (!lp || !rp) continue;
		Proc *pr = &procs[np++]; pr->pid = pid;
		snprintf(pr->name, sizeof pr->name, "%.*s", (int)(rp - lp - 1) < 31 ? (int)(rp - lp - 1) : 31, lp + 1);
		unsigned long ut, stt; long rss;
		sscanf(rp + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %*d %*d %*d %*u %*u %ld", &ut, &stt, &rss);
		pr->ticks = ut + stt; pr->rss = rss * pagek; pr->prev = pr->ticks;
		for (int k = 0; k < nold; k++) if (old[k].pid == pid) { pr->prev = old[k].ticks; break; }
		pr->cpu = dtot > 0 ? 100.0 * (pr->ticks - pr->prev) * ncpu / (double)dtot : 0;
		if (pr->cpu > 100.0 * ncpu) pr->cpu = 100.0 * ncpu;
	}
	if (d) closedir(d);
	static Proc *ptrs[2048];
	for (int k = 0; k < np; k++) ptrs[k] = &procs[k];
	qsort(ptrs, np, sizeof *ptrs, cmpp);
	ntop = np < 12 ? np : 12;
	static Proc snap[12]; for (int k = 0; k < ntop; k++) snap[k] = *ptrs[k];
	for (int k = 0; k < ntop; k++) top[k] = &snap[k];
	first = 0;
}
static void bar(cairo_t *c, int x, int y, int w, double pct, Col k)
{
	ui_rect(c, x, y, w, 14, BGC); ui_bevel(c, x, y, w, 14, 1);
	ui_rect(c, x + 2, y + 2, (w - 4) * pct / 100.0, 10, k);
}
static void draw(App *a)
{
	cairo_t *c = a->cr; int W = a->W; Col fg = col_fg(FACE); char b[96];
	ui_group(c, 8, 4, W - 16, 120, TITLE);
	int gx = 16, gy = 24, gw = W - 32, gh = 92;
	ui_rect(c, gx, gy, gw, gh, HEX(0x0C1A12)); ui_bevel(c, gx - 1, gy - 1, gw + 2, gh + 2, 1);
	ui_set(c, HEX(0x1C4A2C)); cairo_set_line_width(c, 1);
	for (int i = 1; i < 4; i++) { cairo_move_to(c, gx, gy + i * gh / 4 + .5); cairo_line_to(c, gx + gw, gy + i * gh / 4 + .5); } cairo_stroke(c);
	cairo_move_to(c, gx, gy + gh); for (int i = 0; i < HN; i++) cairo_line_to(c, gx + i * (gw - 1) / (double)(HN - 1), gy + gh - gh * hist[i] / 100.0);
	cairo_line_to(c, gx + gw, gy + gh); cairo_close_path(c); cairo_set_source_rgba(c, .3, 1, .5, .25); cairo_fill_preserve(c);
	ui_set(c, HEX(0x50FF80)); cairo_set_line_width(c, 1.4); cairo_stroke(c);
	snprintf(b, sizeof b, "%.0f%%", hist[HN - 1]); ui_text(c, gx + 6, gy + 10, b, HEX(0x90FFB0), 0);
	ui_text(c, 16, 140, "Memory", fg, 0); bar(c, 70, 133, W - 86 - 120, memp, T.col[C_TACT]);
	snprintf(b, sizeof b, "%ld / %ld Mb", (memt - mema) / 1024, memt / 1024); ui_text(c, W - 16 - 118, 140, b, fg, 0);
	int d = (int)up / 86400, h = (int)up % 86400 / 3600, m = (int)up % 3600 / 60;
	snprintf(b, sizeof b, "Load %.2f %.2f %.2f    Up %dd %02dh %02dm", load[0], load[1], load[2], d, h, m); ui_text(c, 16, 164, b, fg, 0);
	ui_rect(c, 8, 180, W - 16, a->H - 188, BGC); ui_bevel(c, 8, 180, W - 16, a->H - 188, 1);
	ui_rect(c, 9, 181, W - 18, 20, FACE); ui_bevel(c, 9, 181, W - 18, 20, 0);
	ui_text(c, 16, 191, "PID", fg, 0); ui_text(c, 70, 191, "Name", fg, 0); ui_text(c, W - 150, 191, "CPU", fg, 0); ui_text(c, W - 90, 191, "Memory", fg, 0);
	Col tf = col_fg(BGC);
	for (int i = 0; i < ntop && 204 + i * 18 < a->H - 12; i++) {
		int y = 213 + i * 18; Proc *p = top[i];
		snprintf(b, sizeof b, "%d", p->pid); ui_text(c, 16, y, b, tf, 0);
		ui_text_fit(c, 70, y, p->name, W - 240, tf, 0);
		snprintf(b, sizeof b, "%.1f%%", p->cpu); ui_text(c, W - 150, y, b, tf, 0);
		snprintf(b, sizeof b, "%.1f Mb", p->rss / 1048576.0); ui_text(c, W - 90, y, b, tf, 0);
	}
}
int main(void)
{
	hz = sysconf(_SC_CLK_TCK); pagek = sysconf(_SC_PAGESIZE);
	App *a = app_new(TITLE, "cpumonitor", 480, 420, 0);
	a->draw = draw; a->tick = sample; a->tick_ms = 1000;
	sample(a); usleep(200000); sample(a);
	app_run(a);
	return 0;
}
