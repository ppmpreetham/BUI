# BUI for Ruby

The `blenderui` gem: Blender's interface for Ruby. A C extension (no FFI) over
BUI's C kit, with Blender's widgets, layouts, menus, trees and file browser in
native windows (Win32, X11, Wayland, Cocoa), the same interface rendered without
a window for tests, and a builder that makes interfaces from blocks.

```ruby
require "bui"

BUI.app do
  window "Hello", width: 320, height: 120 do
    count = 0
    column padding: 8 do
      shown = label "Clicked 0 times"
      button("Click me") { shown.text = "Clicked #{count += 1} times" }
    end
  end
end
```

## Build and test

Ruby 3.1 or newer, CMake 3.20 or newer and a C and C++20 compiler (on Linux the
X11 headers too, `libx11-dev`; on Windows RubyInstaller with its MSYS2 devkit).

```sh
bundle install
rake compile              # the kit and the extension, into lib/bui/
rake test                 # headless; BUI_TEST_WINDOWS=1 with a display adds the window tests
ruby -Ilib examples/counter.rb
rake gem                  # blenderui-<version>.gem, with the kit's C sources in it
```

`BUI_SANITIZE=address rake compile` builds with AddressSanitizer (Linux, macOS).
Ruby itself is not instrumented, so preload the runtime when running:

```sh
LD_PRELOAD=$(gcc -print-file-name=libasan.so) \
ASAN_OPTIONS=use_sigaltstack=0:detect_leaks=1 RUBY_FREE_AT_EXIT=1 rake test
```

(`use_sigaltstack=0`: Ruby and ASan both want the alternate signal stack.)

## The API

| | |
| --- | --- |
| `BUI::Node.new(type)` | A node: `"column"`, `"button"`, `"slider"`, `"text"`, `"tree"`, `"canvas"`... `node[:key] = value`, `set(key: value)`, `num` / `str` / `color` / `strs`, `<<`, `add(child, index)`, `remove`, `children`, `rect`, `free` |
| `node.on(:click) { \|e\| }` | Handlers by event kind (`BUI::EVENT_KINDS`); `on_click`, `on_change`, `on_submit`. The block gets a `BUI::Event` |
| `node.pack(side: :left)` | Tk's `pack`, `grid` and `place` for the children of a `"frame"` |
| `node.menu = [{text:, action:}]` | A menu button's items; `ctx.open_menu`, `ctx.open_pie` |
| `BUI::Context.new(root)` | Layout, input and rendering without a window: `render(raster)`, `click(x, y)`, `key(text, keysym)`, `bind("ctrl+s") {}`, `popups`, `hit_at` |
| `BUI::Raster.new(w, h)` | A frame: `rgba`, `pixel(x, y)`, `save_png(path)` |
| `BUI::Window.new(title, width:, height:) { }` | A native window; `timer(ms) {}`, `on_close {}`, `on_frame {}`, `update {}` from other threads, `free` |
| `BUI.run`, `BUI.app { window ... }` | The loop; `BUI.on_main {}` from any thread; `BUI.quit` |
| `BUI::Builder.new(node).build { }` | Blocks: `column`, `row`, `panel`, `frame`, `label`, `button`, `checkbox`, `slider`, `text_field`, `dropdown`, `menu_button`... |
| `BUI::Image`, `BUI::TreeItem` | Decoded images (`decode`, `load`, `from_rgba`) and tree rows; the GC frees both |
| `BUI.theme = "light"` | Themes (`load_theme_json`), `BUI.scale`, `BUI.icon(name)`, `BUI.eval_number("2*pi")`, `BUI.file_dialog` |

## How it is built

- `ext/bui/`: the extension in C over `BUI_kit.h` only (no internal headers), one
  file per part: `bui_ext.c` (the reference table, callbacks, errors, the module
  functions), `bui_node.c`, `bui_context.c`, `bui_window.c`, `bui_image.c`.
  `bui_ext.h` states the three rules every file keeps.
- `lib/`: what is Ruby-shaped (keyword options, events, the builder, PNG files).
- `extconf.rb` configures `ext/bui/CMakeLists.txt` with this Ruby's compiler and
  headers; CMake builds the kit as a subproject and the extension against it.

The kit holds Ruby objects only as integer ids into a table the GC marks, so
objects can move; Ruby code runs from the kit's callbacks under `rb_protect`,
and an exception comes out of the call that led to it, never through the kit's
C frames. The window loop gives up the GVL while it waits: other threads run,
and an interrupt stops the loop.

Not there yet: video, custom-drawn nodes, the animation engine, Dear ImGui.
The gem is not Ractor-safe (the kit has one thread).
