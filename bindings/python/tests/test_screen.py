"""Screens, areas and regions from Python: the layout built from AreaSplit, areas the user makes
filled by the editor factories, the layout saved and restored, regions and their callbacks, the
editor-type menu, panels pinned and reordered.

SPDX-License-Identifier: GPL-2.0-or-later
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from bui.components import (App, Area, AreaSplit, Button, Column, Editor, Label,  # noqa: E402
                            MenuButton, Panel, Region, Screen, Sidebar, unit)

W, H = 900, 600


def editors(made):
    def viewport(area):
        made.append(("3D Viewport", area))
        side = Sidebar(Panel("Item", Label("x"), category="Item"))
        return Editor(Column(Button("viewport")), header=[MenuButton("View", ["Frame All"])],
                      toolbar=Column(), sidebar=Region(side, size=220, hotkey="n"))

    def outliner(area):
        made.append(("Outliner", area))
        return Editor(Column(Label("outliner")), header=[])

    return {"3D Viewport": viewport, "Outliner": outliner, "Properties": outliner}


def drag(app, x0, y0, x1, y1, mods=None):
    app.motion(x0, y0, mods)
    app.press(x0, y0, mods)
    app.motion((x0 + x1) // 2, (y0 + y1) // 2, mods)
    app.motion(x1, y1, mods)
    app.release(x1, y1, mods)
    app.render_raster()


def test_screen():
    made, changes, windows = [], [], []
    screen = Screen(editors(made),
                    AreaSplit("v", 0.7, "3D Viewport", AreaSplit("h", 0.4, "Outliner", "Properties")),
                    on_change=changes.append, on_window=windows.append)
    app = App("screen", (W, H), screen, padding=0)
    app.render_raster()
    areas = {a.editor: a for a in screen.areas}  # children are in creation order (areabase)
    assert sorted(areas) == ["3D Viewport", "Outliner", "Properties"], sorted(areas)
    view, outl, props = areas["3D Viewport"], areas["Outliner"], areas["Properties"]
    assert view.rect[0] <= 2 and abs(view.rect[2] - 0.7 * W) < 6, view.rect  # 2: the border
    assert outl.rect[0] > view.rect[0] + view.rect[2] - 1
    assert outl.rect[1] < props.rect[1], (outl.rect, props.rect)  # first is on top
    assert abs(outl.rect[3] - 0.4 * H) < 6, outl.rect
    assert len(made) == 3

    # the regions the factory gave: the sidebar 220 wide on the right, the header on top
    side = view.children[2] if len(view.children) > 2 else None
    sidebar = next(c for c in view.children if c.type == "sidebar")
    assert sidebar.rect[2] == round(220 * unit() / 20), sidebar.rect
    assert view.header.rect[1] == view.rect[1]
    del side

    # a corner drag inside the viewport splits it: the new area is filled by its factory
    x, y = view.rect[0] + 3, view.rect[1] + view.rect[3] - 3
    drag(app, x, y, x + 200, y - 10)
    assert len(screen.areas) == 4, len(screen.areas)
    assert sum(1 for e, _ in made if e == "3D Viewport") == 2
    assert changes, "on_change got the layout"
    saved = screen.layout
    saved_editors = [a.editor for a in screen.areas]
    assert '"bui_screen": 1' in saved

    # the region callbacks: drag the sidebar narrower
    resized = []
    sidebar.on_resize = lambda size, hidden: resized.append((size, hidden))
    sx, sy = sidebar.rect[0] + 1, sidebar.rect[1] + sidebar.rect[3] // 2
    drag(app, sx, sy, sx + 40, sy)
    assert resized and not resized[-1][1], resized

    # the editor-type menu switches an area to another editor (its factory fills it)
    n = len(made)
    outl._pick("3D Viewport")
    assert outl.editor == "3D Viewport" and len(made) == n + 1

    # restoring the layout: the areas come back
    two = Screen(editors([]), "Outliner")
    app2 = App("two", (W, H), two, padding=0)
    app2.render_raster()
    two.layout = saved
    app2.render_raster()
    assert [a.editor for a in two.areas] == saved_editors, ([a.editor for a in two.areas], saved_editors)
    try:
        two.layout = "{}"
    except KeyError:
        pass
    else:
        raise AssertionError("a malformed layout was accepted")

    # operations from Python
    a, b = screen.areas[0], screen.areas[1]
    c = a.split("h", 0.5)
    assert isinstance(c, Area) and c.editor == a.editor
    assert c.close()
    a.maximize()
    app.render_raster()
    assert a.full == "maximized" and a.rect[2] == W
    a.restore()
    assert b.dock(a, "left", 0.3)

    # dragging an area out of the window: on_window
    app.render_raster()
    last = screen.areas[-1]
    lx, ly = last.rect[0] + 3, last.rect[1] + 3
    drag(app, lx, ly, W + 200, ly)
    assert windows and isinstance(windows[-1], Area), windows


def test_lone_area_and_panels():
    moved, pins = [], []
    panels = [Panel("A", Label("a"), category="Item", on_move=lambda f, t: moved.append((f, t)),
                    on_pin=pins.append),
              Panel("B", Label("b"), category="Item"),
              Panel("C", Label("c"), category="Tool")]
    area = Area("Properties", Column(Label("main")), header=[MenuButton("View", ["All"])],
                sidebar=Region(Sidebar(*panels), size=300, side="left"))
    app = App("area", (700, 500), area, padding=0)
    app.render_raster()
    sidebar = next(c for c in area.children if c.type == "sidebar")
    assert sidebar.rect[0] == 0 and sidebar.rect[2] == round(300 * unit() / 20), sidebar.rect
    assert area.header.rect[3] == round(26 * unit() / 20)
    try:
        Region(Sidebar(), side="top")._place("sidebar")
    except KeyError:
        pass
    else:
        raise AssertionError("a sidebar on top was accepted")
    a = panels[0]
    app.press(a.rect[0] + 40, a.rect[1] + 5, 1)  # Shift+click pins
    app.release(a.rect[0] + 40, a.rect[1] + 5, 1)
    assert a.pinned and pins == [True]
    a.pinned = False
    app.render_raster()
    gx, gy = a.rect[0] + a.rect[2] - 8, a.rect[1] + 8
    b = panels[1]
    drag(app, gx, gy, gx, b.rect[1] + b.rect[3] - 2)
    assert moved == [(0, 1)], moved


def main():
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
    print("ok")


if __name__ == "__main__":
    main()
