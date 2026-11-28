"""Exercise the Qt workstation through the separate accessibility bus used by screen readers."""

import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib


def children(node):
    for index in range(node.get_child_count()):
        child = node.get_child_at_index(index)
        if child:
            child.clear_cache()
            yield child
            yield from children(child)


def wait(probe):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        while GLib.MainContext.default().iteration(False):
            pass
        try:
            result = probe()
            if result:
                return result
        except GLib.Error:
            pass
        time.sleep(.04)
    raise RuntimeError(f"AT-SPI timeout in {probe.__name__}")


def russian_shortcuts(named):
    x = ctypes.CDLL("libX11.so.6")
    xtest = ctypes.CDLL("libXtst.so.6")
    x.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x.XOpenDisplay.restype = ctypes.c_void_p
    display = x.XOpenDisplay(None)
    x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    x.XkbLockGroup.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint]
    x.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
    x.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xtest.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    try:
        x.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        x.XDefaultRootWindow.restype = ctypes.c_ulong
        x.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                                 ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                                 ctypes.POINTER(ctypes.c_uint)]
        x.XFree.argtypes = [ctypes.c_void_p]
        x.XFetchName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_char_p)]
        x.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        def focus_window():
            root, parent, count = ctypes.c_ulong(), ctypes.c_ulong(), ctypes.c_uint()
            windows = ctypes.POINTER(ctypes.c_ulong)()
            x.XQueryTree(display, x.XDefaultRootWindow(display), ctypes.byref(root), ctypes.byref(parent),
                         ctypes.byref(windows), ctypes.byref(count))
            try:
                for window in windows[:count.value]:
                    title = ctypes.c_char_p()
                    if x.XFetchName(display, window, ctypes.byref(title)):
                        matched = b"Component catalog |" in title.value
                        x.XFree(title)
                        if matched:
                            x.XSetInputFocus(display, window, 2, 0)
                            x.XSync(display, False)
                            return True
            finally:
                x.XFree(windows)
        wait(focus_window)
        subprocess.run(["setxkbmap", "-layout", "us,ru"], check=True)
        codes = {key: x.XKeysymToKeycode(display, symbol) for key, symbol in
                 {"ctrl": 0xffe3, "k": ord("k"), "f": ord("f"), "escape": 0xff1b}.items()}
        assert x.XkbLockGroup(display, 0x0100, 1)
        x.XSync(display, False)
        def stroke(*keys):
            for key in keys:
                xtest.XTestFakeKeyEvent(display, codes[key], True, 0)
            for key in reversed(keys):
                xtest.XTestFakeKeyEvent(display, codes[key], False, 0)
            x.XSync(display, False)
        def focused(node):
            node.clear_cache()
            return node.get_state_set().contains(Atspi.StateType.FOCUSED)
        stroke("ctrl", "f")
        wait(lambda: focused(named("Search by name or ID", Atspi.Role.TEXT)))
        stroke("ctrl", "k")
        wait(lambda: focused(named("Search commands and objects", Atspi.Role.TEXT)))
        stroke("escape")
    finally:
        x.XkbLockGroup(display, 0x0100, 0)
        x.XCloseDisplay(display)


def main():
    application, report = sys.argv[1:]
    with tempfile.TemporaryDirectory() as config, subprocess.Popen(
        [application], env={**os.environ, "XDG_CONFIG_HOME": config}
    ) as process:
        try:
            desktop = Atspi.get_desktop(0)
            def application_node():
                for index in range(desktop.get_child_count()):
                    app = desktop.get_child_at_index(index)
                    if app and app.get_process_id() == process.pid:
                        return app
            app = wait(application_node)
            def named(name, role):
                def find():
                    return next((n for n in children(app) if n.get_name() == name and n.get_role() == role), None)
                find.__name__ = name
                return wait(find)
            def press(control):
                actions = control.get_action_iface()
                names = [actions.get_action_name(i) for i in range(actions.get_n_actions())] if actions else []
                index = next((names.index(n) for n in ("Press", "press", "click", "Click") if n in names), 0)
                if not actions or not actions.do_action(index):
                    raise RuntimeError(f"No accessible action on {control.get_name()}")
            announcements = []
            def announced(event):
                announcements.append(str(event.any_data))
            listener = Atspi.EventListener.new(announced)
            listener.register("object:announcement")

            russian_shortcuts(named)

            def open_point():
                row = named("Freight yard", Atspi.Role.LIST_ITEM)
                press(row)
                return True
            wait(open_point)
            press(named("Command palette", Atspi.Role.PUSH_BUTTON))
            named("Search commands and objects", Atspi.Role.TEXT)
            wait(lambda: any("results." in message for message in announcements))
            press(named("Close", Atspi.Role.PUSH_BUTTON))
            press(named("Camera actions", Atspi.Role.PUSH_BUTTON))
            selected = named("Full screen", Atspi.Role.MENU_ITEM)
            def selected_sector():
                node = named("Full screen", Atspi.Role.MENU_ITEM)
                node.clear_cache()
                return node.get_state_set().contains(Atspi.StateType.SELECTED)
            wait(selected_sector)
            wait(lambda: "Full screen" in announcements)
            press(selected)
            listener.deregister("object:announcement")
            press(named("Settings", Atspi.Role.PUSH_BUTTON))
            en = named("English", Atspi.Role.RADIO_BUTTON)
            ru = named("Русский", Atspi.Role.RADIO_BUTTON)
            def checked(node):
                node.clear_cache()
                return node.get_state_set().contains(Atspi.StateType.CHECKED)
            wait(lambda: checked(en))
            press(ru)
            wait(lambda: checked(ru) and not checked(en))
            back = named("Назад", Atspi.Role.PUSH_BUTTON)
            names = [node.get_name() for node in children(app) if node.get_name()]
            for expected in ("Язык интерфейса", "Тема", "Размер текста"):
                if expected not in names:
                    raise RuntimeError(f"Missing {expected}: {names}")
            shortcuts = named("Сочетания клавиш", Atspi.Role.PUSH_BUTTON)
            press(shortcuts)
            named("Сочетания клавиш", Atspi.Role.TABLE)
            editor = named("Изменить сочетание для действия «Палитра команд»", Atspi.Role.PUSH_BUTTON)
            press(editor)
            def recording():
                editor.clear_cache()
                return editor.get_description() == "Нажмите сочетание клавиш"
            wait(recording)
            press(shortcuts)
            press(back)
            named("Настройки", Atspi.Role.PUSH_BUTTON)
            Path(report).write_text(json.dumps({"bridge":"AT-SPI2","names":names,"selected":"Русский","announcements":announcements},
                                              ensure_ascii=False, indent=2)+"\n")
        finally:
            process.terminate()
            try:
                process.wait(3)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    main()
