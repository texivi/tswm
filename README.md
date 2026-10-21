# TSWM

Texivi's Syllable Window Manager. An X11 window manager and small desktop that copies the look of Syllable Desktop 0.6.6. Written in C with Xlib, cairo and gdk-pixbuf. GPL v3.

![tswm1](screenshots/tswm1.png)

![tswm2](screenshots/tswm2.png)

More in [SCREENSHOTS.md](SCREENSHOTS.md).

## Build

    make
    ./run-xephyr                      # nested test session
    sudo make install PREFIX=/usr     # also adds the TSWM session to your login manager
    make install                      # or ~/.local, start it with startx ~/.local/bin/tswm-session

Needs gcc, make, pkg-config and the development files for cairo, x11, xrandr, gdk-pixbuf, xcomposite, xdamage, xfixes, xcursor, fontconfig, xscrnsaver and pam.

## Notes

- The window frames, dock, menus and widgets follow libsyllable and the Photon decorator. Icons and some wallpapers are taken from the Syllable sources.
- The apps (Files, AEdit, AView, Calculator, CharMap, Launch, Preferences, monitors) are part of the repo and share one toolkit in `src/sylui.c`.
- The dock menu lists every installed program with a `.desktop` file under its Syllable section.
- Alt+Tab switches windows, Alt+F1 to F4 switch desktops, Ctrl+Alt+Backspace reloads the config, Print takes a screenshot.
- Config is `~/.config/tswm/theme.conf`. An executable `~/.config/tswm/autostart` runs at startup.
- `TSWM_NOCOMP=1` turns the compositor off.
- Wallpaper credits are in `data/wallpapers/CREDITS.txt`.
