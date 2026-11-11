"""Themes: bui.set_theme / BUI_THEME switch Blender's Dark and Light presets.

SPDX-License-Identifier: GPL-2.0-or-later
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))

import bui  # noqa: E402  (locates buipy)
import buipy  # noqa: E402
from bui import theme  # noqa: E402
from bui.components import App, Button, Checkbox, NumberSlider, Panel  # noqa: E402


def gray(r, x, y):
    return tuple(r.pixel(x, y)[:3])


def build():
    return App("theme", (260, 200),
               Panel("Render", NumberSlider("Samples", 64, min=1, max=512),
                     Checkbox("Denoise", True), Button("Render Image")))


def test_default_dark():
    assert bui.theme_name() == "dark"
    assert all(abs(a - b) < 1e-6 for a, b in zip(bui.theme_color("region_bg"), (48 / 255,) * 3 + (1.0,)))
    assert len(bui.theme_color("regular.inner")) == 4
    try:
        bui.theme_color("no_such_color")
    except KeyError:
        pass
    else:
        raise AssertionError("unknown key")
    try:
        bui.set_theme("solarized")
    except ValueError:
        pass
    else:
        raise AssertionError("unknown theme")
    assert bui.theme_name() == "dark"


def test_switch_redraws_and_dark_is_unchanged():
    app = build()
    w, h = app.size
    r = app.render_raster(w, h, True)
    dark = [r.pixel(x, y) for y in range(0, h, 3) for x in range(0, w, 3)]
    assert gray(r, w - 2, h - 2) == (48, 48, 48)
    bui.set_theme("light")
    assert bui.theme_name() == "light"
    assert theme.REGION_BG == bui.theme_color("region_bg")
    r = app.render_raster(w, h)  # the switch damaged the whole frame
    assert gray(r, w - 2, h - 2) == (179, 179, 179), gray(r, w - 2, h - 2)
    light = [r.pixel(x, y) for y in range(0, h, 3) for x in range(0, w, 3)]
    assert sum(a != b for a, b in zip(dark, light)) > len(dark) // 2
    bui.set_theme("dark")
    r = app.render_raster(w, h)
    assert [r.pixel(x, y) for y in range(0, h, 3) for x in range(0, w, 3)] == dark
    assert theme.REGION_BG == bui.theme_color("region_bg")


def test_tk_root_follows_theme():
    import bui.tksyntax as tk
    bui.set_theme("light")
    try:
        root = tk.Tk()
        tk.Label(root, text="hi").pack()
        root.update()
        w, h = root._window.size
        r = buipy.Raster(w, h)
        root._ctx.render(w, h, r, True)
        assert gray(r, w - 2, h - 2) == (179, 179, 179), gray(r, w - 2, h - 2)
        root.destroy()
    finally:
        bui.set_theme("dark")


def test_env_var():
    code = "import bui; print(bui.theme_name(), bui.theme_color('region_bg')[0])"
    env = dict(os.environ, BUI_THEME="light")
    out = subprocess.run([sys.executable, "-c", code], env=env, capture_output=True, text=True,
                         cwd=os.path.join(HERE, ".."))
    assert out.returncode == 0, out.stderr
    name, r = out.stdout.split()
    assert name == "light" and abs(float(r) - 179 / 255) < 1e-6, out.stdout


def test_theme_file_and_keys():
    """A theme file: plain-English and Blender keys, short names, numbers, errors."""
    text = """{ "name": "set", "themes": [
      { "name": "Plum", "mode": "dark", "radius": 0.8,
        "colors": { "primary.background": "#112233", "button.background": "#445566",
                    "user_interface.wcol_num.inner": "#0a0b0c", "button.border.width": 3,
                    "button.corners.top_left": false, "nonsense.key": "#ffffff" } } ] }"""
    try:
        added, ignored = buipy.kit_theme_load_json(text)
        assert (added, ignored) == (1, 1)
        assert "Plum" in bui.themes()
        count = len(bui.themes())
        bui.set_theme("plum")
        assert bui.theme_name() == "Plum"
        near = lambda c, rgb: all(abs(a - b / 255) < 2e-3 for a, b in zip(c, rgb))
        assert near(bui.theme_color("button.background"), (0x44, 0x55, 0x66))
        assert near(bui.theme_color("tool_button.background.selected"), (0x11, 0x22, 0x33))
        assert near(bui.theme_color("number_field.background"), (0x0a, 0x0b, 0x0c))
        assert bui.theme_value("button.border.width") == 3
        assert bui.theme_value("button.corners.top_left") == 0
        assert abs(bui.theme_value("slider.roundness") - 0.8) < 1e-6
        # the module's own colors follow (bui.theme.WCOL is what the Python components draw with)
        assert near(theme.WCOL["regular"].inner, (0x44, 0x55, 0x66))
        # single values
        bui.set_theme_color("button.background", "#ff0000")
        assert near(bui.theme_color("button.background"), (255, 0, 0))
        bui.set_theme_color("button.background", (0.0, 1.0, 0.0))
        assert near(bui.theme_color("button.background"), (0, 255, 0))
        bui.set_theme_value("button.roundness", 0.25)
        assert abs(bui.theme_value("button.roundness") - 0.25) < 1e-6
        for bad in (lambda: bui.set_theme_color("button.nope", "#fff"), lambda: bui.theme_value("button.nope")):
            try:
                bad()
            except KeyError:
                pass
            else:
                raise AssertionError("expected KeyError")
        # mistakes say where, and add nothing
        try:
            bui.load_theme('{"themes": [ {"name": "A"}, {} ]}')
        except ValueError as e:
            assert "themes[1]" in str(e), e
        else:
            raise AssertionError("expected ValueError")
        assert len(bui.themes()) == count  # the bad file added none
        try:
            bui.set_theme("no such theme")
        except ValueError:
            pass
        else:
            raise AssertionError("expected ValueError")
        # from a file path
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
            f.write(text.replace("Plum", "FromFile"))
        try:
            assert "FromFile" in bui.load_theme(f.name)
        finally:
            os.unlink(f.name)
    finally:
        bui.set_theme("dark")


def test_example_theme_file():
    """docs/themes/catppuccin.json loads whole (every key it uses exists) and each theme applies."""
    path = os.path.join(HERE, "..", "..", "..", "docs", "themes", "catppuccin.json")
    try:
        added, ignored = buipy.kit_theme_load_file(path)
        assert added == 3 and ignored == 0, (added, ignored)
        for name in ("Catppuccin Mocha", "Catppuccin Latte", "Mocha Flat"):
            assert name in bui.themes()
            bui.set_theme(name)
            assert bui.theme_name() == name
        bui.set_theme("Catppuccin Mocha")
        assert abs(bui.theme_color("region.background")[0] - 0x18 / 255) < 2e-3
        bui.set_theme("Mocha Flat")
        assert bui.theme_value("number_field.corners.top_left") == 0
        assert bui.theme_value("button.border.width") == 2
        assert bui.theme_value("button.emboss.enabled") == 0
    finally:
        bui.set_theme("dark")


def main() -> int:
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        t()
        print("ok", t.__name__)
    print("all theme tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
