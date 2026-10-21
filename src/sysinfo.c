#define _GNU_SOURCE
#include "sylui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

static char rows[8][2][160]; static int nr;
static void add(const char *k, const char *fmt, ...)
{
	__builtin_va_list ap; __builtin_va_start(ap, fmt);
	snprintf(rows[nr][0], 160, "%s", k); vsnprintf(rows[nr][1], 160, fmt, ap); nr++;
	__builtin_va_end(ap);
}
static void gather(App *a)
{
	struct utsname u; uname(&u);
	nr = 0;
	add("Operating system", "%s %s", u.sysname, u.release);
	add("Machine", "%s", u.machine);
	add("Host name", "%s", u.nodename);
	char cpu[128] = "unknown", l[256]; int ncpu = 0; long mem = 0;
	FILE *f = fopen("/proc/cpuinfo", "r");
	while (f && fgets(l, sizeof l, f)) { if (!strncmp(l, "model name", 10) && cpu[0] == 'u') { char *p = strchr(l, ':'); if (p) { snprintf(cpu, sizeof cpu, "%s", p + 2); cpu[strcspn(cpu, "\n")] = 0; } } if (!strncmp(l, "processor", 9)) ncpu++; }
	if (f) fclose(f);
	f = fopen("/proc/meminfo", "r"); if (f) { fscanf(f, "MemTotal: %ld", &mem); fclose(f); }
	add("Processor", "%s", cpu); add("Processors", "%d", ncpu);
	add("Memory", "%ld Mb", mem / 1024);
	double up = 0; f = fopen("/proc/uptime", "r"); if (f) { fscanf(f, "%lf", &up); fclose(f); }
	add("Uptime", "%dd %02dh %02dm", (int)up / 86400, (int)up % 86400 / 3600, (int)up % 3600 / 60);
	add("Display", "%dx%d", DisplayWidth(a->d, a->scr), DisplayHeight(a->d, a->scr));
}
static void draw(App *a)
{
	cairo_t *c = a->cr; Col fg = col_fg(FACE);
	ui_icon(c, IC_ABOUT, 14, 12, 40);
	ui_text(c, 66, 24, "System Information", fg, 1); ui_text(c, 66, 42, "TSWM 1.0.0", fg, 0);
	ui_group(c, 8, 64, a->W - 16, a->H - 112, NULL);
	for (int i = 0; i < nr; i++) { ui_text(c, 20, 84 + i * 22, rows[i][0], T.shadow, 0); ui_text_fit(c, 140, 84 + i * 22, rows[i][1], a->W - 160, fg, 0); }
	ui_button(c, a->W - 92, a->H - 38, 80, 26, "Close", 2);
}
static void press(App *a, int x, int y, int b, unsigned long t) { if (b == 1 && x >= a->W - 92 && y >= a->H - 38) a->quit = 1; }
static void key(App *a, KeySym k, const char *t, int n, unsigned s) { if (k == 0xff1b || k == 0xff0d) a->quit = 1; }
int main(void)
{
	App *a = app_new("System Information", "sysinfo", 400, 290, 1);
	a->draw = draw; a->press = press; a->key = key; gather(a);
	app_run(a);
	return 0;
}
