#ifndef SYLUI_H
#define SYLUI_H
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

typedef struct { double r, g, b; } Col;
#define HEX(v) ((Col){((v) >> 16 & 255) / 255.0, ((v) >> 8 & 255) / 255.0, ((v) & 255) / 255.0})

enum { C_FACE, C_BG, C_TEXT, C_TACT, C_TINA, C_SEL, C_PANEL, C_DESK, NCOL };

typedef struct {
	Col col[NCOL];
	Col light, shadow, dark;
	Col menubg, selmenu, sbbg, sbknob, focus;
	int ext;
	int decor, clock24, clock_secs;
	char wall[512];
	char font[64], icons[64];
	int fsize, mouse_speed, mouse_left, lock_min, dpms_min, watermark;
} Theme;
extern Theme T;
#define FACE T.col[C_FACE]
#define BGC T.col[C_BG]
#define TEXTC T.col[C_TEXT]

struct Preset { const char *name; unsigned c[NCOL]; };
extern const struct Preset PRESETS[];
extern const int NPRESETS;
extern const char *COLNAMES[NCOL];
extern const char *DECORS[3];

Col col_mix(Col a, Col b, double t);
Col col_fg(Col bg);
void theme_derive(Theme *t);
void theme_preset(Theme *t, int i);
void theme_load(void);
int theme_save(void);
const char *ui_preview_path(void);
int theme_equal(const Theme *a, const Theme *b);
int theme_save_to(const Theme *t, const char *path, int colors_only);
int theme_load_from(Theme *t, const char *path);
const char *ui_cfgdir(void);
const char *ui_datadir(void);
const char *ui_wall_path(const char *wall);
int ui_wall_list(char names[][256], int max);
void ui_edit(char *buf, int cap, KeySym k, const char *t, int n);
int ui_file_icon(const char *name, int dir);
void ui_open(const char *path);
void ui_spawn(const char *cmd);
void ui_notify_theme(Display *d);

#define UI_TERM "xterm -fa Monospace -fs 10 -bg black -fg white -geometry 80x24 -T \"$USER@$(hostname):~ - Syllable Terminal\""

typedef struct { const char *name; int icon; const char *cmd; int sec; } OwnApp;
extern const OwnApp UI_OWN[];
extern const int UI_NOWN;

/* installed applications */
typedef struct { char name[64], exec[400], icon[200]; int section; } AppEnt;
extern const char *UI_SECTIONS[6];
int ui_apps_scan(AppEnt *out, int max);
cairo_surface_t *ui_image_load(const char *path);
cairo_surface_t *ui_icon_load(const char *name, int size);

/* drawing */
void ui_set(cairo_t *c, Col k);
void ui_rect(cairo_t *c, double x, double y, double w, double h, Col k);
void ui_vgrad(cairo_t *c, double x, double y, double w, double h, Col top, Col bot);
enum { FR_RECESSED = 8, FR_RAISED = 16, FR_THIN = 32, FR_ETCHED = 128, FR_FLAT = 256, FR_DISABLED = 512, FR_TRANSPARENT = 0x10000 };
void ui_frame(cairo_t *c, double x, double y, double w, double h, int style, Col erase); /* port of View::DrawFrame */
void ui_bevel(cairo_t *c, double x, double y, double w, double h, int st);
void ui_text_title(cairo_t *c, double x, double cy, const char *s, Col k);
double ui_tw_title(cairo_t *c, const char *s);
void ui_dropdown_ex(cairo_t *c, double x, double y, double w, double h, const char *s, int focus);
Col ui_tint(Col c, double t);
void ui_text(cairo_t *c, double x, double cy, const char *s, Col k, int bold);
void ui_text_c(cairo_t *c, double cx, double cy, const char *s, Col k, int bold);
void ui_text_fit(cairo_t *c, double x, double cy, const char *s, double maxw, Col k, int bold);
double ui_tw(cairo_t *c, const char *s, int bold);
void ui_button(cairo_t *c, double x, double y, double w, double h, const char *label, int state);
void ui_group(cairo_t *c, double x, double y, double w, double h, const char *label);
void ui_field(cairo_t *c, double x, double y, double w, double h, const char *s, int caret);
void ui_dropdown(cairo_t *c, double x, double y, double w, double h, const char *s);
void ui_check(cairo_t *c, double x, double y, const char *label, int on);
void ui_slider(cairo_t *c, double x, double y, double w, int v);
void ui_vscroll(cairo_t *c, double x, double y, double w, double h, double pos, double view, double total);
void ui_vscroll_thumb(double y, double h, double pos, double view, double total, double *ty, double *th);
void ui_arrow(cairo_t *c, double cx, double cy, int dir, double s, Col k);

enum { IC_FOLDER, IC_DISK, IC_HOME, IC_PREFS, IC_TRASH, IC_TERM, IC_APPS, IC_FILE, IC_GLOBE,
       IC_APPEAR, IC_DESKTOP, IC_CLOCK, IC_KEYBOARD, IC_INFO, IC_BACK, IC_UP, IC_REFRESH,
       IC_HOMEBTN, IC_SEARCH, IC_ABOUT, IC_CALC, IC_MON, IC_NOTE, IC_VIEW, IC_CHARMAP, IC_RUN, IC_FONT, IC_MOUSE, IC_SCREEN,
       IC_LOCK, IC_POWER, IC_SUSPEND, IC_RESTART, IC_SPEAKER, IC_MUTE, IC_BATTERY, IC_PLUG, IC_WIFI, IC_NOWIFI, IC_BRIGHT, IC_BELL, IC_IMAGE, IC_AUDIO, IC_ARCHIVE, IC_SCRIPT, IC_PDF, IC_MEM, IC_LOGOUT, IC_WINDOW };
void ui_icon(cairo_t *c, int id, double x, double y, double size);
void ui_icon_dim(cairo_t *c, int id, double x, double y, double size);
void ui_badge(cairo_t *c, double x, double y, double size, const char *name);
void ui_logo(cairo_t *c, double cx, double cy, double r);

#define TB_H 44
void ui_toolbar(cairo_t *c, int W, int back_ok, const char *viewlabel);
int ui_toolbar_hit(int x, int y, int W);

/* app framework */
typedef struct App App;
struct App {
	Display *d; Window w; int scr, W, H, dirty, quit;
	cairo_surface_t *sf; cairo_t *cr; XIC ic; Atom wmdel;
	void (*draw)(App *);
	void (*press)(App *, int x, int y, int b, unsigned long t);
	void (*release)(App *, int x, int y, int b);
	void (*motion)(App *, int x, int y, unsigned st);
	void (*key)(App *, KeySym ks, const char *txt, int len, unsigned st);
	void (*reload)(App *);
	void (*tick)(App *);
	int tick_ms;
	char *clip;
	void *user;
	unsigned long lastt; int lastid;
};
App *app_new(const char *title, const char *cls, int w, int h, int fixed);
void app_title(App *a, const char *t);
void app_redraw(App *a);
void app_run(App *a);
void app_copy(App *a, const char *utf8);
int app_isdbl(App *a, unsigned long t, int id);
#endif
