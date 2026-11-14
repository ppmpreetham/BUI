"""Blender's screen: areas and the regions inside them (``kit/intern/screen``).

* :class:`Screen` — areas on Blender's graph of shared edges. Drag an edge between areas to
  resize them (Shift moves every aligned edge, Ctrl snaps, Escape or right-click cancels); drag
  an area's corner to split it, join it into a neighbor, dock it beside or into any other area,
  or out of the window; Ctrl-drag a corner to swap two areas. Right-click an edge or a header
  for the same as a menu. Ctrl+Space maximizes the area under the pointer, Ctrl+Alt+Space is
  focus mode.
* :class:`Area` — one editor: its main view and its regions (header, toolbar, sidebar, ...).
  It works on its own too, as a panel whose sidebars resize and hide.
* :class:`Editor` — what an editor shows: the main view and the regions around it.
* :class:`Region` — a component placed as a region, with its size, side and hotkey.
* :class:`AreaSplit` — a screen's starting layout: ``AreaSplit("v", 0.7, "3D Viewport",
  AreaSplit("h", 0.4, "Outliner", "Properties"))``.

Areas the user makes (a split, a dock, a restored layout) are filled by the screen's
``editors``: one factory per editor name, ``factory(area) -> Editor``::

    def viewport(area):
        return Editor(Viewport3D(), header=[MenuButton("View", [...])],
                      toolbar=Toolbar(), sidebar=Region(Sidebar(...), size=280, hotkey="n"))

    Screen({"3D Viewport": viewport, "Properties": properties},
           AreaSplit("v", 0.75, "3D Viewport", "Properties"),
           saved=load(), on_change=save)

SPDX-License-Identifier: GPL-2.0-or-later
"""

from __future__ import annotations

from typing import Callable, Dict, Optional, Sequence, Union

import buipy

from bui.components.core import EV_CHANGE, Component

AREA_NEW, AREA_CLOSE, AREA_WINDOW = 1, 2, 3  # BK_AREA_*
DOCK_LEFT, DOCK_RIGHT, DOCK_TOP, DOCK_BOTTOM, DOCK_CENTER = range(5)  # BKDock
_DOCK = {"left": DOCK_LEFT, "right": DOCK_RIGHT, "top": DOCK_TOP, "bottom": DOCK_BOTTOM,
         "center": DOCK_CENTER}

# Blender's editor-type menu (ED_area_type_hud / space_type_menu): groups of (name, icon).
EDITOR_TYPES = [
    [("3D Viewport", "VIEW3D"), ("Image Editor", "IMAGE"), ("UV Editor", "UV"),
     ("Compositor", "NODE_COMPOSITING"), ("Texture Node Editor", "NODE_TEXTURE"),
     ("Geometry Node Editor", "GEOMETRY_NODES"), ("Shader Editor", "NODE_MATERIAL"),
     ("Video Sequencer", "SEQUENCE"), ("Movie Clip Editor", "TRACKER")],
    [("Dope Sheet", "ACTION"), ("Timeline", "TIME"), ("Graph Editor", "GRAPH"),
     ("Drivers", "DRIVER"), ("Nonlinear Animation", "NLA")],
    [("Text Editor", "TEXT"), ("Python Console", "CONSOLE"), ("Info", "INFO")],
    [("Outliner", "OUTLINER"), ("Properties", "PROPERTIES"), ("File Browser", "FILEBROWSER"),
     ("Asset Browser", "ASSET_MANAGER"), ("Spreadsheet", "SPREADSHEET"),
     ("Preferences", "PREFERENCES")],
]

#: The regions an editor may have, in Blender's names (one of each).
REGIONS = ("header", "tool_header", "footer", "asset_shelf", "channels", "nav_bar", "toolbar",
           "sidebar", "execute", "hud")


def _icon_of(name: str, groups) -> str:
    return next((i for group in groups for n, i in group if n == name), "QUESTION")


class AreaHeader(Component):
    """An editor's header region (``ED_region_header_layout``): 1.3 units tall,
    items left to right from the corner offset; a ``Spacer()`` pushes the rest
    to the right (``layout.separator_spacer()``)."""

    TYPE = "areaheader"

    def __init__(self, *children: Component, **kw):
        super().__init__(**kw)
        from bui.components.widgets import Spacer
        for c in children:
            c.weight = c.weight if isinstance(c, Spacer) else 0
        self.add(*children)


class Region:
    """``component`` as one of an area's regions. ``size`` is unscaled pixels across the area
    (a sidebar's width, an asset shelf's height); ``side`` flips it ("left", "right", "top",
    "bottom": the sides its kind allows); ``overlap=False`` gives it its own space instead of
    floating over the main view; ``hotkey`` ("n", "ctrl+]") toggles it while the pointer is over
    the area; ``on_resize(size, hidden)`` follows the user dragging, hiding or showing it."""

    def __init__(self, component: Component, *, size: Optional[int] = None,
                 hidden: bool = False, side: Optional[str] = None,
                 overlap: Optional[bool] = None, resizable: bool = True,
                 maxsize: Optional[int] = None, hotkey: Optional[str] = None,
                 on_resize: Optional[Callable[[int, bool], None]] = None):
        self.component = component
        self.props = {"size": size, "hidden": hidden or None, "side": side, "overlap": overlap,
                      "resizable": None if resizable else False, "maxsize": maxsize,
                      "hotkey": hotkey}
        self.on_resize = on_resize

    def _place(self, kind: str) -> Component:
        c = self.component
        c.set("region", kind)
        for key, value in self.props.items():
            if value is not None:
                c.set(key, value)  # refused values raise (KeyError) here, at the boundary
        if self.on_resize is not None:
            c.on_resize = self.on_resize
        return c


class Editor:
    """What an editor shows: ``body`` (the main view) and its regions, each a component, a
    :class:`Region` (with its size, side, hotkey ...) or, for the ``header``, a list of
    components after the editor-type menu."""

    def __init__(self, body: Optional[Component] = None, *,
                 header: Union[Sequence[Component], Component, Region, None] = (), **regions):
        for name in regions:
            if name not in REGIONS:
                raise TypeError(f"Editor() got an unexpected region '{name}'")
        self.body, self.header, self.regions = body, header, regions


class Area(Component):
    """One editor: the header with Blender's editor-type menu, the regions, the main view.

        Area("Geometry Node Editor", NodeEditor(...), header=[MenuButton("View", [...])],
             sidebar=Region(Sidebar(...), size=240), on_editor=lambda name: ...)

    In a :class:`Screen` the user splits, joins, docks and resizes areas; on its own an area is
    a panel whose regions resize and hide. ``on_editor(name)`` follows the editor-type menu."""

    TYPE = "area"

    def __init__(self, editor: str, body: Optional[Component] = None, *,
                 header: Union[Sequence[Component], Component, Region, None] = (),
                 on_editor: Optional[Callable[[str], None]] = None, **kw):
        regions = {k: kw.pop(k) for k in list(kw) if k in REGIONS}
        kw.setdefault("fill", True)  # an area fills its place
        super().__init__(**kw)
        self._setup(editor, on_editor, EDITOR_TYPES)
        self.fill(Editor(body, header=header, **regions))

    def _setup(self, editor, on_editor, editor_types):
        self.on_editor = on_editor
        self._types = editor_types
        self.header = self.editor_button = self.body = None
        self.set("editor", editor)

    @classmethod
    def _adopt(cls, node: buipy.KitNode) -> "Area":
        """The Python side of an area the screen made (``node`` is a borrowed wrapper)."""
        if isinstance(node, Area):
            return node
        area = cls.__new__(cls)
        buipy.KitNode._take(area, node)  # the node is made already: only the Python fields
        area.motion = None
        area.on_editor = None
        area._types = EDITOR_TYPES
        area.header = area.editor_button = area.body = None
        return area

    @property
    def editor(self) -> str:
        return self.get_str("editor")

    def _editor_button(self):
        from bui.components.menus import MenuButton, MenuItem
        items = []
        for g, group in enumerate(self._types):
            if g:
                items.append(None)
            for name, ic in group:
                items.append(MenuItem(name, (lambda n=name: self._pick(n)), icon=ic, checked=None))
        return MenuButton("", items, icon=_icon_of(self.editor, self._types),
                          description="Editor Type")

    def fill(self, editor: Editor) -> "Area":
        """Shows ``editor``: what was in the area goes (Blender's ``ED_area_newspace``)."""
        for c in list(self.children):
            buipy.KitNode.remove(self, c)
        self.header = self.editor_button = None
        header = editor.header
        if isinstance(header, Component):
            header = Region(header)
        if isinstance(header, Region):  # a header of the host's own
            self.header = header._place("header")
            buipy.KitNode.add(self, self.header)
        elif header is not None:  # the editor-type menu, then these
            self.editor_button = self._editor_button()
            self.header = AreaHeader(self.editor_button, *header)
            buipy.KitNode.add(self, self.header)
        for kind, region in editor.regions.items():
            if region is None:
                continue
            region = region if isinstance(region, Region) else Region(region)
            buipy.KitNode.add(self, region._place(kind))
        self.body = editor.body
        if self.body is not None:
            buipy.KitNode.add(self, self.body)
        return self

    def _pick(self, name: str) -> None:
        self.set("editor", name)
        if self.editor_button is not None:
            self.editor_button.set("icon", _icon_of(name, self._types))
        screen = self.parent
        if isinstance(screen, Screen):
            screen._show(self)
        if self.on_editor is not None:
            self.on_editor(name)

    # -- the screen's operations --------------------------------------------------------------
    def split(self, axis: str, factor: float) -> Optional["Area"]:
        """A new area off this one (Blender's ``area_split``): ``axis`` "v" puts it beside
        (``factor`` from the left), "h" above or below (from the bottom); it is the side past 0.5.
        None when the area is too small. The screen fills it with this area's editor."""
        node = self.area_split(axis, float(factor))
        return None if node is None else Area._adopt(node)

    def join(self, other: "Area") -> bool:
        """``other`` joins into this area (they share an edge)."""
        return self.area_join(other)

    def dock(self, target: "Area", where: str = "center", factor: float = 0.5) -> bool:
        """Moves this area beside ``target`` ("left", "right", "top", "bottom", taking
        ``factor`` of it) or into its place ("center")."""
        if where not in _DOCK:
            raise ValueError(f"dock: where must be one of {sorted(_DOCK)}")
        return self.area_dock(target, _DOCK[where], float(factor))

    def swap(self, other: "Area") -> bool:
        return self.area_swap(other)

    def close(self) -> bool:
        """A neighbor takes this area's place."""
        return self.area_close()

    @property
    def full(self) -> str:
        """"" (normal), "maximized" (Ctrl+Space) or "focus" (Ctrl+Alt+Space)."""
        return self.get_str("full")

    def maximize(self) -> None:
        self.set("full", "maximized")

    def focus(self) -> None:
        self.set("full", "focus")

    def restore(self) -> None:
        self.set("full", "")

    def _on_area(self, index: int, what: int) -> None:
        screen = self.parent
        if what == AREA_WINDOW and isinstance(screen, Screen) and screen.on_window is not None:
            screen.on_window(self)


class AreaSplit:
    """A screen's starting layout: ``first`` and ``second`` side by side (``axis`` "v", ``first``
    on the left) or stacked ("h", ``first`` on top), ``first`` taking ``factor``. Each is an
    editor name or another AreaSplit."""

    def __init__(self, axis: str, factor: float, first: Union[str, "AreaSplit"],
                 second: Union[str, "AreaSplit"]):
        if axis not in ("h", "v"):
            raise ValueError("AreaSplit: axis must be 'h' or 'v'")
        if not 0.0 < factor < 1.0:
            raise ValueError("AreaSplit: factor must be between 0 and 1")
        self.axis, self.factor, self.first, self.second = axis, float(factor), first, second


# Panes hosts Qt and Tk widgets, whose own rect / parent / children are their APIs': the kit's.
def _rect(node):
    return buipy.KitNode.rect.__get__(node)


def _parent(node):
    return buipy.KitNode.parent.__get__(node)


def _children(node):
    return buipy.KitNode.children.__get__(node)


def _type(node):
    return buipy.KitNode.type.__get__(node)


class Panes:
    """Panes side by side (``axis`` "v") or stacked ("h") on a screen node whose only operation
    is dragging the edges between them (``ops`` "resize"): one area per pane. The splitters of
    the Qt and Tk APIs (QSplitter, PanedWindow) are this, so they look and drag like Blender's
    area edges. Sizes asked for (``set_sizes``) are placed exactly once the screen knows its
    size, the last pane taking the rest; after that the user's drags and window resizes move
    the edges as in Blender (proportionally). ``on_moved(pos, index)`` follows the user moving
    edge ``index`` (1: between the first two panes) to ``pos`` pixels from the start."""

    def __init__(self, node: buipy.KitNode, axis: str = "v",
                 on_moved: Optional[Callable[[int, int], None]] = None):
        self.node, self.axis, self.on_moved = node, axis, on_moved
        self.panes: list = []
        self._want = None   # sizes asked for, until the screen has a size to place them in
        self._applying = False
        self._seen: list = []
        buipy.KitNode.set(node, "ops", "resize")
        buipy.KitNode.set(node, "events", True)  # EV_CONFIGURE: the size to place asked-for sizes in

    # -- geometry: the area borders (area_calc_totrct) between and around the panes ----------
    @staticmethod
    def _pad() -> int:
        return max(1, round(2 * buipy.kit_unit() / 20))  # U.border_width * UI_SCALE_FAC

    @property
    def handle(self) -> int:
        """The pixels between two panes: each area's border, and the edge pixel."""
        return 2 * self._pad() - 1

    def _extent(self):
        x, y, w, h = _rect(self.node)
        if w < 2 or h < 2:
            w, h = buipy.KitNode.natural(self.node)
        return (w, h) if self.axis == "v" else (h, w)

    def sizes(self) -> list:
        if self._want is not None and not self._placed():
            return list(self._want)
        return [_rect(p)[2] if self.axis == "v" else _rect(p)[3] for p in self.panes]

    def _placed(self) -> bool:
        x, y, w, h = _rect(self.node)
        return w > 1 and h > 1

    def set_sizes(self, sizes) -> None:
        self._want = [max(0, int(s)) for s in sizes][:len(self.panes)]
        self._layout()

    def edge(self, index: int) -> int:
        """Where edge ``index`` (0: after the first pane) is, from the screen's start."""
        r, own = _rect(self.panes[index]), _rect(self.node)
        return (r[0] + r[2] - own[0]) if self.axis == "v" else (r[1] + r[3] - own[1])

    def place_edge(self, index: int, pos: int) -> None:
        """Moves edge ``index`` to ``pos`` (Tk's sash_place): the pane after it gives or takes."""
        sizes = self.sizes()
        start = self._pad() + sum(sizes[:index]) + index * self.handle
        delta = int(pos) - (start + sizes[index])
        delta = max(-sizes[index], min(delta, sizes[index + 1]))
        sizes[index] += delta
        sizes[index + 1] -= delta
        self.set_sizes(sizes)

    # -- panes ---------------------------------------------------------------------------------
    def insert(self, index: int, pane) -> None:
        if pane in self.panes:
            self.panes.remove(pane)
        self.panes.insert(max(0, min(int(index), len(self.panes))), pane)
        self._rebuild()

    def remove(self, pane) -> None:
        if pane in self.panes:
            self.panes.remove(pane)
            self._detach(pane)
            self._rebuild()

    def _detach(self, pane) -> None:
        parent = _parent(pane)
        if parent is not None and _type(parent) == "area":
            buipy.KitNode.remove(parent, pane)

    def _rebuild(self) -> None:
        if self._want is not None:
            self._want = self._want[:len(self.panes)]
        for p in self.panes:
            self._detach(p)
        if not self.panes:
            for c in _children(self.node):
                buipy.KitNode.remove(self.node, c)  # the screen starts over
            return
        if not _children(self.node):
            buipy.KitNode.add(self.node, buipy.KitNode("area"))
        self._layout()
        for p, area in zip(self.panes, _children(self.node)):
            buipy.KitNode.add(area, p)

    def _layout(self) -> None:
        """The panes' areas at the sizes asked for (equal shares without), as a screen layout."""
        n = len(self.panes)
        if n == 0:
            return
        extent, other = self._extent()
        want = list(self._want or [])
        share = max(1, (extent - 2 * self._pad() - (n - 1) * self.handle) // n)
        sizes = [want[i] if i < len(want) else share for i in range(n)]
        cuts = [0]
        for i in range(n - 1):  # a pane spans from its edge vertex + pad to the next one - pad
            at = cuts[-1] + max(1, sizes[i]) + self.handle
            cuts.append(max(cuts[-1] + 1, min(at, extent - 1 - (n - 1 - i))))
        cuts.append(extent - 1)
        verts, areas = [], []
        for i, c in enumerate(cuts):
            if self.axis == "v":
                verts += [[c, 0], [c, other - 1]]
            else:  # stacked from the top: y runs up
                verts += [[0, extent - 1 - c], [other - 1, extent - 1 - c]]
        for i in range(n):
            a, b = 2 * i, 2 * (i + 1)
            if self.axis == "v":  # bottom-left, top-left, top-right, bottom-right
                areas.append({"v": [a, a + 1, b + 1, b]})
            else:
                areas.append({"v": [b, a, a + 1, b + 1]})
        import json
        size = [extent, other] if self.axis == "v" else [other, extent]
        self._applying = True
        try:
            buipy.KitNode.set(self.node, "layout", json.dumps({"bui_screen": 1, "size": size, "verts": verts,
                                                "areas": areas}))
        finally:
            self._applying = False
        if self._placed():
            self._want = None
        self._seen = []

    # -- events from the screen node (forwarded by the owner's _on_event) ------------------------
    def on_configure(self) -> None:
        if self._want is not None and self.panes:
            self._layout()

    def _sizes_now(self) -> list:
        """The panes' sizes from the screen's geometry as it is now (rects follow at layout)."""
        import json
        doc = json.loads(buipy.KitNode.get_str(self.node, "layout"))
        verts, axis = doc["verts"], 0 if self.axis == "v" else 1
        out = []
        for area in doc["areas"]:  # an area spans its corners less a border on each side
            lo, hi = verts[area["v"][0]][axis], verts[area["v"][2]][axis]
            out.append(hi - lo + 1 - 2 * self._pad())
        return out

    def on_change(self) -> None:
        if self._applying:
            return
        sizes = self._sizes_now()
        if self._seen and len(self._seen) == len(sizes) and self.on_moved is not None:
            for i in range(len(sizes) - 1):
                if sizes[i] != self._seen[i]:
                    self.on_moved(sum(sizes[:i + 1]) + i * self.handle, i + 1)
                    break
        self._seen = sizes


Factory = Callable[[Area], Optional[Editor]]


class Screen(Component):
    """Areas the user rearranges as in Blender (see the module docs).

    ``editors``: editor name -> ``factory(area) -> Editor`` (or ``(icon, factory)``); they fill
    every area, the ones the user makes too, and list the editor-type menu. ``layout``: the
    first arrangement (an editor name or an :class:`AreaSplit`), used when ``saved`` (a layout
    from ``screen.layout``) is None. ``on_change(layout)`` follows every change the user makes:
    save the string to restore it. ``on_window(area)``: the user dragged ``area`` out of the
    window (or chose Duplicate Area into New Window). ``ops="resize"`` allows only edge drags."""

    TYPE = "screen"

    def __init__(self, editors: Dict[str, Union[Factory, tuple]],
                 layout: Union[str, AreaSplit, None] = None, *, saved: Optional[str] = None,
                 on_change: Optional[Callable[[str], None]] = None,
                 on_window: Optional[Callable[[Area], None]] = None,
                 ops: str = "all", transparent: bool = False, **kw):
        kw.setdefault("fill", True)
        super().__init__(**kw)
        self.on_change, self.on_window = on_change, on_window
        self._factories, groups = {}, []
        for name, entry in editors.items():
            icon, factory = entry if isinstance(entry, tuple) else (_icon_of(name, EDITOR_TYPES),
                                                                    entry)
            self._factories[name] = factory
            groups.append((name, icon))
        self._types = [groups]
        self._building = False
        if ops != "all":
            self.set("ops", ops)
        if transparent:
            self.set("transparent", True)
        layout = layout if layout is not None else next(iter(self._factories), "")
        first = self._new_area(layout if isinstance(layout, str) else "")
        buipy.KitNode.add(self, first)
        self._building = True
        self._build(layout, first)
        self._building = False
        if saved is not None:
            self.layout = saved

    # -- areas ----------------------------------------------------------------------------------
    def _new_area(self, editor: str) -> Area:
        area = Area.__new__(Area)
        Component.__init__(area, fill=True)  # the node, from Area.TYPE
        area._setup(editor, None, self._types)
        return area

    def _show(self, area: Area) -> None:
        """Fills ``area`` for its editor (a new area, or the editor-type menu changed it)."""
        area._types = self._types
        factory = self._factories.get(area.editor)
        editor = factory(area) if factory is not None else None
        area.fill(editor if editor is not None else Editor(None))

    def _build(self, spec, area: Area) -> None:
        if isinstance(spec, str):
            area.set("editor", spec)
            self._show(area)
            return
        # The new area is the side past 0.5: kit factors run from the left and from the bottom,
        # AreaSplit's from the left and from the top.
        kit_factor = spec.factor if spec.axis == "v" else 1.0 - spec.factor
        new = area.split(spec.axis, kit_factor)
        if new is None:
            raise ValueError("AreaSplit: the screen is too small for this layout")
        new_is_first = (kit_factor <= 0.5) if spec.axis == "v" else (kit_factor > 0.5)
        first, second = (new, area) if new_is_first else (area, new)
        self._build(spec.first, first)
        self._build(spec.second, second)

    @property
    def areas(self) -> list:
        return [Area._adopt(c) for c in self.children]

    def _sync(self) -> None:
        """Areas the screen made get their Python side and their editor."""
        for c in self.children:
            if not isinstance(c, Area):
                area = Area._adopt(c)
                if not self._building:
                    self._show(area)

    @property
    def layout(self) -> str:
        """The arrangement as text (JSON): store it, set it back to restore."""
        return self.get_str("layout")

    @layout.setter
    def layout(self, text: str) -> None:
        before = {id(a): a.editor for a in self.areas}
        self.set("layout", text)  # a malformed layout raises (KeyError), and nothing changes
        self._sync()
        for area in self.areas:  # the areas that show another editor now
            if before.get(id(area), area.editor) != area.editor:
                self._show(area)

    def _on_area(self, index: int, what: int) -> None:
        if what == AREA_NEW:
            self._sync()

    def _on_event(self, kind, ref, a, b):
        if kind == EV_CHANGE:
            self._sync()
            if self.on_change is not None:
                self.on_change(self.layout)
