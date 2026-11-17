# BUI on Android

BUI runs on a phone as one `NativeActivity` (no Java, no Gradle) with the software raster, touch and the
soft keyboard. **The window is the kit's:** `kit/intern/kit_window_android.c` implements `BK_window_*`
the way `kit_window_win32.c` and `kit_window_x11.c` do, so an app opens a window and runs the loop
exactly as on a desktop. C, Lua and Rust apps are the same code they would be on Windows or Linux.

Nothing changes for other platforms: the Android code lives in `platform/android`, in the kit's
Android-only branches, and in `if(ANDROID)` blocks of CMake.

```
python platform/android/build.py                       # the C demo: arm64-v8a + x86_64 -> build-android/bui.apk
python platform/android/build.py --abi arm64-v8a       # a phone only
python platform/android/build.py --install             # adb install -r, then start it
python platform/android/build.py --lua examples/android        # a Lua app (the folder's *.lua are assets)
python platform/android/build.py --rust platform/android/rust  # a Rust app (a staticlib crate)
python platform/android/build.py --js app.jsx                  # a JavaScript app: React, Vue or Svelte on QuickJS
python platform/android/build.py --imgui [app.cpp]             # a Dear ImGui app (C++)
```

Needs the Android SDK (`ANDROID_HOME`; build-tools, platform 34), an NDK (the newest in the SDK, or
`ANDROID_NDK_HOME`), CMake, a JDK for `keytool`, and for Rust `cargo` with the Android targets
(`rustup target add aarch64-linux-android x86_64-linux-android`). The APK is signed with
`~/.android/debug.keystore`.

## Writing an app

| Language | You write | Run it |
| --- | --- | --- |
| **Rust** | an ordinary `blenderui` app in a `staticlib` crate with `#[unsafe(no_mangle)] pub extern "C" fn bui_app_main() { Application::new().run(...) }` (`platform/android/rust`) | `--rust CRATE` |
| **Lua** | an ordinary BUI Lua script, `assets/main.lua`, ending in `bui.open{root=...}` or `bui.Window(...)` + `bui.run()`; `require` finds your other `.lua` files and `bui.lua` in the APK | `--lua DIR` |
| **JavaScript** | an ordinary `@blender-ui/native`, React, Vue or Svelte app (`.js`, `.jsx`, `.vue`, `.svelte`); it is bundled into one script (`bui bundle`, needs Bun) and runs on QuickJS (`bindings/rust/blenderui-qjs`, the engine `bui build` embeds), as `assets/main.js`; `console.log` goes to logcat (tag `bui-js`) | `--js ENTRY` |
| **Dear ImGui** | an ordinary ImGui frame function in a C++ file with `bui_app_main` (`ImGui_ImplBUI_CreateApp` / `RunApp`; `platform/android/imgui_app.cpp`), plus `BK_window_wm(win, "touch_drag", 1, 0)` so a drag is ImGui's (sliders, window moves) | `--imgui [FILE]` |
| **C** | `void bui_app_main(void)`: build nodes, `BK_context_new`, `BK_window_new`, `BK_window_run` (`platform/android/demo.c`) | the default |

Python, Qt, tkinter and D are not available on Android: each needs its
runtime on the device.

The Rust demo (`platform/android/rust`) has three tabs: the widgets, a list of **10 000 000 rows** and a
chat of **100 000 messages** (`blenderui::vlist`: only the visible rows exist, the row text is computed
when it is fetched, a frame costs O(visible rows)). The emulator runs it at about 75 MB.

For Rust the staticlib carries its own copy of the kit (`blenderui-sys` builds it for the target), so
the app crate sets the kit's features itself (see its `Cargo.toml`: no GPU, ImGui or extra image
formats).

## Behavior

- **Damage driven.** Nothing is drawn and the thread sleeps in `ALooper` until a touch, key, timer, wake
  or frame request. Only the damaged rectangles are converted (B,G,R words to the surface's R,G,B bytes)
  and locked. Idle cost: 0 CPU ticks in 10 s on the emulator, for the C and the Rust app.
- **Scale.** A CSS pixel is a dp, as in Chrome on Android: the kit's scale is the screen density
  (`dpi / 160`), so Rust layout and Blender's 20-unit widgets come out at a phone's size.
- **Touch.** One finger is the pointer. A tap is a click (sent when the finger lifts in place). A drag
  that starts on a widget that drags (sliders, number fields, text, scrollbars, panels) drags it;
  anywhere else it scrolls. The content follows the finger exactly (`BK_scroll_px`: the distance in
  pixels, not a wheel notch per touch event) and keeps going after the finger lifts at the velocity of
  the last 100 ms, slowing down the way UIScrollView does (0.998 per millisecond); a touch stops it. A
  touch in the 20 dp strip at the right edge of a custom or canvas node is the app's press and drag
  (a list's scrollbar thumb), so a huge list can be dragged to any place. Two fingers always scroll. `BK_window_wm(win, "touch_drag", 1, 0)` gives
  the drags of custom and canvas nodes to the app instead (a paint app); by default they scroll.
- **Keyboard.** The soft keyboard opens while a text field or area is being edited (through the input
  method manager: the activity's view is not a text editor, so `ANativeActivity_showSoftInput` is
  ignored by current Android). Keys arrive as key events (`KeyEvent.getUnicodeChar`), so typing,
  Backspace, arrows and Back (Escape) work. Predictive and composing IMEs (`BK_ime_event`) need an
  `InputConnection` and are not wired up.
- **System bars.** The UI is drawn inside the activity's content rectangle; the soft keyboard resizes it
  (`adjustResize`).
- **Window.** One window, the surface. `BK_window_new` waits for the surface so the app builds its UI at
  the real size and scale. `BK_window_set_size` and most `BK_window_wm` keys do nothing;
  `BK_window_wm_get` answers `scale`, `screen` and `pointer`.

## Fonts

Inter is left out of the build (`BUI_NO_INTER`, 0.8 MB). The shell registers the system's fonts
(`platform/android/fonts.c`: Roboto and Droid Sans Mono from `/system/fonts` as the families `sans` and
`mono`). To use your own, call `BUI_text_add_font(family, flags, data, len)` with the font's bytes
(from `BK_android_assets()` or a file) and select it with `BK_set_font(family)`, as on any platform.

## Size

`-O2` with link-time optimization (C and Lua apps; Rust does its own), `-fvisibility=hidden`,
`--gc-sections`, stripped, GPU renderers, Dear ImGui, the file browser and every image format but PNG and
SVG off (`features.json`). arm64-v8a: the C demo is 2.5 MB (APK about 1.1 MB), with Lua 3.2 MB (APK 1.4
MB), with Rust 4.3 MB (APK 2.0 MB; blenderui's layout, CSS and Tailwind engines). The APK deflates the
library and the installer extracts it. `-Os` saved only 4%, so speed was kept. About 0.45 MB is the icon
set.

## Tested, and not

Run on an x86_64 emulator (API 33): the C demo, the Lua app (a button callback updates its label), the React, Vue and Svelte counters (QuickJS: clicks, the keyboard in an input, a slider) and the
Rust app (a button, a text input, scrolling with the scrollbar). The arm64-v8a builds of all three
compile and link but were not run on a device.

Not done: a GPU renderer (OpenGL ES would reuse `gpu_gl.c`, untested), long-press as right-click, pinch,
IME composition, a clipboard beyond the app's own, cutout insets beyond the content rectangle, and
touch text selection in Rust pages (a drag scrolls).
