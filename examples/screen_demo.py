"""Blender's screen layout, to try by hand (bui.components.screen).

    python examples/screen_demo.py            # the layout is kept between runs
    python examples/screen_demo.py --reset    # start from the built-in layout
    python examples/screen_demo.py --shot out.png

Seven editors in a Blender-like arrangement, each with the regions Blender gives it. Try:

Areas
* drag the edge between two areas (Shift: every aligned edge moves; Ctrl: snap to 24ths and to
  other edges; Esc or right-click cancels)
* drag an area's corner (the top-left and bottom-left zones, the right ones are shorter):
    - inward: split it (Ctrl snaps)           - into a neighbor: join
    - into another area: dock it beside it (left/right/top/bottom) or replace it (center)
    - Ctrl+drag: swap two areas               - out of the window: the status bar says so
* right-click an edge, or an area's header: Blender's area menus (split, join, swap, merge edge,
  maximize, focus mode, close ...); click an area's header corner for the same
* Ctrl+Space maximizes the area under the pointer, Ctrl+Alt+Space is focus mode (again: restore)
* the editor-type menu (first button of each header) turns an area into another editor

Regions
* drag a sidebar's or toolbar's inner edge: it resizes (the toolbar snaps to its columns)
* drag it all the way shut: it hides and leaves a tab; click or drag the tab to bring it back
* N toggles the viewport's and node editor's sidebar, T their toolbars (pointer over the area)
* right-click a header: Hide Header, Flip to Bottom ...

Panels (viewport sidebar)
* Shift+click a header pins it (it shows under every tab); drag a panel's grip to reorder
* Ctrl+Wheel switches the tabs

Everything you change is saved (on_change) and restored on the next run.

SPDX-License-Identifier: GPL-2.0-or-later
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bindings", "python"))

from bui.components import (App, AreaSplit, Button, Checkbox, Column, Editor, EnumRow,  # noqa: E402
                            FileBrowser, IconButton, Label, ListView, MenuButton, Node, NodeEditor,
                            NumberSlider, Panel, ProgressBar, Region, Row, Screen, Sidebar,
                            Spacer, TextArea, TreeItem, TreeView, VectorField)

LAYOUT_FILE = os.path.join(tempfile.gettempdir(), "bui_screen_demo.json")


def page(*children):
    return Column(*children, padding=8, spacing=2)


# -- the editors ----------------------------------------------------------------------------------


def viewport(area):
    """3D Viewport: a header, a toolbar, an N-panel with tabs and a pinned panel."""
    transform = Panel("Transform", VectorField("Location", (0.0, 0.0, 1.0)),
                      VectorField("Rotation", (0.0, 0.0, 0.0), precision=1),
                      VectorField("Scale", (1.0, 1.0, 1.0)), category="Item", pinned=True)
    item = Panel("Item", Label("Cube"), Checkbox("Visible", True), category="Item")
    tool = Panel("Active Tool", EnumRow("Mode", ["Select", "Move", "Rotate"], "Move"),
                 NumberSlider("Strength", 0.5), category="Tool")
    view = Panel("View", NumberSlider("Focal Length", 50.0, min=1.0, max=300.0),
                 Checkbox("Lock to Object", False), category="View")
    note = Panel("Try", Label("Shift+click a panel header to pin it"),
                 Label("Drag a grip to reorder"), category="Item")
    return Editor(
        page(Label("3D viewport (the main view)"),
             Label("N: sidebar   T: toolbar"), Button("A button in the main view")),
        header=[MenuButton("View", ["Frame All", "Frame Selected"]),
                MenuButton("Select", ["All", "None", "Invert"]),
                MenuButton("Add", ["Mesh", "Light", "Camera"]), Spacer(),
                Label("Object Mode")],
        toolbar=Region(Column(*[IconButton(i, emboss=True) for i in
                               ("RESTRICT_SELECT_OFF", "OBJECT_ORIGIN", "BRUSH_DATA", "TOOL_SETTINGS")],
                              padding=4), size=40, hotkey="t", overlap=False),
        sidebar=Region(Sidebar(transform, item, tool, view, note), size=280, hotkey="n"))


def node_editor(area):
    a = Node("Group Input", outputs=[("Geometry", "geometry"), ("Size", "float")],
             location=(-260, 0), kind="group")
    b = Node("Set Position", inputs=[("Geometry", "geometry"), ("Offset", "vector")],
             outputs=[("Geometry", "geometry")], location=(0, 0), kind="geometry")
    c = Node("Group Output", inputs=[("Geometry", "geometry")], location=(260, 0), kind="group")
    editor = NodeEditor([a, b, c], links=[(a, 0, b, 0), (b, 0, c, 0)])
    node_panel = Panel("Node", Label("Set Position"), NumberSlider("Offset Z", 0.0, min=-10, max=10),
                       category="Node")
    return Editor(
        editor,
        header=[MenuButton("Add", ["Input", "Geometry", "Utilities"]), Spacer()],
        sidebar=Region(Sidebar(node_panel,
                               Panel("Options", Checkbox("Use Snapping", True), category="Tool")),
                       size=240, hotkey="n"),
        toolbar=Region(Column(IconButton("RESTRICT_SELECT_OFF", emboss=True),
                              IconButton("BRUSH_DATA", emboss=True), padding=4),
                       size=40, hotkey="t", overlap=False))


def outliner(area):
    scene = TreeItem("Scene Collection", TreeItem("Collection",
                     TreeItem("Camera", icon="OUTLINER_OB_CAMERA"),
                     TreeItem("Cube", icon="OUTLINER_OB_MESH"),
                     TreeItem("Light", icon="OUTLINER_OB_LIGHT")), icon="OUTLINER_COLLECTION")
    return Editor(TreeView([scene], rows=14, columns=("hide", "render")),
                  header=[MenuButton("View", ["Expand All", "Collapse All"]), Spacer(),
                          Label("Scene")])


def properties(area):
    def pg(title, *c, **kw):
        return Panel(title, *c, **kw)
    return Editor(
        page(pg("Transform", VectorField("Location", (0.0, 0.0, 0.0)),
                VectorField("Scale", (1.0, 1.0, 1.0))),
             pg("Modifiers", Button("Add Modifier"),
                pg("Subdivision", NumberSlider("Levels", 2, min=0, max=6, integer=True))),
             pg("Visibility", Checkbox("Show in Viewport", True), Checkbox("Show in Renders", True))),
        header=[MenuButton("Edit", ["Copy", "Paste"]), Spacer()])


def timeline(area):
    bar = ProgressBar(0.25, "Frame 25 / 100")
    return Editor(
        page(bar, Row(Button("Play"), Button("Stop"), NumberSlider("Frame", 25, min=1, max=100,
                                                                 integer=True))),
        header=[MenuButton("View", ["Show Seconds"]), Spacer(), Label("Playback")],
        footer=Region(Row(Label("Start 1   End 100"), padding=4), size=26))


def file_browser(area):
    return Editor(FileBrowser(os.path.expanduser("~")),
                  header=[MenuButton("File", ["Refresh"]), Spacer()])


def text_editor(area):
    return Editor(TextArea("# Python\nprint('hello from the text editor')\n"),
                  header=[MenuButton("Text", ["New", "Open"]), Spacer(), Label("Text")])


def spreadsheet(area):
    return Editor(ListView(["Vertex 0", "Vertex 1", "Vertex 2", "Vertex 3", "Vertex 4"], rows=10),
                  header=[Spacer(), Label("Spreadsheet")])


EDITORS = {"3D Viewport": viewport, "Geometry Node Editor": node_editor, "Outliner": outliner,
           "Properties": properties, "Timeline": timeline, "File Browser": file_browser,
           "Text Editor": text_editor, "Spreadsheet": spreadsheet}

# The built-in arrangement (AreaSplit factors: "v" from the left, "h" from the top).
LAYOUT = AreaSplit("h", 0.78,
                   AreaSplit("v", 0.82,
                             AreaSplit("v", 0.55, "3D Viewport", "Geometry Node Editor"),
                             AreaSplit("h", 0.45, "Outliner", "Properties")),
                   AreaSplit("v", 0.5, "Timeline", AreaSplit("v", 0.5, "Text Editor", "Spreadsheet")))


def build(reset: bool = False) -> App:
    status = Label("Drag edges and corners; Ctrl+Space maximizes; N / T toggle regions", align="left")
    saved = None
    if not reset and os.path.exists(LAYOUT_FILE):
        with open(LAYOUT_FILE, encoding="utf-8") as f:
            saved = f.read()

    def on_change(layout):
        with open(LAYOUT_FILE, "w", encoding="utf-8") as f:
            f.write(layout)
        names = ", ".join(a.editor for a in screen.areas)
        status.set("text", "%d areas: %s" % (len(screen.areas), names))

    def on_window(area):
        status.set("text", "dragged out of the window: a host would open '%s' in a window" % area.editor)

    try:
        screen = Screen(EDITORS, LAYOUT, saved=saved, on_change=on_change, on_window=on_window)
    except (KeyError, ValueError):  # a layout from an older version of this example
        screen = Screen(EDITORS, LAYOUT, on_change=on_change, on_window=on_window)
    return App("BUI — Blender's screen", (1500, 900), screen, Column(status, padding=6),
               padding=0, spacing=0)


if __name__ == "__main__":
    app = build(reset="--reset" in sys.argv)
    if "--shot" in sys.argv:  # a PNG of the first frame, for the docs
        path = sys.argv[sys.argv.index("--shot") + 1]
        app.render_raster()
        ppm = app.render_raster().ppm(0, 0, *app.size)
        w, h = app.size
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
        from blender_compare import write_png
        write_png(path, w, h, ppm[ppm.index(b"255" + bytes([10])) + 4:])
    else:
        app.run()
