# Custom themes

BUI starts with Blender's two themes, Dark and Light. A **theme file** adds your own: a JSON file in the
shape of [gpui-kit's themes](https://github.com/longbridge/gpui-kit), with Blender's names for the colors.

```json
{
  "name": "My themes",
  "themes": [
    {
      "name": "My Dark",
      "mode": "dark",
      "colors": {
        "background": "#181825",
        "foreground": "#cdd6f4",
        "primary.background": "#89b4fa",
        "button.background": "#302d41",
        "button.corners.top_left": false,
        "node_editor.grid": "#1e1e2e"
      }
    }
  ]
}
```

```python
import blenderui as bui

bui.load_theme("my-themes.json")   # a path, or the JSON text itself
bui.set_theme("My Dark")           # every open window redraws
```

`docs/themes/catppuccin.json` is a complete example (three themes), and `docs/theme-schema.json` is a JSON
Schema: point `"$schema"` at it and an editor completes every key.

## The file

| Field | |
| --- | --- |
| `name` | The set's name (required). `author` and `url` are free text. |
| `themes` | The themes (required): a list of the objects below. |

A theme:

| Field | |
| --- | --- |
| `name` | Required. What you pass to `set_theme`, in any case. A name already loaded is replaced. |
| `mode` | `"dark"` (default) or `"light"`: the built-in theme this one starts from. A theme lists only what it changes. |
| `colors` | The colors and numbers, below. |
| `font.family` | The UI font (`BK_set_font`). |
| `font.size` | The UI text size in points (11 is Blender's). Browser-look controls keep their 13.33 px. |
| `shadow` | `false` turns the shadow behind UI text off. |
| `radius` | 0 to 1: the roundness of every widget (Blender's `roundness`). The keys below override it. |

Anything else in a theme (gpui-kit's `highlight`, `chart.*`, ...) is ignored, so a gpui-kit file loads.

## Colors and numbers

`colors` takes three kinds of key. All of them are listed in [theme-keys.md](theme-keys.md).

**Plain-English keys** name a part of a widget: `button.background`, `number_field.fill`, `slider.text.selected`,
`dropdown.border`, `popup.background`, `scrollbar.fill`, `tooltip.text`. The widgets are `button`,
`tool_button`, `toolbar_button`, `toggle_button`, `radio_button`, `checkbox`, `text_field`, `number_field`,
`slider`, `menu`, `dropdown`, `popup`, `menu_item`, `list_item`, `box`, `scrollbar`, `progress_bar`, `tooltip`,
`tab` and `pie_menu`; each has `border`, `border.selected`, `background`, `background.selected`, `fill`,
`text` and `text.selected`. Others name the surroundings: `region.background`, `panel.background`,
`caret`, `selection.background`, `node_editor.grid`, `file_browser.row_alternate` ...

**Blender's own keys** are the path of the property in Blender's theme XML: `user_interface.wcol_regular.inner`
is `button.background`. The two spellings are the same key, so you can copy a name out of Blender's theme
editor or a `.xml` preset. Blender's theme has 782 keys; BUI uses the ones it has widgets for and ignores
the rest (the table says which), so a theme from Blender loads whole.

**Short palette names** fill in many keys at once, as gpui-kit's do: `background`, `foreground`, `border`,
`primary.background`, `primary.foreground`, `secondary.background`, `accent.background`, `popover.background`,
`input.border` and more. They are a convenience of BUI's, not Blender's: `primary.background` sets the
selected fill of every widget and the bar of sliders and progress bars. A theme gets a coherent look from a
handful of them, then refines with exact keys: **a key the theme sets itself always wins over a short name.**

A color is `"#rgb"`, `"#rgba"`, `"#rrggbb"` or `"#rrggbbaa"`.

### What you can change about a widget

Besides colors, every widget above takes:

| Key | |
| --- | --- |
| `<widget>.roundness` | How round the corners are, 0 to 1 (a fraction of the widget unit). |
| `<widget>.corners.top_left`, `.top_right`, `.bottom_left`, `.bottom_right` | `false` keeps that corner square, wherever the layout would round it. |
| `<widget>.border.width` | The outline's width in pixels (1 is Blender's); `0` draws none. |
| `<widget>.emboss.enabled` | `false` removes the light line under the widget. |
| `<widget>.gradient.enabled`, `.gradient.top`, `.gradient.bottom` | Blender's top-to-bottom shading of the fill. |

Corners and borders can differ per widget, so `number_field` can be square on its left and `button` flat and
thick-bordered.

## From each language

| | Load | Choose | One value |
| --- | --- | --- | --- |
| Python | `bui.load_theme(path_or_text)`, `bui.themes()` | `bui.set_theme(name)` | `bui.set_theme_color(key, "#rrggbb")`, `bui.set_theme_value(key, 3)` |
| Rust | `blenderui::load_theme(text)`, `load_theme_file(path)`, `themes()` | `blenderui::set_theme(name)` | `set_theme_color(key, [r, g, b, a])`, `set_theme_value(key, 3.0)` |
| JavaScript | `await app.loadTheme(pathOrText)`, `await app.themes()` | `app.setTheme(name)` | `await app.setThemeColor(key, '#rrggbb')`, `await app.setThemeValue(key, 3)` |
| Lua | `bui.load_theme(path_or_text)`, `bui.themes()` | `bui.set_theme(name)` | `bui.set_theme_color(key, r, g, b)`, `bui.set_theme_value(key, 3)` |
| C | `BK_theme_load_file(path)`, `BK_theme_load_json(text)` | `BK_set_theme(name)` | `BK_theme_set_color(key, rgba)`, `BK_theme_set_num(key, v)` |

Reading goes by the same keys: `theme_color("button.background")`, `theme_value("button.border.width")`.
Single values change the active theme until the next `set_theme`.

A file with a mistake raises (Python `ValueError`, Rust `Err`, JavaScript a rejected promise, C `-1` and
`BK_theme_error()`) with the line and column, and none of its themes is added. Keys BUI doesn't know are
not errors; `BK_theme_ignored()` counts them (Python's `buipy.kit_theme_load_file` returns `(added, ignored)`).

## Your own keys

A color (`"#rrggbb"`, with an optional alpha) or a number in `colors` whose key BUI has no field for is not lost: it is the theme's own, and you read it back by that key while the theme is in use. An app can carry the colors it needs next to the ones BUI draws with:

```json
{ "name": "Chat", "mode": "dark",
  "colors": { "background": "#111b21", "muted.foreground": "#8696A0", "list.hover.background": "#2a3942", "bubble.radius": 14 } }
```

`theme_color("muted.foreground")` (Rust), `BK_theme_color("muted.foreground", rgba)` (C) and the same call in the other bindings return the color, and `theme_value("bubble.radius")` / `BK_theme_num` the number. Keys are matched without regard to case, and switching to another theme (or a built-in one) drops them. BUI itself uses none of them, so `BK_theme_ignored()` still counts them, and a misspelled BUI key is a custom key rather than an error. Own keys are read-only: `set_theme_color` is for BUI's keys.

## What a theme does not reach

- **Browser-look controls.** Plain HTML inputs and buttons draw like Chrome's, with Chrome's colors, outside
  `styled`. Their text takes the CSS `color` and `font-size` you give them. Inside `styled`, and for
  Blender's own components, the theme applies. The `styled` sheet follows the theme's background, text
  and (dark base) accent colors.
- **One theme for the whole process**, as in Blender. Two windows cannot differ.
- **Corners are all-or-nothing, not different radii.** A corner is round or square; the radius is one
  per widget. Blender draws it this way too.
- **Blender keys BUI has no widget for** (the 3D view, the bone colors, ...) are accepted and not drawn; like any other key BUI doesn't know, they can be read back as the theme's own.

## Where the keys come from

`tools/gen_theme_keys.py` reads Blender's `Blender_Light.xml` and writes the key table the kit compiles
(`kit/intern/kit_theme_keys.inc`), the short names (`kit_theme_aliases.inc`), `docs/theme-schema.json` and
`docs/theme-keys.md`: one list, so they cannot disagree. The plain-English names follow rules in the
script: Blender's `wcol_*` sets are named for the widget (`wcol_numslider` is `slider`), `inner` is
`background`, `outline` is `border`, `_sel` is `.selected`. The presets themselves
(`tools/gen_theme.py`) are unchanged.
