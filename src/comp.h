#ifndef COMP_H
#define COMP_H
#include <X11/Xlib.h>
enum { AN_NONE, AN_FADE, AN_OPEN, AN_MIN, AN_RESTORE, AN_CLOSE };
int comp_init(Display *d, Window root, int W, int H);
int comp_on(void);
void comp_desktop(Pixmap pm);
void comp_resize(int W, int H);
int comp_event(XEvent *e);
void comp_noanim(Window w);
void comp_hint(Window w, int kind, int tx, int ty);
void comp_ghost(Window w, int kind, int tx, int ty);
void comp_morph(Window w, int ox, int oy, int ow, int oh);
void comp_alpha(Window w, double a);
void comp_preview(int on, int x, int y, int w, int h); /* snap outline */
int comp_wait(void);
void comp_frame(void);
void comp_splash(void);
int comp_dump(const char *png);
#endif
