//! blenderui in a web page.
//!
//! The app is an ordinary blenderui app compiled to `wasm32-wasip1` (a `cdylib`). It runs off-screen:
//! this crate keeps it alive between calls, the page's JavaScript (`js/bui-web.js`) calls in on every
//! animation frame and for every input event, and the frame the app draws comes back as an RGBA buffer in
//! the module's memory for the page to put on a `<canvas>`.
//!
//! ```ignore
//! use blenderui::prelude::*;
//!
//! struct Counter { count: i32 }
//! impl Render for Counter { /* ... as on a desktop ... */ }
//!
//! blenderui_web::export!(|cx, options| {
//!     cx.open_window(options, |_, cx| cx.new(|_| Counter { count: 0 })).unwrap();
//! });
//! ```
//!
//! `export!` makes the functions JavaScript calls (`bui_start`, `bui_frame`, ...). The rest of this
//! crate is the plain Rust behind them, which a custom host can call itself.

use std::cell::Cell;

use blenderui::prelude::*;

/// What the host keeps between calls: the application, its window and the last frame as RGBA bytes.
struct Host {
    app: App,
    window: WindowId,
    rgba: Vec<u8>,
    size: (usize, usize),
}

thread_local! {
    /// The one application of this page. Each cell's value is moved out while used (no `RefCell`).
    static HOST: Cell<Option<Host>> = const { Cell::new(None) };
}

fn with_host<R>(f: impl FnOnce(&mut Host) -> R) -> Option<R> {
    let mut host = HOST.take()?;
    let result = f(&mut host);
    HOST.set(Some(host));
    Some(result)
}

/// Starts the application in a canvas of `width` x `height` CSS pixels at `scale` device pixels per CSS
/// pixel. `build` opens the window with the options it is given (the canvas's size). False when it did not
/// open one, or an application is already running.
pub fn start(width: u32, height: u32, scale: f32, build: impl FnOnce(&mut App, WindowOptions)) -> bool {
    if with_host(|_| ()).is_some() {
        return false;
    }
    // the kit's resolution scale: a retina canvas draws twice the pixels
    // SAFETY: a plain setter, called before any window exists
    unsafe { blenderui_sys::BK_set_scale(scale) };
    let options = WindowOptions {
        window_bounds: Some(WindowBounds::Windowed(Bounds {
            origin: point(px(0.0), px(0.0)),
            size: size(px(width as f32), px(height as f32)),
        })),
        ..Default::default()
    };
    let app = Application::headless().start(|cx| build(cx, options));
    let Some(window) = app.window_ids().first().copied() else {
        return false;
    };
    let device = ((width as f32 * scale).round() as usize, (height as f32 * scale).round() as usize);
    HOST.set(Some(Host { app, window, rgba: Vec::new(), size: device }));
    true
}

/// The canvas changed size (CSS pixels) or pixel ratio.
pub fn resize(width: u32, height: u32, scale: f32) {
    with_host(|h| {
        let device = ((width as f32 * scale).round() as usize, (height as f32 * scale).round() as usize);
        h.app.resize_window(h.window, device.0, device.1, scale);
        h.size = device;
    });
}

/// Draws the next frame if anything changed (or something is animating). True when there is a new frame:
/// its pixels are at [`pixels`].
#[must_use]
pub fn frame() -> bool {
    with_host(|h| {
        let Some((w, ht, bgr)) = h.app.frame_if_needed(h.window) else {
            return false;
        };
        h.size = (w, ht);
        h.rgba.resize(bgr.len() * 4, 0);
        // the kit's 0x00RRGGBB words as the canvas's R, G, B, A bytes (a fixed-size chunk per pixel, so
        // there is no capacity check in the loop and the compiler can vectorize it)
        for (out, p) in h.rgba.chunks_exact_mut(4).zip(bgr) {
            out.copy_from_slice(&[(p >> 16) as u8, (p >> 8) as u8, p as u8, 255]);
        }
        true
    })
    .unwrap_or(false)
}

/// The last frame: a pointer to `width * height * 4` RGBA bytes, valid until the next [`frame`].
#[must_use]
pub fn pixels() -> *const u8 {
    with_host(|h| h.rgba.as_ptr()).unwrap_or(std::ptr::null())
}

/// The frame's width in device pixels.
#[must_use]
pub fn width() -> u32 {
    with_host(|h| h.size.0 as u32).unwrap_or(0)
}

/// The frame's height in device pixels.
#[must_use]
pub fn height() -> u32 {
    with_host(|h| h.size.1 as u32).unwrap_or(0)
}

/// The pointer moved (CSS pixels from the canvas's corner).
pub fn pointer_move(x: f32, y: f32) {
    with_host(|h| h.app.simulate_mouse_move(h.window, x, y));
}

/// The primary button went down.
pub fn pointer_down(x: f32, y: f32) {
    with_host(|h| h.app.simulate_press(h.window, x, y));
}

/// The primary button went up.
pub fn pointer_up(x: f32, y: f32) {
    with_host(|h| h.app.simulate_release(h.window, x, y));
}

/// The wheel turned: positive `lines` are wheel-up (the content scrolls toward its start), negative
/// are wheel-down, as on a desktop.
pub fn wheel(x: f32, y: f32, lines: i32) {
    with_host(|h| h.app.simulate_scroll(h.window, x, y, lines));
}

/// A key went down: the text it types (empty for none) and its X11 key name (`Return`, `BackSpace`,
/// `Left`, `a`), with the modifiers held.
pub fn key(text: &str, keysym: &str, ctrl: bool, shift: bool) {
    with_host(|h| h.app.simulate_key_with(h.window, text, keysym, ctrl, shift));
}

/// The page is done with the app (its script stopped or trapped): frees the window and everything the
/// kit holds for it. Calls after this do nothing, and a new [`start`] is allowed.
pub fn stop() {
    // dropping the host drops the app, which frees its windows
    drop(HOST.take());
}

/// Gives JavaScript a buffer to write a string into (a key's text): its address, `len` bytes long.
/// Free it with [`free_buffer`] with the same length.
#[must_use]
pub fn alloc_buffer(len: usize) -> *mut u8 {
    let mut v = vec![0u8; len].into_boxed_slice();
    let p = v.as_mut_ptr();
    std::mem::forget(v);
    p
}

/// Frees what [`alloc_buffer`] gave.
///
/// # Safety
/// `ptr` and `len` must be what [`alloc_buffer`] returned and was given, and not freed before.
pub unsafe fn free_buffer(ptr: *mut u8, len: usize) {
    // SAFETY: by the contract above, this is the boxed slice `alloc_buffer` made
    drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(ptr, len)) });
}

/// A string JavaScript wrote into a buffer.
///
/// # Safety
/// `ptr` and `len` must describe bytes that are valid for reads.
#[must_use]
pub unsafe fn read_str(ptr: *const u8, len: usize) -> String {
    // SAFETY: by the contract above
    String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(ptr, len) }).into_owned()
}

unsafe extern "C" {
    /// wasi-libc's constructors: `cdylib`s have no `_start`, so nobody runs them before the first call.
    #[cfg(target_os = "wasi")]
    fn __wasm_call_ctors();
}

/// Runs wasi-libc's initialization once (a library module has no `_start` to do it).
#[doc(hidden)]
pub fn init_libc() {
    #[cfg(target_os = "wasi")]
    {
        thread_local! { static DONE: Cell<bool> = const { Cell::new(false) }; }
        if !DONE.replace(true) {
            // SAFETY: wasm-ld provides it; it is called once, before anything else
            unsafe { __wasm_call_ctors() };
        }
    }
}

/// Defines what JavaScript calls (`js/bui-web.js`) for an app whose window is opened by the closure
/// `|cx: &mut App, options: WindowOptions| { ... }`.
#[macro_export]
macro_rules! export {
    ($build:expr) => {
        /// Starts the app in a canvas of this size; 1 when it opened a window.
        #[unsafe(no_mangle)]
        pub extern "C" fn bui_start(width: u32, height: u32, scale: f32) -> i32 {
            $crate::init_libc();
            i32::from($crate::start(width, height, scale, $build))
        }

        #[unsafe(no_mangle)]
        pub extern "C" fn bui_resize(width: u32, height: u32, scale: f32) {
            $crate::resize(width, height, scale);
        }

        /// 1 when there is a new frame at `bui_pixels`.
        #[unsafe(no_mangle)]
        pub extern "C" fn bui_frame() -> i32 {
            i32::from($crate::frame())
        }

        #[unsafe(no_mangle)]
        pub extern "C" fn bui_pixels() -> *const u8 {
            $crate::pixels()
        }

        #[unsafe(no_mangle)]
        pub extern "C" fn bui_width() -> u32 {
            $crate::width()
        }

        #[unsafe(no_mangle)]
        pub extern "C" fn bui_height() -> u32 {
            $crate::height()
        }

        #[unsafe(no_mangle)]
        pub extern "C" fn bui_pointer_move(x: f32, y: f32) {
            $crate::pointer_move(x, y);
        }

        #[unsafe(no_mangle)]
        pub extern "C" fn bui_pointer_down(x: f32, y: f32) {
            $crate::pointer_down(x, y);
        }

        #[unsafe(no_mangle)]
        pub extern "C" fn bui_pointer_up(x: f32, y: f32) {
            $crate::pointer_up(x, y);
        }

        #[unsafe(no_mangle)]
        pub extern "C" fn bui_wheel(x: f32, y: f32, lines: i32) {
            $crate::wheel(x, y, lines);
        }

        /// The page stopped: frees the app.
        #[unsafe(no_mangle)]
        pub extern "C" fn bui_stop() {
            $crate::stop();
        }

        #[unsafe(no_mangle)]
        pub extern "C" fn bui_alloc(len: usize) -> *mut u8 {
            $crate::alloc_buffer(len)
        }

        /// A key: `text` and `keysym` are UTF-8 the page wrote with `bui_alloc`; they are freed here.
        #[unsafe(no_mangle)]
        pub unsafe extern "C" fn bui_key(
            text: *mut u8,
            text_len: usize,
            keysym: *mut u8,
            keysym_len: usize,
            ctrl: i32,
            shift: i32,
        ) {
            // SAFETY: the page wrote these buffers with `bui_alloc` and passes each back once
            unsafe {
                let (t, k) = ($crate::read_str(text, text_len), $crate::read_str(keysym, keysym_len));
                $crate::free_buffer(text, text_len);
                $crate::free_buffer(keysym, keysym_len);
                $crate::key(&t, &k, ctrl != 0, shift != 0);
            }
        }
    };
}
