# BUI for Go

[Go](https://go.dev) bindings for BUI: Blender's widget drawing, layout and
interaction rules in a small C runtime, with two Go faces.

- **`bui`** is the kit itself: nodes made by type name and set by property
  name, contexts, native windows, headless rendering, menus, canvases, trees,
  images and themes. It is the D and Lua bindings' API in Go.
- **`ui`** builds the same widgets the way [MyGo](https://github.com/egoist/mygo)'s
  `ui` package does: a view is a function of your state that builds the
  interface, and runs again after input. `ui.Column(c).Gap(8).Children(...)`,
  `ui.Checkbox(c, &app.sync, "Sync")`, `if ui.Button(c, "Save").Clicked()`,
  `c.Key(id)`, and a `Tester` that clicks and types without a window.

```go
type counter struct{ n int }

func (s *counter) view(c *ui.Context) {
	ui.Column(c).Fill().Center().Gap(16).Children(func() {
		ui.Textf(c, "%d", s.n).FontSize(56).Bold()
		ui.Row(c).Gap(8).Children(func() {
			if ui.Button(c, "−").Label("Decrement").Clicked() {
				s.n--
			}
			if ui.PrimaryButton(c, "+").Label("Increment").Clicked() {
				s.n++
			}
		})
	})
}

func main() {
	s := &counter{}
	bui.App.WhenReady(func() {
		bui.NewWindow(bui.WindowOptions{Title: "Counter", Width: 360, Height: 260, Content: ui.View(s.view)})
	})
	if err := bui.App.Run(); err != nil {
		log.Fatal(err)
	}
}
```

## Requirements

- **Go 1.22** or newer, with cgo.
- **A C compiler cgo can use**: gcc or clang. On Windows that is MinGW-w64 gcc
  (MSYS2's `ucrt64` or `mingw64`, or the one on GitHub's runners); MSVC and
  `clang-cl` don't work with cgo. If `CC` names another compiler, set
  `CC=gcc CXX=g++` for the build.
- **On Linux**, the X11 development headers (`libx11-dev`, or `libX11-devel`).
- **CMake**, and Ninja if you have it (on Windows without Ninja, MinGW's
  `mingw32-make`).
- No separate BUI install: `go generate` builds the C kit into `.bui/`.

## Quick start

From this repository:

```sh
cd bindings/go
go generate ./bui            # builds the C kit into .bui/ (once, and after updating BUI)
go run ./examples/counter    # MyGo's counter
go run ./examples/todos      # a to-do list: inputs, check boxes, keyed rows, a panel
go run ./examples/hello      # the kit's nodes: a slider and a button
go run ./examples/headless   # render and click without a window
go test ./...                # both packages' tests, headless
```

`go generate` runs `internal/kitgen`, which configures CMake once in
`.bui/build` with the compiler cgo uses (`go env CC`, a launcher such as
`ccache gcc` included), builds `bui_kit`, and copies the libraries to
`.bui/lib` and the headers to `.bui/include`, only when they changed. Set
`BUI_SOURCE_DIR` to build another BUI checkout and `BUI_CMAKE_GENERATOR` for
another CMake generator.

Use the module from your own with a `replace` pointing at a checkout (the
module needs the C sources next to it, so it is not fetched on its own):

```
require github.com/ppmpreetham/BUI/bindings/go v0.0.0
replace github.com/ppmpreetham/BUI/bindings/go => ../BUI/bindings/go
```

and run `go generate github.com/ppmpreetham/BUI/bindings/go/bui` once.

## Threads

The kit belongs to the main thread. Package `bui` locks the main goroutine to
it in `init`, so `main` and every callback run there. From other goroutines,
use `bui.RunOnMain(fn)`, or `window.Update(fn)`, which runs fn on the main
thread and draws a frame:

```go
go func() {
	items, err := fetchItems()
	win.Update(func() { app.items, app.err = items, err })
}()
```

## The `ui` package: MyGo's syntax

A view is a method of your state that takes the window's `*ui.Context`. It
builds the interface of one frame; BUI calls it again after input, after
`c.Invalidate()` and after `window.Invalidate()` or `Update`.

### Elements and children

Constructors take the context first and return an `ui.Element`, whose methods
style it and ask about input. Containers take their children in a callback:

```go
ui.Column(c).Padding(16).Gap(8).Children(func() {
	ui.Text(c, "Documents").Bold()
	ui.Row(c).Gap(6).Children(func() {
		ui.TextInput(c, &app.query).Placeholder("Search").Grow(1)
		ui.Button(c, "Go")
	})
})
```

| Constructor | Kit node | Notes |
| --- | --- | --- |
| `Column`, `Box`, `Row` | `frame` | flexbox-like rows and columns, below |
| `Scroll` | `scroll` | a column that scrolls |
| `Collapsible(c, label, &open, fn)` | `panel` | one of Blender's panels |
| `RadioGroup(c, fn)` | `frame` | a column of radios |
| `Spacer`, `Divider` | `frame`, `separator` | |
| `Text`, `Textf` | `label` | |
| `Button`, `PrimaryButton` | `button` | the primary one is Blender's default button |
| `Checkbox(c, &b, label)`, `Switch(c, &b)` | `checkbox` | Blender has no switch: it is a check box |
| `Toggle(c, &b, label)` | `toggle` | |
| `Radio(c, &sel, value, label)` | `radio` | any comparable type |
| `Select(c, &s, options)` | `dropdown` | |
| `Slider(c, &v, lo, hi)`, `StepSlider(..., step)` | `slider` | Blender's number slider: drag, Ctrl snaps, click to type |
| `NumberInput(c, &v, lo, hi, step)` | `number` | |
| `TextInput(c, &s)`, `TextArea(c, &s)` | `text`, `textarea` | |
| `Progress(c, v)` | `progress` | 0..1 |
| `Image(c, bitmap)` | `image` | `ui.DecodeBitmap(data)` once, outside the view |

### Input

Controls take a pointer to what they edit and write the user's edit through
it as their constructor returns, so the next statement reads it, as in MyGo. A value the app
changes shows in the control on the next build.

```go
if ui.Checkbox(c, &app.showHidden, "Show hidden files").Changed() {
	app.reload()
}
if ui.TextInput(c, &app.name).Submitted() { // Return
	app.rename()
}
if ui.Button(c, "Save").Clicked() {
	app.save()
}
ui.Button(c, "Delete").OnClick(func() { app.remove(item.ID) }) // after the view built
```

`Clicked`, `Changed` and `Submitted` report input in the pass that took it.
`OnClick`, `OnChange` and `OnSubmit` run after the view has built, which is
the place to change a collection the view is looping over. A pass that took
input is built once more before the frame is drawn, so the frame shows the
outcome.

`c.Shortcut(mods, key)` reports a key combination since the last build:
`c.Shortcut(ui.Ctrl, ui.KeyS)`, `c.Shortcut(0, ui.KeyUp)`.

### Keys

An element keeps its node, and so its state (focus, text being edited, a
panel's animation), by its place among its siblings of its kind. Give items
of a collection a key with `c.Key` before the constructor, so their elements
follow them when the collection changes:

```go
for _, item := range app.items {
	ui.Row(c.Key(item.ID)).Children(func() {
		ui.Checkbox(c, &item.Done, item.Title)
	})
}
```

Keys compare as map keys do; a key Go cannot compare compares by its value
printed.

### Layout

Rows place their children left to right, centered vertically; columns stack
them and stretch them to their width. Sizes are in DIPs, scaled with the
window.

- `Gap`, `Padding(v...)` (1, 2 or 4 values, as CSS), `Width`, `Height`, `Size`.
- `Grow(f)` takes the free space along the parent's direction (any factor
  above 0 takes an equal share); `Spacer(c)` is an empty one.
- `Fill`, `FillWidth`, `FillHeight` take the parent's box.
- `Justify(Start | Center | End | SpaceBetween | SpaceAround | SpaceEvenly)`,
  `AlignItems(Start | Center | End | Stretch)`, `AlignSelf`, and `Center()`
  for both. A size of its own across the direction wins over `Stretch`, as
  in CSS.

They are laid out by the kit's packer (Tk's), so only what changed is laid
out again.

### Look

`Bold`, `FontSize` (Blender's UI text is 11), `TextColor`, `Background` (rows
and columns), `Tooltip`, `Disabled`, `Hidden`, `Label` (sliders, number and
text inputs and selects show it as Blender's fields do: "Volume: 50"),
`Placeholder` and `Password`. Colors are `ui.RGB`, `ui.RGBA`, `ui.Hex`, and
`c.Theme()` gives the active Blender theme's by MyGo's names (`Background`,
`Surface`, `Border`, `Text`, `TextMuted`, `Accent`, `AccentText`).

### Tests

`ui.NewTester(view, width, height)` runs a view on a software raster.
`Click(text or label)`, `ClickAt(x, y)`, `Key(mods, key)`, `Type(text)`,
`HasText`, `Texts` and `Frame` drive it; MyGo's own counter test runs
unchanged on it (`examples/counter/main_test.go`).

### What is not there

The look is Blender's, so MyGo's styling of boxes (radius, borders, shadows,
gradients, materials, transforms, transitions), its custom-widget bases
(`ButtonBase`, `SliderBase`...), overlays, lists that build only the rows in
view, grids, `Margin`, `Absolute` positioning and the web-view content are
not part of this package. Unknown parts of a view are a compile error, not a
silent difference. Use package `bui` for the rest of the kit (menus, trees,
canvases, the file browser, Blender's editors).

## The `bui` package: the kit

```go
root := bui.NewNode("column").Set("padding", 8)

size := bui.NewNode("slider").Set("label", "Size").Set("min", 0).Set("max", 10).Set("value", 2.5)
size.OnChange(func(e bui.Event) { fmt.Println("size =", e.Node.Num("value")) })
root.Add(size)

apply := bui.NewNode("button").Set("text", "Apply")
apply.OnClick(func(bui.Event) { bui.Quit() })
root.Add(apply)

bui.NewWindow(bui.WindowOptions{Title: "BUI from Go", Width: 320, Height: 120, Root: root})
bui.Run()
```

- **Nodes**: `NewNode(type)`, `Add`, `Insert`, `Remove`, `Child`,
  `ChildCount`, `Parent`, `Rect`, `Natural`, `Free` (a root; children go with
  it). Properties by name: `Set(key, value)` for numbers, bools, strings,
  `[]string` and `Color`, or `SetNum`, `SetStr`, `SetColor`, `SetStrs`, and
  `Num`, `Str`, `Color`, `Strs` to read. The node types and keys are the C
  kit's: see [the C API](../../docs/c-api.md).
- **Events**: `OnClick`, `OnChange`, `OnSubmit`, `On(kind, fn)`, and
  `Context.OnEvent` for all of them.
- **Geometry** in a `frame`: `Pack`, `Grid` and `Place` (Tk's managers), and
  `GridWeight`.
- **Contexts**: `NewContext(root)`, `Render` into a `Raster`, `Configure`,
  input (`Press`, `Release`, `Motion`, `Click`, `Wheel`, `Scroll`, `Key`,
  `ContextMenu`), `Bind("ctrl+s", fn)`, `HitAt`, `Hover`, `Focus`,
  `FocusNode`, `Popups`, `Undo`, `Redo`, the clipboard.
- **Windows**: `NewWindow(WindowOptions)` with a `Root`, a `Context` or a
  `Content`, `Show`, `Close`, `SetTitle`, `SetSize`, `Size`, `UseGPU`, `Timer`,
  `RequestFrame`, `OnFrame`, `OnClose`, `OnFocus`, `WM` and `WMGet` (Tk's
  `wm`), `Update`, `Invalidate`; `Run`, `Pump`, `Quit`, `RunOnMain`, and
  `App.WhenReady` / `App.Run`.
- **Menus**: `node.SetMenu([]bui.MenuItem{...})` on a `menubutton`,
  `Context.OpenMenu`, `Context.OpenPie`; items run Go functions.
- **Images**: `DecodeImage`, `LoadImage`, `ImageFromRGBA`, and on nodes
  `SetImage`, `SetImBuf`, `SetImageSource` (decoded in the background).
- **Canvas**: `AddItem`, `SetItem`, `PutItem` / `TruncateItems` for
  immediate-mode lists, `DeleteItem`.
- **Text areas**: `TextInsert`, `TextDelete`, `Caret`, `Selection`, `See`.
- **Trees**: `NewTreeItem`, `Add`, `SetValues`, `SetTreeRoots`, `TreeSelected`.
- **Look**: `SetTheme`, `LoadThemeJSON`, `LoadThemeFile`, `ThemeColor`,
  `SetThemeColor`, `SetScale`, `SetFont`, `SetFontSize`, `Icon(name)`.
- **More**: `FileDialog` (Blender's File Browser in its own window),
  `EvalNumber` (Blender's number expressions), `Version`.

The kit holds integers, never Go pointers: a context's host data, a window's
callbacks and the refs of menu items and key bindings are ids into tables of
the package, and the kit's retain and release hooks drop a menu's functions
once no menu holds them.

## Platforms

Windows (Win32, Direct3D 11 or software), Linux (X11 and Wayland; the kit
loads them at run time, so a headless program needs neither) and macOS
(Cocoa). Tested on Windows 11 with MSYS2's gcc 15.2, in native windows on
both renderers; CI runs the headless tests on Ubuntu and Windows. macOS links
the frameworks the Rust crate links but has not been built yet.
