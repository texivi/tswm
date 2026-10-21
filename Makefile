CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter
PKG = $(shell pkg-config --cflags --libs cairo x11 xrandr gdk-pixbuf-2.0 xcomposite xdamage xfixes xcursor fontconfig xscrnsaver) -lpam
APPS = about preferences files applications aedit calculator cpumonitor memmonitor aview charmap launch sysinfo diskmanager lock powerdialog notifyd
BINS = tswm $(APPS)
COMMON = src/sylui.c src/icons.c src/sylui.h
all: $(addprefix bin/,$(BINS)) walls
bin/memmonitor: src/cpumonitor.c $(COMMON)
	@mkdir -p bin
	$(CC) $(CFLAGS) -DMEMMODE -o $@ $< src/sylui.c src/icons.c $(PKG) -lm
bin/tswm: src/tswm.c src/comp.c src/comp.h $(COMMON)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ src/tswm.c src/comp.c src/sylui.c src/icons.c $(PKG) -lm
bin/notifyd: src/notifyd.c $(COMMON)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $< src/sylui.c src/icons.c $(PKG) $(shell pkg-config --cflags --libs gio-2.0 gio-unix-2.0) -lm
bin/mkwall: src/mkwall.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $< $(shell pkg-config --cflags --libs cairo) -lm
bin/%: src/%.c $(COMMON)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $< src/sylui.c src/icons.c $(PKG) -lm
walls: bin/mkwall
	@mkdir -p data/wallpapers
	./bin/mkwall data/wallpapers
clean:
	rm -rf bin

PREFIX ?= $(HOME)/.local
install: all
	install -Dm755 $(addprefix bin/,$(BINS)) -t $(PREFIX)/bin
	install -d $(PREFIX)/share/tswm/wallpapers $(PREFIX)/share/tswm/fonts
	install -m644 data/fonts/* $(PREFIX)/share/tswm/fonts
	cp -r data/icons $(PREFIX)/share/tswm/
	install -m644 data/wallpapers/* $(PREFIX)/share/tswm/wallpapers
	install -Dm755 run-xephyr $(PREFIX)/bin/tswm-xephyr
	install -Dm755 tswm-session $(PREFIX)/bin/tswm-session
	install -Dm644 tswm.desktop $(PREFIX)/share/xsessions/tswm.desktop
uninstall:
	rm -f $(addprefix $(PREFIX)/bin/,$(BINS)) $(PREFIX)/bin/tswm-xephyr $(PREFIX)/bin/tswm-session
	rm -rf $(PREFIX)/share/tswm
	rm -f $(PREFIX)/share/xsessions/tswm.desktop
.PHONY: all clean walls install uninstall
