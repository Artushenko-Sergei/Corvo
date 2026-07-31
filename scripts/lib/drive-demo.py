#!/usr/bin/env python3
"""Нажатия по меню Corvo через XTEST на экране :99 - для демонстрационного видео."""
import sys
import time

from Xlib import X, XK, display
from Xlib.ext import xtest


def find_window(dpy, root):
    for child in root.query_tree().children:
        for w in [child] + child.query_tree().children:
            try:
                name = w.get_wm_name()
                cls = w.get_wm_class()
                geom = w.get_geometry()
            except Exception:
                continue
            if name == "Corvo" and cls and geom.width > 500:
                return w
    return None


def key(dpy, name, mods=()):
    code = dpy.keysym_to_keycode(XK.string_to_keysym(name))
    mod_codes = [dpy.keysym_to_keycode(XK.string_to_keysym(m)) for m in mods]
    for m in mod_codes:
        xtest.fake_input(dpy, X.KeyPress, m)
    xtest.fake_input(dpy, X.KeyPress, code)
    dpy.sync()
    time.sleep(0.06)
    xtest.fake_input(dpy, X.KeyRelease, code)
    for m in reversed(mod_codes):
        xtest.fake_input(dpy, X.KeyRelease, m)
    dpy.sync()
    time.sleep(0.45)


def main():
    dpy = display.Display(":99")
    win = find_window(dpy, dpy.screen().root)
    if win is None:
        print("окно Corvo не найдено", file=sys.stderr)
        return 1

    win.set_input_focus(X.RevertToParent, X.CurrentTime)
    win.raise_window()
    dpy.sync()
    time.sleep(3)                       # экран входа с QR-кодом

    for _ in range(2):                  # масштаб +
        key(dpy, "plus", ["Control_L"])
    time.sleep(1.5)
    for _ in range(2):                  # масштаб -
        key(dpy, "minus", ["Control_L"])
    time.sleep(0.8)

    key(dpy, "F10")                     # меню «Файл»
    key(dpy, "Down")
    time.sleep(0.8)
    for _ in range(3):                  # -> «Настройки…»
        key(dpy, "Down")
    key(dpy, "Return")
    time.sleep(4)
    key(dpy, "Escape")
    time.sleep(1)

    key(dpy, "F10")                     # меню «Помощь»
    for _ in range(2):
        key(dpy, "Right")
    key(dpy, "Down")
    time.sleep(0.8)
    key(dpy, "Return")                  # -> «О программе»
    time.sleep(4)
    key(dpy, "Escape")
    time.sleep(1)

    key(dpy, "F11")                     # полный экран
    time.sleep(2.5)
    key(dpy, "F11")
    time.sleep(1.5)
    return 0


if __name__ == "__main__":
    sys.exit(main())
