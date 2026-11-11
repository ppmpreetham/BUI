"""Generate BUI's theme key table and schema from Blender's theme preset.

    py -3 tools/gen_theme_keys.py [Blender_Light.xml]

A theme file names its colors in plain English ("button.background", "node_editor.grid") and also
accepts Blender's own names ("user_interface.wcol_regular.inner"). Both come from here, so there is
one list: Blender's theme XML, every attribute of it, renamed by the rules below. Writes

  kit/intern/kit_theme_keys.inc     the keys BKTheme can hold, as a C table (offsets into BKTheme)
  kit/intern/kit_theme_aliases.inc  the short palette names ("background", "primary.background") and what they set
  docs/theme-schema.json          a JSON Schema of a theme file: every key, for editor completion
  docs/theme-keys.md              the same list as a table

Without an argument the preset is looked up in
../geometry-nodes/geometry-nodes/blender/scripts/presets/interface_theme and, failing that, the
BLENDER_PRESETS environment variable. Stdlib only.

SPDX-License-Identifier: GPL-2.0-or-later
"""
from __future__ import annotations

import json
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_INC = os.path.join(ROOT, "kit", "intern", "kit_theme_keys.inc")
OUT_ALIASES = os.path.join(ROOT, "kit", "intern", "kit_theme_aliases.inc")
OUT_SCHEMA = os.path.join(ROOT, "docs", "theme-schema.json")
OUT_DOC = os.path.join(ROOT, "docs", "theme-keys.md")

# ---- Blender's keys: the path of property elements down to an attribute ----


def blender_keys(xml_path):
    """Every attribute of the preset as {dotted key: value} (the XML's wrapper types are left out)."""
    root = ET.parse(xml_path).getroot()
    keys = {}

    def convert(v):
        if v in ("TRUE", "FALSE"):
            return v == "TRUE"
        if re.fullmatch(r"-?\d+", v):
            return int(v)
        if re.fullmatch(r"-?\d*\.\d+(e-?\d+)?|-?\d+\.\d*", v):
            return float(v)
        return v

    def walk(el, path):
        wrappers = [c for c in el if c.tag[0].isupper()]
        repeated = len(wrappers) > 1
        seen = 0
        for child in el:
            if child.tag[0].isupper():  # a type wrapper: ThemeWidgetColors ...
                sub = path + ([str(seen)] if repeated else [])
                seen += 1
                for name, value in child.attrib.items():
                    keys[".".join(sub + [name])] = convert(value)
                walk(child, sub)
            else:
                walk(child, path + [child.tag])

    for top in root:  # <Theme> and <ThemeStyle>
        walk(top, [] if top.tag == "Theme" else ["ui_styles"])
    return keys


# ---- plain-English names ----

WIDGETS = {  # Blender's wcol_<x> -> the widget's name
    "regular": "button", "tool": "tool_button", "toolbar_item": "toolbar_button",
    "toggle": "toggle_button", "radio": "radio_button", "option": "checkbox",
    "text": "text_field", "num": "number_field", "numslider": "slider", "tab": "tab",
    "menu": "menu", "menu_back": "popup", "menu_item": "menu_item", "pulldown": "dropdown",
    "tooltip": "tooltip", "box": "box", "scroll": "scrollbar", "progress": "progress_bar",
    "list_item": "list_item", "pie_menu": "pie_menu", "state": "status",
}
WIDGET_LEAVES = {
    "outline": "border", "outline_sel": "border.selected", "inner": "background",
    "inner_sel": "background.selected", "item": "fill", "text": "text", "text_sel": "text.selected",
    "show_shaded": "gradient.enabled", "shadetop": "gradient.top", "shadedown": "gradient.bottom",
    "roundness": "roundness",
}
UI = {
    "editor_border": "editor.border", "editor_outline": "editor.outline",
    "editor_outline_active": "editor.outline.active", "widget_emboss": "widget.emboss",
    "widget_text_cursor": "caret", "menu_shadow_fac": "menu.shadow.strength",
    "menu_shadow_width": "menu.shadow.size", "panel_back": "panel.background",
    "panel_header": "panel.header", "panel_outline": "panel.border",
    "panel_roundness": "panel.roundness", "panel_sub_back": "panel.sub_background",
    "panel_text": "panel.text", "panel_title": "panel.title",
    "transparent_checker_primary": "checker.primary",
    "transparent_checker_secondary": "checker.secondary", "transparent_checker_size": "checker.size",
}
GROUPS = {
    "view_3d": "viewport_3d", "node_editor": "node_editor", "file_browser": "file_browser",
    "image_editor": "image_editor", "sequence_editor": "sequencer", "nla_editor": "nla_editor",
    "clip_editor": "clip_editor", "text_editor": "text_editor", "info": "info_editor",
    "dopesheet_editor": "dope_sheet", "graph_editor": "graph_editor", "outliner": "outliner",
    "console": "console", "properties": "properties_editor", "spreadsheet": "spreadsheet",
    "topbar": "top_bar", "statusbar": "status_bar", "preferences": "preferences_editor",
    "regions": "regions", "common": "common", "bone_color_sets": "bone_colors",
    "collection_color": "collection_colors", "strip_color": "strip_colors", "ui_styles": "font",
}
TOKENS = {  # Blender's abbreviations, written out
    "sel": "selected", "hi": "highlight", "back": "background", "anim": "animated",
    "fac": "strength", "len": "length", "vert": "vertex", "gp": "grease_pencil", "inner": "background",
    "bg": "background", "facedot": "face_dot", "editmesh": "edit_mesh", "distor": "distort",
    "metadatabg": "metadata.background", "metadatatext": "metadata.text", "points": "size",
}
ROLES = {"selected", "highlight", "background", "border", "text", "size", "strength", "top", "bottom",
         "alpha", "width", "active", "animated", "changed", "key", "driven", "overridden"}


def leaf_words(leaf, state=False):
    """Compound nouns keep their underscores (distort_node); a role word starts a new dotted part."""
    tokens = []
    for token in leaf.split("_"):
        tokens.append("inner" if token == "inner" and not state else TOKENS.get(token, token))
    parts, cur = [], []
    for token in ".".join(tokens).split("."):
        if token in ROLES and cur:
            parts.append("_".join(cur))
            cur = []
        cur.append(token)
    parts.append("_".join(cur))
    return ".".join(parts)


def friendly(key):
    parts = key.split(".")
    top = parts[0]
    if top == "user_interface":
        if parts[1].startswith("wcol_"):
            name, leaf = WIDGETS[parts[1][5:]], parts[2]
            if leaf in WIDGET_LEAVES:
                return f"{name}.{WIDGET_LEAVES[leaf]}"
            return f"{name}.{leaf_words(leaf, state=parts[1] == 'wcol_state')}"
        return UI.get(parts[1]) or leaf_words(parts[1])
    if top == "ui_styles":
        return f"font.{parts[1]}.{leaf_words(parts[2])}"
    mid = ".".join(parts[1:-1])
    return ".".join(x for x in (GROUPS[top], mid, leaf_words(parts[-1])) if x)


# ---- what BKTheme (kit/intern/kit_intern.h) can hold ----
# BKTheme's widget color set -> (Blender's wcol name or None, friendly widget name)
WCOLS = [
    ("regular", "regular"), ("tool", "tool"), ("toggle", "toggle"), ("radio", "radio"),
    ("option", "option"), ("textfield", "text"), ("num", "num"), ("numslider", "numslider"),
    ("menu", "menu"), ("pulldown", "pulldown"), ("menu_back", "menu_back"),
    ("menu_item", "menu_item"), ("list_item", "list_item"), ("box", "box"), ("scroll", "scroll"),
    ("progress", "progress"), ("tooltip", "tooltip"), ("tab", "tab"), ("pie_menu", "pie_menu"),
    ("curve", None),
]
COLOR_LEAVES = [  # BKWCol field, Blender attribute, friendly leaf
    ("outline", "outline", "border"), ("outline_sel", "outline_sel", "border.selected"),
    ("inner", "inner", "background"), ("inner_sel", "inner_sel", "background.selected"),
    ("item", "item", "fill"), ("text", "text", "text"), ("text_sel", "text_sel", "text.selected"),
]
# Fields of BKTheme itself: (field, kind, Blender key or None, friendly key when Blender has none)
GLOBALS = [
    ("region_bg", "color", None, "region.background"),
    ("header_bg", "color", None, "header.background"),
    ("tabs_bg", "color", None, "tab_bar.background"),
    ("panel_header", "color", "user_interface.panel_header", None),
    ("panel_back", "color", "user_interface.panel_back", None),
    ("panel_sub_back", "color", "user_interface.panel_sub_back", None),
    ("panel_outline", "color", "user_interface.panel_outline", None),
    ("text", "color", None, "text"),
    ("text_hi", "color", None, "text.highlight"),
    ("emboss", "color", "user_interface.widget_emboss", None),
    ("cursor", "color", "user_interface.widget_text_cursor", None),
    ("selection", "color", None, "selection.background"),
    ("menu_shadow_fac", "float", "user_interface.menu_shadow_fac", None),
    ("menu_shadow_width", "int", "user_interface.menu_shadow_width", None),
    ("panel_roundness", "float", "user_interface.panel_roundness", None),
    ("file_back", "color", "file_browser.space.back", None),
    ("file_text", "color", "file_browser.space.text", None),
    ("file_header", "color", "file_browser.space.header", None),
    ("file_row_alternate", "color", "file_browser.row_alternate", None),
    ("node_back", "color", "node_editor.space.back", None),
    ("node_grid", "color", "node_editor.grid", None),
    ("node_wire", "color", "node_editor.wire", None),
    ("node_body", "color", None, "node_editor.node.body"),
    ("node_outline", "color", "node_editor.node_outline", None),
    ("node_selected", "color", "node_editor.node_selected", None),
    ("node_active", "color", "node_editor.node_active", None),
    ("area_header", "color", None, "area_header.background"),
]
CORNERS = [("top_left", "CNR_TL"), ("top_right", "CNR_TR"), ("bottom_right", "CNR_BR"),
           ("bottom_left", "CNR_BL")]


# ---- the short palette names (gpui-kit's) and the BKTheme keys they fill in ----
# A convenience layer, not Blender's: a theme that sets only "primary.background" gets a coherent look.
# These are applied before the explicit keys of the same theme, which always win.
SOLID = ["button", "tool_button", "toggle_button", "radio_button", "checkbox", "number_field", "slider"]
TEXTED = SOLID + ["text_field", "menu", "dropdown", "menu_item", "list_item", "box", "progress_bar", "tab"]
ALL_WIDGETS = sorted(set(TEXTED + ["popup", "tooltip", "scrollbar", "pie_menu"]))
ALIASES = {
    "background": ["region.background"],
    "foreground": ["text"] + [f"{w}.text" for w in TEXTED],
    "border": [f"{w}.border" for w in ALL_WIDGETS] + ["panel.border"],
    "primary.background": [f"{w}.background.selected" for w in ALL_WIDGETS]
                          + ["number_field.fill", "slider.fill", "progress_bar.fill"],
    "primary.foreground": [f"{w}.text.selected" for w in ALL_WIDGETS],
    "secondary.background": [f"{w}.background" for w in SOLID],
    "secondary.foreground": [f"{w}.text" for w in SOLID],
    "accent.background": ["menu_item.background.selected", "dropdown.background.selected",
                          "list_item.background.selected"],
    "accent.foreground": ["menu_item.text.selected", "dropdown.text.selected", "list_item.text.selected"],
    "popover.background": ["popup.background", "tooltip.background"],
    "popover.foreground": ["popup.text", "tooltip.text"],
    "input.border": ["text_field.border"],
    "muted.background": ["panel.sub_background"],
    "tab.background": ["tab.background"],
    "tab.foreground": ["tab.text"],
    "tab.active.background": ["tab.background.selected"],
    "tab.active.foreground": ["tab.text.selected"],
    "scrollbar.background": ["scrollbar.background"],
    "scrollbar.thumb.background": ["scrollbar.fill"],
}


def table(blender):
    """The C table: (friendly, blender or '', kind, field expression, extra)."""
    rows = []
    for field, bname in WCOLS:
        widget = WIDGETS[bname] if bname else field
        for leaf, battr, fleaf in COLOR_LEAVES:
            bkey = f"user_interface.wcol_{bname}.{battr}" if bname else ""
            rows.append((f"{widget}.{fleaf}", bkey, "color", f"{field}.{leaf}", ""))
        for leaf, battr, fleaf, kind in (("shaded", "show_shaded", "gradient.enabled", "flag"),
                                         ("shadetop", "shadetop", "gradient.top", "short"),
                                         ("shadedown", "shadedown", "gradient.bottom", "short"),
                                         ("roundness", "roundness", "roundness", "float")):
            bkey = f"user_interface.wcol_{bname}.{battr}" if bname else ""
            rows.append((f"{widget}.{fleaf}", bkey, kind, f"{field}.{leaf}", ""))
        # what BUI adds: not in Blender's XML, so no Blender name
        rows.append((f"{widget}.border.width", "", "border_width", f"{field}.border_extra", ""))
        rows.append((f"{widget}.emboss.enabled", "", "emboss", f"{field}.no_emboss", ""))
        for corner, flag in CORNERS:
            rows.append((f"{widget}.corners.{corner}", "", "corner", f"{field}.square_corners", flag))
    for field, kind, bkey, fkey in GLOBALS:
        rows.append((fkey or friendly(bkey), bkey or "", kind, field, ""))
    for friendly_key, bkey, *_ in rows:  # every Blender name must exist in the preset
        if bkey and bkey not in blender:
            raise SystemExit(f"Blender key not in the preset: {bkey}")
    names = [r[0] for r in rows]
    assert len(names) == len(set(names)), "duplicate friendly key"
    return rows


def write_aliases(rows):
    known = {r[0] for r in rows}
    lines = [
        "/* Generated by tools/gen_theme_keys.py. Do not edit.",
        " *",
        " * The short palette names and the theme keys each one fills in (kit_theme.c).",
        " *",
        " * SPDX-License-Identifier: GPL-2.0-or-later */",
        "",
    ]
    for i, (name, targets) in enumerate(ALIASES.items()):
        for t in targets:
            if t not in known:
                raise SystemExit(f"alias {name}: unknown key {t}")
        lines.append(f"static const char *const k_alias_{i}[] = {{")
        lines.extend(f'    "{t}",' for t in targets)
        lines.append("    NULL};")
    lines.append("")
    lines.append("static const struct {")
    lines.append("  const char *name;")
    lines.append("  const char *const *targets;")
    lines.append("} k_aliases[] = {")
    lines.extend(f'    {{"{name}", k_alias_{i}}},' for i, name in enumerate(ALIASES))
    lines.append("};")
    with open(OUT_ALIASES, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def write_inc(rows):
    macro = {"color": "TK_COLOR", "float": "TK_FLOAT", "int": "TK_INT", "short": "TK_SHORT",
             "flag": "TK_FLAG", "border_width": "TK_BORDER_WIDTH", "emboss": "TK_EMBOSS",
             "corner": "TK_CORNER"}
    lines = [
        "/* Generated by tools/gen_theme_keys.py from Blender's theme preset. Do not edit.",
        " *",
        " * The theme keys BKTheme can hold: the plain-English name, Blender's own name (or \"\"),",
        " * and where the value lives in BKTheme (kit_theme.c includes this inside its table).",
        " *",
        " * SPDX-License-Identifier: GPL-2.0-or-later */",
        "",
    ]
    for friendly_key, bkey, kind, field, flag in rows:
        extra = f", {flag}" if flag else ""
        lines.append(f'{macro[kind]}("{friendly_key}", "{bkey}", {field}{extra})')
    with open(OUT_INC, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def write_schema(blender, rows):
    supported = {r[0] for r in rows}
    props = {}
    for key, value in sorted((friendly(k), v) for k, v in blender.items()):
        if isinstance(value, bool):
            props[key] = {"type": "boolean"}
        elif isinstance(value, (int, float)):
            props[key] = {"type": "number"}
        else:
            props[key] = {"$ref": "#/$defs/Rgba"}
        if key in supported:
            props[key]["description"] = "Used by BUI."
        else:
            props[key]["description"] = "Blender's key; BUI has no use for it yet and ignores it."
    for friendly_key, bkey, kind, *_ in rows:  # BUI's own keys
        if friendly_key not in props:
            props[friendly_key] = {"$ref": "#/$defs/Rgba"} if kind == "color" else {
                "type": "boolean" if kind in ("emboss", "corner", "flag") else "number"}
            props[friendly_key]["description"] = "Used by BUI."
    for alias in ALIASES:
        props.setdefault(alias, {"$ref": "#/$defs/Rgba"})
        props[alias]["description"] = "A short palette name: fills in several keys (docs/theming.md)."
    schema = {
        "$schema": "http://json-schema.org/draft-07/schema#",
        "title": "BUI theme set",
        "description": "A BUI theme file: a name and a list of themes (see docs/theming.md).",
        "type": "object",
        "required": ["name", "themes"],
        "properties": {
            "$schema": {"type": "string"},
            "name": {"type": "string"},
            "author": {"type": ["string", "null"]},
            "url": {"type": ["string", "null"]},
            "themes": {"type": "array", "items": {"$ref": "#/$defs/Theme"}},
        },
        "$defs": {
            "Rgba": {"type": "string", "pattern": "^#([0-9a-fA-F]{3}|[0-9a-fA-F]{4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"},
            "Theme": {
                "type": "object",
                "required": ["name"],
                "properties": {
                    "name": {"type": "string"},
                    "mode": {"enum": ["light", "dark"], "default": "dark",
                             "description": "The built-in theme this one starts from."},
                    "font.family": {"type": ["string", "null"]},
                    "font.size": {"type": ["number", "null"], "description": "UI text size in points (11)."},
                    "radius": {"type": ["number", "null"], "description": "Roundness (0 to 1) of every widget."},
                    "shadow": {"type": ["boolean", "null"], "description": "Text shadows."},
                    "colors": {"type": "object", "properties": props, "additionalProperties": True},
                },
            },
        },
    }
    with open(OUT_SCHEMA, "w", encoding="utf-8", newline="\n") as f:
        json.dump(schema, f, indent=2)
        f.write("\n")


def write_doc(blender, rows):
    used = {r[0]: r for r in rows}
    out = [
        "# Theme keys",
        "",
        "<!-- Generated by tools/gen_theme_keys.py. Do not edit. -->",
        "",
        "Every key a [theme file](theming.md) may set. A theme file takes the plain-English key, or the",
        "Blender key in the second column (they are the same thing). The last column says whether BUI uses",
        "the key: Blender's theme has more keys than BUI has widgets for, and a key BUI has no use for is",
        "accepted and ignored, so a theme exported from Blender loads whole.",
        "",
        "## Short palette names",
        "",
        "These fill in several keys at once. A key the theme sets itself always wins over them.",
        "",
        "| Name | Sets |",
        "| --- | --- |",
    ]
    for name, targets in ALIASES.items():
        shown = ", ".join(f"`{t}`" for t in targets[:4]) + (f" and {len(targets) - 4} more" if len(targets) > 4 else "")
        out.append(f"| `{name}` | {shown} |")
    out += ["", "## Keys", "", "| Key | Blender's key | Used by BUI |", "| --- | --- | --- |"]
    seen = set()
    for bkey in blender:
        fkey = friendly(bkey)
        seen.add(fkey)
        out.append(f"| `{fkey}` | `{bkey}` | {'yes' if fkey in used else 'no (ignored)'} |")
    for fkey, row in used.items():  # BUI's own keys: no Blender name
        if fkey not in seen:
            out.append(f"| `{fkey}` | {('`' + row[1] + '`') if row[1] else ''} | yes |")
    with open(OUT_DOC, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")


def find_preset():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for base in (os.path.join(ROOT, "..", "geometry-nodes", "geometry-nodes", "blender", "scripts", "presets"),
                 os.environ.get("BLENDER_PRESETS", "")):
        p = os.path.join(base, "interface_theme", "Blender_Light.xml")
        if os.path.exists(p):
            return p
    raise SystemExit("Blender_Light.xml not found: pass it, or set BLENDER_PRESETS")


def main():
    blender = blender_keys(find_preset())
    rows = table(blender)
    write_inc(rows)
    write_aliases(rows)
    write_schema(blender, rows)
    write_doc(blender, rows)
    print(f"{len(blender)} Blender keys; BKTheme holds {len(rows)} keys -> {OUT_INC}, {OUT_SCHEMA}")


if __name__ == "__main__":
    main()
