#define _GNU_SOURCE
#include "sylui.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *XML =
	"<node><interface name='org.freedesktop.Notifications'>"
	"<method name='GetCapabilities'><arg type='as' direction='out'/></method>"
	"<method name='Notify'><arg type='s' name='app_name' direction='in'/><arg type='u' name='replaces_id' direction='in'/><arg type='s' name='app_icon' direction='in'/>"
	"<arg type='s' name='summary' direction='in'/><arg type='s' name='body' direction='in'/><arg type='as' name='actions' direction='in'/>"
	"<arg type='a{sv}' name='hints' direction='in'/><arg type='i' name='expire_timeout' direction='in'/><arg type='u' direction='out'/></method>"
	"<method name='CloseNotification'><arg type='u' name='id' direction='in'/></method>"
	"<method name='GetServerInformation'><arg type='s' direction='out'/><arg type='s' direction='out'/><arg type='s' direction='out'/><arg type='s' direction='out'/></method>"
	"<signal name='NotificationClosed'><arg type='u' name='id'/><arg type='u' name='reason'/></signal>"
	"<signal name='ActionInvoked'><arg type='u' name='id'/><arg type='s' name='action_key'/></signal>"
	"</interface></node>";

typedef struct Note { unsigned id; Window w; char summary[160], body[600], icon[200]; int urgency, h; gint64 expire; struct Note *next; } Note;
static Note *notes; static unsigned next_id = 1;
static Display *dpy; static int scr, SW, SH;
static GDBusConnection *bus;
enum { NW = 340, TOPY = 38 };

static void strip_markup(char *s)
{
	char *o = s; int in = 0;
	for (char *p = s; *p; p++) {
		if (*p == '<') in = 1; else if (*p == '>' && in) in = 0;
		else if (!in) {
			if (!strncmp(p, "&amp;", 5)) { *o++ = '&'; p += 4; } else if (!strncmp(p, "&lt;", 4)) { *o++ = '<'; p += 3; } else if (!strncmp(p, "&gt;", 4)) { *o++ = '>'; p += 3; }
			else if (!strncmp(p, "&quot;", 6)) { *o++ = '"'; p += 5; } else *o++ = *p;
		}
	}
	*o = 0;
}
/* word wrap */
static int wrap(cairo_t *c, const char *text, double w, int max, char lines[][200])
{
	int n = 0; char cur[200] = ""; char buf[700]; snprintf(buf, sizeof buf, "%s", text);
	for (char *tok = strtok(buf, " \n"); tok; tok = strtok(NULL, " \n")) {
		char t[260]; snprintf(t, sizeof t, "%s%s%s", cur, cur[0] ? " " : "", tok);
		if (ui_tw(c, t, 0) > w && cur[0]) { if (n < max) snprintf(lines[n], 200, "%s", cur); n++; snprintf(cur, sizeof cur, "%s", tok); }
		else snprintf(cur, sizeof cur, "%s", t);
	}
	if (cur[0]) { if (n < max) snprintf(lines[n], 200, "%s", cur); n++; }
	return n > max ? max : n;
}
static void draw_note(Note *n)
{
	cairo_surface_t *s = cairo_xlib_surface_create(dpy, n->w, DefaultVisual(dpy, scr), NW, n->h);
	cairo_t *c = cairo_create(s);
	ui_bevel(c, 0, 0, NW, n->h, 0);
	ui_rect(c, 3, 3, 4, n->h - 6, n->urgency >= 2 ? HEX(0xC83030) : T.col[C_TACT]);
	Col fg = col_fg(FACE);
	cairo_surface_t *ic = n->icon[0] ? ui_icon_load(n->icon, 36) : NULL;
	if (ic) { double w = cairo_image_surface_get_width(ic), h = cairo_image_surface_get_height(ic); cairo_save(c); cairo_new_path(c); cairo_translate(c, 14 + (36 - w) / 2, 12 + (36 - h) / 2); cairo_set_source_surface(c, ic, 0, 0); cairo_paint(c); cairo_restore(c); }
	else ui_icon(c, IC_BELL, 14, 12, 36);
	ui_text_fit(c, 58, 20, n->summary, NW - 74, fg, 1);
	char lines[3][200]; int nl = wrap(c, n->body, NW - 74, 3, lines);
	for (int i = 0; i < nl; i++) ui_text(c, 58, 38 + i * 15, lines[i], fg, 0);
	cairo_destroy(c); cairo_surface_destroy(s); XFlush(dpy);
}
static void layout(void)
{
	int y = TOPY;
	for (Note *n = notes; n; n = n->next) { XMoveWindow(dpy, n->w, SW - NW - 8, y); y += n->h + 8; }
}
static void close_note(unsigned id, unsigned reason)
{
	Note **p = &notes;
	while (*p && (*p)->id != id) p = &(*p)->next;
	if (!*p) return;
	Note *n = *p; *p = n->next;
	XDestroyWindow(dpy, n->w); free(n); layout(); XFlush(dpy);
	if (bus) g_dbus_connection_emit_signal(bus, NULL, "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "NotificationClosed", g_variant_new("(uu)", id, reason), NULL);
}
static Note *find(unsigned id) { for (Note *n = notes; n; n = n->next) if (n->id == id) return n; return NULL; }
static void method(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *name, GVariant *params, GDBusMethodInvocation *inv, gpointer data)
{
	if (!strcmp(name, "GetCapabilities")) {
		const char *caps[] = {"body", "icon-static", "persistence", NULL};
		g_dbus_method_invocation_return_value(inv, g_variant_new("(^as)", caps));
	} else if (!strcmp(name, "GetServerInformation")) {
		g_dbus_method_invocation_return_value(inv, g_variant_new("(ssss)", "TSWM notifyd", "tswm", "1.0", "1.2"));
	} else if (!strcmp(name, "CloseNotification")) {
		guint id; g_variant_get(params, "(u)", &id); close_note(id, 3);
		g_dbus_method_invocation_return_value(inv, NULL);
	} else if (!strcmp(name, "Notify")) {
		const char *app, *icon, *summary, *body; guint rid; gint exp; GVariant *hints; GVariantIter *acts;
		g_variant_get(params, "(&su&s&s&sas@a{sv}i)", &app, &rid, &icon, &summary, &body, &acts, &hints, &exp);
		g_variant_iter_free(acts);
		Note *n = rid ? find(rid) : NULL; int fresh = !n;
		if (!n) { n = calloc(1, sizeof *n); n->id = next_id++; }
		snprintf(n->summary, sizeof n->summary, "%s", summary); snprintf(n->body, sizeof n->body, "%s", body); snprintf(n->icon, sizeof n->icon, "%s", icon);
		strip_markup(n->summary); strip_markup(n->body);
		guchar urg = 1; g_variant_lookup(hints, "urgency", "y", &urg); n->urgency = urg;
		g_variant_unref(hints);
		if (!n->icon[0]) { GVariant *v = NULL; (void)v; }
		n->expire = exp == 0 ? 0 : g_get_monotonic_time() + (exp < 0 ? 6000 : exp) * 1000;
		cairo_surface_t *m = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1); cairo_t *mc = cairo_create(m);
		char lines[3][200]; int nl = wrap(mc, n->body, NW - 74, 3, lines); cairo_destroy(mc); cairo_surface_destroy(m);
		n->h = 24 + 18 + nl * 15 + 8; if (n->h < 64) n->h = 64;
		if (fresh) {
			XSetWindowAttributes at = {.override_redirect = True, .background_pixmap = None};
			n->w = XCreateWindow(dpy, DefaultRootWindow(dpy), SW - NW - 8, TOPY, NW, n->h, 0, CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixmap, &at);
			XSelectInput(dpy, n->w, ExposureMask | ButtonPressMask);
			n->next = notes; notes = n; /* newest on top */
			XMapRaised(dpy, n->w);
		} else XResizeWindow(dpy, n->w, NW, n->h);
		layout(); draw_note(n);
		g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", n->id));
	}
}
static gboolean x_events(gint fd, GIOCondition cond, gpointer data)
{
	while (XPending(dpy)) {
		XEvent e; XNextEvent(dpy, &e);
		for (Note *n = notes; n; n = n->next) if (n->w == e.xany.window) {
			if (e.type == Expose && !e.xexpose.count) draw_note(n);
			else if (e.type == ButtonPress) { close_note(n->id, 2); }
			break;
		}
	}
	return TRUE;
}
static gboolean tick(gpointer data)
{
	gint64 now = g_get_monotonic_time();
	for (Note *n = notes; n; n = n->next) if (n->expire && now > n->expire) { close_note(n->id, 1); break; }
	return TRUE;
}
static const GDBusInterfaceVTable vt = {method, NULL, NULL, {0}};
static void on_bus(GDBusConnection *c, const gchar *name, gpointer data)
{
	bus = c;
	GDBusNodeInfo *ni = g_dbus_node_info_new_for_xml(XML, NULL);
	g_dbus_connection_register_object(c, "/org/freedesktop/Notifications", ni->interfaces[0], &vt, NULL, NULL, NULL);
}
static void on_lost(GDBusConnection *c, const gchar *name, gpointer data) { exit(0); } /* name taken */
int main(void)
{
	dpy = XOpenDisplay(NULL); if (!dpy) return 1;
	scr = DefaultScreen(dpy); SW = DisplayWidth(dpy, scr); SH = DisplayHeight(dpy, scr);
	theme_load();
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.Notifications", G_BUS_NAME_OWNER_FLAGS_NONE, on_bus, NULL, on_lost, NULL, NULL);
	g_unix_fd_add(ConnectionNumber(dpy), G_IO_IN, x_events, NULL);
	g_timeout_add(300, tick, NULL);
	g_main_loop_run(loop);
	return 0;
}
