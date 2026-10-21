#define _GNU_SOURCE
#include "comp.h"
#include "sylui.h"
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct CW {
	Window id; int x, y, w, h, bw, mapped, noanim;
	Visual *vis; Damage dmg; Pixmap pix; cairo_surface_t *surf;
	double alpha, talpha;
	int an, ax, ay; double t0;
	int morph, mx, my, mw, mh; double mt0;
	struct CW *next;
} CW;
typedef struct Ghost { cairo_surface_t *img; int x, y, w, h, kind, tx, ty; double t0; struct Ghost *next; } Ghost;
static struct { Window w; int kind, tx, ty, noanim; } hints[64]; static int nhints;

static Display *dpy; static Window root, overlay;
static int SW, SH, on, dmg_base, dirty = 1;
static CW *list; static Ghost *ghosts;
static cairo_surface_t *front, *back, *desk; static Pixmap backpm;
static struct { int on, x, y, w, h; } snapprev;
static double splash_t0 = -1, slow = 1; /* debug slowdown */

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static double ease(double t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1 - pow(1 - t, 3); }
static CW *find(Window w) { for (CW *c = list; c; c = c->next) if (c->id == w) return c; return NULL; }
static int hint_slot(Window w, int create)
{
	for (int i = 0; i < nhints; i++) if (hints[i].w == w) return i;
	if (!create) return -1;
	if (nhints == 64) { memmove(hints, hints + 1, 63 * sizeof hints[0]); nhints--; }
	memset(&hints[nhints], 0, sizeof hints[0]); hints[nhints].w = w; return nhints++;
}
void comp_noanim(Window w) { hints[hint_slot(w, 1)].noanim = 1; }
void comp_hint(Window w, int kind, int tx, int ty)
{
	CW *c = find(w);
	if (c && c->mapped) return;
	int i = hint_slot(w, 1); hints[i].kind = kind; hints[i].tx = tx; hints[i].ty = ty;
}
int comp_on(void) { return on; }

static void drop_pix(CW *c)
{
	if (c->surf) { cairo_surface_destroy(c->surf); c->surf = NULL; }
	if (c->pix) { XFreePixmap(dpy, c->pix); c->pix = 0; }
}
static void name_pix(CW *c)
{
	drop_pix(c);
	if (!c->mapped || c->w <= 0 || c->h <= 0) return;
	c->pix = XCompositeNameWindowPixmap(dpy, c->id);
	c->surf = cairo_xlib_surface_create(dpy, c->pix, c->vis, c->w + 2 * c->bw, c->h + 2 * c->bw);
}
static void mapped_now(CW *c)
{
	c->mapped = 1; name_pix(c);
	c->an = AN_NONE; c->alpha = c->talpha = 1;
	int i = hint_slot(c->id, 0);
	if (i >= 0 && !hints[i].noanim && hints[i].kind) {
		c->an = hints[i].kind; c->ax = hints[i].tx; c->ay = hints[i].ty; c->t0 = now();
		if (c->an == AN_FADE) c->alpha = 0;
		hints[i].kind = 0;
	}
	dirty = 1;
}
static CW *add(Window w)
{
	if (w == overlay || find(w)) return NULL;
	XWindowAttributes wa;
	if (!XGetWindowAttributes(dpy, w, &wa) || wa.class == InputOnly) return NULL;
	CW *c = calloc(1, sizeof *c);
	c->id = w; c->x = wa.x; c->y = wa.y; c->w = wa.width; c->h = wa.height; c->bw = wa.border_width; c->vis = wa.visual;
	c->alpha = c->talpha = 1;
	int i = hint_slot(w, 0); c->noanim = i >= 0 && hints[i].noanim;
	if (wa.override_redirect && (i < 0 || !hints[i].kind) && !c->noanim && wa.map_state != IsViewable) comp_hint(w, AN_FADE, 0, 0);
	c->dmg = XDamageCreate(dpy, w, XDamageReportNonEmpty);
	CW **p = &list; while (*p) p = &(*p)->next; *p = c;
	if (wa.map_state == IsViewable) mapped_now(c);
	return c;
}
static void del(CW *c)
{
	CW **p = &list; while (*p && *p != c) p = &(*p)->next;
	if (*p) *p = c->next;
	drop_pix(c); free(c);
}

int comp_init(Display *d, Window r, int W, int H)
{
	int ev, er, ma, mi;
	if (getenv("TSWM_NOCOMP") || !XCompositeQueryExtension(d, &ev, &er) || !XDamageQueryExtension(d, &dmg_base, &er)) return 0;
	XCompositeQueryVersion(d, &ma, &mi);
	if (ma == 0 && mi < 4) return 0;
	dpy = d; root = r; SW = W; SH = H;
	if (getenv("TSWM_SLOW")) slow = atof(getenv("TSWM_SLOW"));
	XCompositeRedirectSubwindows(dpy, root, CompositeRedirectManual);
	overlay = XCompositeGetOverlayWindow(dpy, root);
	XserverRegion empty = XFixesCreateRegion(dpy, NULL, 0);
	XFixesSetWindowShapeRegion(dpy, overlay, ShapeInput, 0, 0, empty);
	XFixesDestroyRegion(dpy, empty);
	int scr = DefaultScreen(dpy);
	front = cairo_xlib_surface_create(dpy, overlay, DefaultVisual(dpy, scr), SW, SH);
	backpm = XCreatePixmap(dpy, root, SW, SH, DefaultDepth(dpy, scr));
	back = cairo_xlib_surface_create(dpy, backpm, DefaultVisual(dpy, scr), SW, SH);
	Window rr, pp, *kids; unsigned nk;
	if (XQueryTree(dpy, root, &rr, &pp, &kids, &nk)) { for (unsigned i = 0; i < nk; i++) add(kids[i]); XFree(kids); }
	on = 1;
	return 1;
}
void comp_desktop(Pixmap pm)
{
	if (!on) return;
	if (desk) cairo_surface_destroy(desk);
	desk = cairo_xlib_surface_create(dpy, pm, DefaultVisual(dpy, DefaultScreen(dpy)), SW, SH);
	dirty = 1;
}
void comp_resize(int W, int H)
{
	if (!on) return;
	SW = W; SH = H;
	int scr = DefaultScreen(dpy);
	cairo_surface_destroy(front); cairo_surface_destroy(back); XFreePixmap(dpy, backpm);
	front = cairo_xlib_surface_create(dpy, overlay, DefaultVisual(dpy, scr), SW, SH);
	backpm = XCreatePixmap(dpy, root, SW, SH, DefaultDepth(dpy, scr));
	back = cairo_xlib_surface_create(dpy, backpm, DefaultVisual(dpy, scr), SW, SH);
	dirty = 1;
}

int comp_event(XEvent *e)
{
	if (!on) return 0;
	if (e->type == dmg_base + XDamageNotify) {
		XDamageNotifyEvent *d = (XDamageNotifyEvent *)e;
		CW *c = find(d->drawable);
		if (c) XDamageSubtract(dpy, c->dmg, None, None);
		dirty = 1; return 1;
	}
	/* root children only */
	switch (e->type) {
	case MapNotify: if (e->xmap.event != root) return 0; break;
	case UnmapNotify: if (e->xunmap.event != root) return 0; break;
	case DestroyNotify: if (e->xdestroywindow.event != root) return 0; break;
	case ConfigureNotify: if (e->xconfigure.event != root) return 0; break;
	case ReparentNotify: if (e->xreparent.event != root) return 0; break;
	}
	switch (e->type) {
	case CreateNotify: if (e->xcreatewindow.parent == root) add(e->xcreatewindow.window); break;
	case DestroyNotify: { CW *c = find(e->xdestroywindow.window); if (c) { del(c); dirty = 1; } break; }
	case MapNotify: { CW *c = find(e->xmap.window); if (!c) c = add(e->xmap.window); else if (!c->mapped) mapped_now(c); break; }
	case UnmapNotify: { CW *c = find(e->xunmap.window); if (c && c->mapped) { c->mapped = 0; drop_pix(c); dirty = 1; } break; }
	case ReparentNotify: {
		CW *c = find(e->xreparent.window);
		if (e->xreparent.parent != root) { if (c) { del(c); dirty = 1; } }
		else if (!c) add(e->xreparent.window);
		break;
	}
	case ConfigureNotify: {
		XConfigureEvent *x = &e->xconfigure;
		if (x->window == root) break;
		CW *c = find(x->window); if (!c) break;
		int resized = c->w != x->width || c->h != x->height || c->bw != x->border_width;
		c->x = x->x; c->y = x->y; c->w = x->width; c->h = x->height; c->bw = x->border_width;
		if (resized && c->mapped) name_pix(c);
		/* restack above x->above */
		CW **p = &list; while (*p && *p != c) p = &(*p)->next;
		*p = c->next;
		if (x->above == None) { c->next = list; list = c; }
		else { CW *a = find(x->above); if (a) { c->next = a->next; a->next = c; } else { CW **t = &list; while (*t) t = &(*t)->next; c->next = NULL; *t = c; } }
		dirty = 1; break;
	}
	}
	return 0;
}

void comp_ghost(Window w, int kind, int tx, int ty)
{
	CW *c = find(w);
	if (!on || !c || !c->mapped || !c->surf || c->noanim) return;
	Ghost *g = calloc(1, sizeof *g);
	g->w = c->w + 2 * c->bw; g->h = c->h + 2 * c->bw; g->x = c->x; g->y = c->y; g->kind = kind; g->tx = tx; g->ty = ty; g->t0 = now();
	g->img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, g->w, g->h);
	cairo_t *cr = cairo_create(g->img); cairo_set_source_surface(cr, c->surf, 0, 0); cairo_paint(cr); cairo_destroy(cr);
	g->next = ghosts; ghosts = g; dirty = 1;
}
void comp_morph(Window w, int ox, int oy, int ow, int oh)
{
	CW *c = find(w); if (!on || !c) return;
	c->morph = 1; c->mx = ox; c->my = oy; c->mw = ow + 0; c->mh = oh; c->mt0 = now(); dirty = 1;
}
void comp_preview(int on, int x, int y, int w, int h) { snapprev.on = on; snapprev.x = x; snapprev.y = y; snapprev.w = w; snapprev.h = h; dirty = 1; }
void comp_alpha(Window w, double a) { CW *c = find(w); if (c) { c->talpha = a; dirty = 1; } }

static void rrect(cairo_t *c, double x, double y, double w, double h, double r)
{
	cairo_new_sub_path(c);
	cairo_arc(c, x + w - r, y + r, r, -M_PI / 2, 0); cairo_arc(c, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(c, x + r, y + h - r, r, M_PI / 2, M_PI); cairo_arc(c, x + r, y + r, r, M_PI, 1.5 * M_PI); cairo_close_path(c);
}
static void shadow(cairo_t *c, double w, double h, double a)
{
	cairo_save(c);
	cairo_set_fill_rule(c, CAIRO_FILL_RULE_EVEN_ODD); /* outside only */
	cairo_rectangle(c, -40, -40, w + 80, h + 80); cairo_rectangle(c, 0, 0, w, h); cairo_clip(c);
	cairo_set_fill_rule(c, CAIRO_FILL_RULE_WINDING);
	for (int i = 9; i >= 1; i--) { rrect(c, -i, -i + 4, w + 2 * i, h + 2 * i, i + 2); cairo_set_source_rgba(c, 0, 0, 0, a * .045); cairo_fill(c); }
	cairo_restore(c);
}
static void blit(cairo_t *c, cairo_surface_t *s, double cx, double cy, double w, double h, double scale, double alpha, int sh)
{
	cairo_save(c); cairo_new_path(c);
	cairo_translate(c, cx, cy); cairo_scale(c, scale, scale); cairo_translate(c, -w / 2, -h / 2);
	if (sh) shadow(c, w, h, alpha);
	cairo_set_source_surface(c, s, 0, 0); cairo_paint_with_alpha(c, alpha);
	cairo_restore(c);
}
static int animating(void)
{
	if (ghosts || splash_t0 >= 0) return 1;
	for (CW *c = list; c; c = c->next) if (c->an || c->morph || fabs(c->alpha - c->talpha) > .005) return 1;
	return 0;
}
void comp_splash(void) { if (on) { splash_t0 = now(); dirty = 1; } }
int comp_wait(void) { return !on ? -1 : (dirty ? 0 : animating() ? 12 : -1); }

void comp_frame(void)
{
	if (!on || (!dirty && !animating())) return;
	double t = now();
	cairo_t *c = cairo_create(back);
	if (desk) { cairo_set_source_surface(c, desk, 0, 0); cairo_paint(c); }
	else { cairo_set_source_rgb(c, .2, .4, .65); cairo_paint(c); }
	for (CW *w = list; w; w = w->next) {
		if (!w->mapped || !w->surf) continue;
		if (fabs(w->alpha - w->talpha) > .005) w->alpha += (w->talpha - w->alpha) * .35; else w->alpha = w->talpha;
		double fw = w->w + 2 * w->bw, fh = w->h + 2 * w->bw, x = w->x, y = w->y, rw = fw, rh = fh, scale = 1, al = w->alpha, cx, cy;
		if (w->morph) {
			double p = ease((t - w->mt0) / (slow * .22));
			x = w->mx + (w->x - w->mx) * p; y = w->my + (w->y - w->my) * p; rw = w->mw + (fw - w->mw) * p; rh = w->mh + (fh - w->mh) * p;
			if (p >= 1) w->morph = 0;
		}
		cx = x + rw / 2; cy = y + rh / 2;
		double sx = rw / fw, sy = rh / fh;
		if (w->an) {
			double dur = slow * (w->an == AN_FADE ? .16 : w->an == AN_RESTORE ? .24 : .17), p = ease((t - w->t0) / dur);
			double lin = (t - w->t0) / dur; lin = lin < 0 ? 0 : lin > 1 ? 1 : lin;
			if (w->an == AN_OPEN) { scale = .90 + .10 * p; al *= lin; }
			else if (w->an == AN_FADE) al *= lin;
			else if (w->an == AN_RESTORE) { scale = .12 + .88 * p; al *= lin; cx = w->ax + (cx - w->ax) * p; cy = w->ay + (cy - w->ay) * p; }
			if ((t - w->t0) / dur >= 1) w->an = AN_NONE;
		}
		cairo_save(c); cairo_new_path(c);
		cairo_translate(c, cx, cy); cairo_scale(c, scale * sx, scale * sy); cairo_translate(c, -fw / 2, -fh / 2);
		if (!w->noanim) shadow(c, fw, fh, al);
		cairo_set_source_surface(c, w->surf, 0, 0); cairo_paint_with_alpha(c, al);
		cairo_restore(c);
	}
	Ghost **gp = &ghosts;
	while (*gp) {
		Ghost *g = *gp; double dur = slow * (g->kind == AN_MIN ? .24 : g->kind == AN_CLOSE ? .16 : .12), p = (t - g->t0) / dur;
		if (p >= 1) { *gp = g->next; cairo_surface_destroy(g->img); free(g); continue; }
		double e = ease(p), cx = g->x + g->w / 2.0, cy = g->y + g->h / 2.0, scale = 1, al = 1 - p;
		if (g->kind == AN_CLOSE) scale = 1 - .08 * e;
		else if (g->kind == AN_MIN) { scale = 1 - .88 * e; cx += (g->tx - cx) * e; cy += (g->ty - cy) * e; }
		blit(c, g->img, cx, cy, g->w, g->h, scale, al, 1);
		gp = &g->next;
	}
	if (snapprev.on) {
		cairo_set_source_rgba(c, T.col[C_SEL].r, T.col[C_SEL].g, T.col[C_SEL].b, .28); rrect(c, snapprev.x, snapprev.y, snapprev.w, snapprev.h, 5); cairo_fill_preserve(c);
		cairo_set_source_rgba(c, T.col[C_SEL].r, T.col[C_SEL].g, T.col[C_SEL].b, .9); cairo_set_line_width(c, 2); cairo_stroke(c);
	}
	if (splash_t0 >= 0) {
		double p = (t - splash_t0) / 1.4;
		if (p >= 1) splash_t0 = -1;
		else {
			double a = 1 - ease((p - .25) / .75); if (p < .25) a = 1;
			cairo_set_source_rgba(c, .03, .06, .12, a); cairo_paint(c);
			cairo_save(c); cairo_push_group(c);
			ui_logo(c, SW / 2, SH / 2 - 10, 46 + 10 * p);
			ui_text_c(c, SW / 2, SH / 2 + 62, "TSWM", HEX(0xF4A010), 1);
			cairo_pop_group_to_source(c); cairo_paint_with_alpha(c, a); cairo_restore(c);
		}
	}
	cairo_destroy(c);
	cairo_t *f = cairo_create(front); cairo_set_source_surface(f, back, 0, 0); cairo_set_operator(f, CAIRO_OPERATOR_SOURCE); cairo_paint(f); cairo_destroy(f);
	cairo_surface_flush(front); XFlush(dpy);
	dirty = 0;
}
int comp_dump(const char *png)
{
	if (!on) return -1;
	if (getenv("TSWM_DEBUG")) for (CW *c = list; c; c = c->next) fprintf(stderr, "cw %lx %d,%d %dx%d mapped=%d surf=%d alpha=%.2f an=%d\n", c->id, c->x, c->y, c->w, c->h, c->mapped, c->surf != NULL, c->alpha, c->an);
	return cairo_surface_write_to_png(back, png) == CAIRO_STATUS_SUCCESS ? 0 : -1;
}
