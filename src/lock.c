#define _GNU_SOURCE
#include "sylui.h"
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <math.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char pw[256], msg[80];
static double shake_t = -1;

static int conv(int n, const struct pam_message **m, struct pam_response **r, void *data)
{
	struct pam_response *resp = calloc(n, sizeof *resp);
	for (int i = 0; i < n; i++) if (m[i]->msg_style == PAM_PROMPT_ECHO_OFF || m[i]->msg_style == PAM_PROMPT_ECHO_ON) resp[i].resp = strdup(pw);
	*r = resp; return PAM_SUCCESS;
}
static int auth(const char *user)
{
	struct pam_conv pc = {conv, NULL}; pam_handle_t *h = NULL;
	static const char *svc[] = {"login", "system-auth", "other"};
	for (int i = 0; i < 3; i++) {
		if (pam_start(svc[i], user, &pc, &h) != PAM_SUCCESS) continue;
		int rc = pam_authenticate(h, 0); pam_end(h, rc);
		if (rc == PAM_SUCCESS) return 1;
		if (rc == PAM_AUTH_ERR || rc == PAM_USER_UNKNOWN) return 0; /* hard fail */
	}
	return 0;
}
int main(void)
{
	Display *d = XOpenDisplay(NULL); if (!d) return 1;
	int scr = DefaultScreen(d), W = DisplayWidth(d, scr), H = DisplayHeight(d, scr);
	Window root = RootWindow(d, scr);
	theme_load();
	struct passwd *pwd = getpwuid(getuid()); const char *user = pwd ? pwd->pw_name : "user", *real = pwd && pwd->pw_gecos && *pwd->pw_gecos ? pwd->pw_gecos : user;
	XSetWindowAttributes at = {.override_redirect = True, .background_pixel = BlackPixel(d, scr), .event_mask = KeyPressMask | ExposureMask | ButtonPressMask};
	Window w = XCreateWindow(d, root, 0, 0, W, H, 0, CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixel | CWEventMask, &at);
	Cursor cur = XCreateFontCursor(d, 68);
	XMapRaised(d, w);
	int got = 0;
	for (int i = 0; i < 100 && !got; i++) { /* retry grab */
		if (XGrabKeyboard(d, w, True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess) got = 1; else usleep(20000);
	}
	if (!got) return 1; /* never fake a lock */
	XGrabPointer(d, w, True, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, cur, CurrentTime);
	XIM im = XOpenIM(d, NULL, NULL, NULL);
	XIC ic = im ? XCreateIC(im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, w, XNFocusWindow, w, NULL) : NULL;
	cairo_surface_t *sf = cairo_xlib_surface_create(d, w, DefaultVisual(d, scr), W, H);
	cairo_surface_t *wall = NULL; { const char *p = ui_wall_path(T.wall); if (p) wall = ui_image_load(p); }
	int redraw = 1, fd = ConnectionNumber(d);
	for (;;) {
		if (redraw || shake_t >= 0) {
			cairo_t *c = cairo_create(sf);
			if (wall) { double iw = cairo_image_surface_get_width(wall), ih = cairo_image_surface_get_height(wall), k = fmax(W / iw, H / ih); cairo_save(c); cairo_translate(c, (W - iw * k) / 2, (H - ih * k) / 2); cairo_scale(c, k, k); cairo_set_source_surface(c, wall, 0, 0); cairo_paint(c); cairo_restore(c); }
			else ui_rect(c, 0, 0, W, H, T.col[C_DESK]);
			cairo_set_source_rgba(c, 0, 0, 0, .45); cairo_paint(c);
			double dx = 0; if (shake_t >= 0) { double t = (double)clock() / CLOCKS_PER_SEC - shake_t; if (t > .4) shake_t = -1; else dx = sin(t * 60) * 12 * (1 - t / .4); }
			int bw = 360, bh = 190, bx = (W - bw) / 2 + (int)dx, by = (H - bh) / 2;
			ui_bevel(c, bx, by, bw, bh, 0);
			ui_rect(c, bx + 3, by + 3, bw - 6, 20, T.col[C_TACT]); ui_text(c, bx + 10, by + 13, "Screen locked", col_fg(T.col[C_TACT]), 1);
			ui_logo(c, bx + 52, by + 70, 28);
			ui_text(c, bx + 100, by + 52, real, col_fg(FACE), 1);
			time_t t = time(NULL); char tb[40]; strftime(tb, sizeof tb, "%A, %H:%M", localtime(&t)); ui_text(c, bx + 100, by + 72, tb, T.shadow, 0);
			ui_text(c, bx + 20, by + 112, "Password:", col_fg(FACE), 0);
			char stars[260]; int n = strlen(pw); if (n > 100) n = 100; memset(stars, '*', n); stars[n] = 0;
			ui_field(c, bx + 90, by + 100, bw - 110, 24, stars, 1);
			if (msg[0]) ui_text(c, bx + 90, by + 142, msg, HEX(0xB02020), 1);
			ui_button(c, bx + bw - 100, by + bh - 36, 84, 26, "Unlock", 2);
			cairo_destroy(c); cairo_surface_flush(sf); XFlush(d); redraw = 0;
		}
		struct timeval tv = {0, shake_t >= 0 ? 16000 : 30000000};
		fd_set fs; FD_ZERO(&fs); FD_SET(fd, &fs);
		if (!XPending(d) && select(fd + 1, &fs, NULL, NULL, &tv) == 0) { redraw = 1; continue; }
		while (XPending(d)) {
			XEvent e; XNextEvent(d, &e);
			if (e.type == Expose) redraw = 1;
			else if (e.type == KeyPress) {
				char buf[64]; KeySym ks = 0; int len;
				if (ic) { Status st; len = Xutf8LookupString(ic, &e.xkey, buf, 63, &ks, &st); if (st == XBufferOverflow || st == XLookupNone) len = 0; } else len = XLookupString(&e.xkey, buf, 63, &ks, NULL);
				if (ks == XK_Return || ks == XK_KP_Enter) {
					if (auth(user)) { XUngrabKeyboard(d, CurrentTime); XUngrabPointer(d, CurrentTime); memset(pw, 0, sizeof pw); return 0; }
					memset(pw, 0, sizeof pw); snprintf(msg, sizeof msg, "Incorrect password"); shake_t = (double)clock() / CLOCKS_PER_SEC;
				} else if (ks == XK_Escape) { memset(pw, 0, sizeof pw); msg[0] = 0; }
				else { msg[0] = 0; ui_edit(pw, sizeof pw, ks, buf, len > 0 ? len : 0); }
				redraw = 1;
			}
		}
	}
}
