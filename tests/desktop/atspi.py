"""Exercise the Qt workstation through the separate accessibility bus used by screen readers."""

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
            press(back)
            named("Настройки", Atspi.Role.PUSH_BUTTON)
            Path(report).write_text(json.dumps({"bridge":"AT-SPI2","names":names,"selected":"Русский"},
                                              ensure_ascii=False, indent=2)+"\n")
        finally:
            process.terminate()
            try:
                process.wait(3)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    main()
