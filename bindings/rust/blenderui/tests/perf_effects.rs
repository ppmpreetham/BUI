//! Frame times of the effects that used to be slow, on a page the size of a
//! long song's lyrics (300 lines of text): CSS transitions, animated scale on
//! text, blur (still, moving, animated, a wide backdrop). Headless, so drawn
//! by the software rasterizer. Run in release:
//! `cargo test --release --test perf_effects -- --ignored --nocapture --test-threads=1`

use std::fmt::Write as _;
use std::time::{Duration, Instant};

use blenderui::prelude::*;

const ROWS: usize = 300;

struct Scene {
    css: &'static str,
    /// The class on the body (a style change starts the transitions).
    on: bool,
    /// Rows `0..marked` get the class "m".
    marked: usize,
}

impl Render for Scene {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        let rows = (0..ROWS).map(|i| {
            let mut text = String::new();
            let _ = write!(text, "line {i} of the song with eight words");
            let row = div().class("row").child(text);
            if i < self.marked { row.class("m") } else { row }
        });
        let list = div().id("list").children(rows);
        let page = body().child(style().child(self.css)).child(div().id("back")).child(list);
        if self.on { page.class("on") } else { page }
    }
}

fn open(cx: &mut App, scene: Scene) -> WindowHandle<Scene> {
    let opts = WindowOptions {
        window_bounds: Some(WindowBounds::Windowed(Bounds {
            origin: point(px(0.0), px(0.0)),
            size: size(px(1280.0), px(800.0)),
        })),
        ..Default::default()
    };
    cx.open_window(opts, |_, cx| cx.new(|_| scene)).unwrap()
}

/// `frames` frames of 16 ms: BUI's work (the frame: render or redraw,
/// layers, the display list; `advance_clock` does it) apart from the
/// headless software raster of the whole window (`draw`; a native window
/// draws on the GPU, and only what changed). Means and the slowest, in ms.
struct Times {
    bui: (f64, f64),
    raster: f64,
}

fn frames(cx: &mut App, win: WindowId, frames: u32) -> Times {
    let (mut bui, mut worst, mut raster) = (0.0f64, 0.0f64, 0.0f64);
    for _ in 0..frames {
        let t = Instant::now();
        cx.advance_clock(Duration::from_millis(16));
        let ms = t.elapsed().as_secs_f64() * 1000.0;
        let t = Instant::now();
        cx.draw(win);
        raster += t.elapsed().as_secs_f64() * 1000.0;
        bui += ms;
        worst = worst.max(ms);
    }
    let n = f64::from(frames);
    Times { bui: (bui / n, worst), raster: raster / n }
}

fn report(name: &str, first: f64, t: Times) {
    eprintln!("PERF {name:<28} first {first:8.2}   bui mean {:7.2} worst {:7.2}   raster {:7.2} (ms)",
              t.bui.0, t.bui.1, t.raster);
}

fn first(cx: &mut App, win: WindowId) -> f64 {
    let t = Instant::now();
    cx.draw(win);
    t.elapsed().as_secs_f64() * 1000.0
}

const BASE: &str = "html, body { margin: 0; background: #111; color: #ddd }
    #list { position: absolute; left: 40px; top: 0; width: 700px }
    .row { font-size: 34px; font-weight: 700; padding: 8px 12px }";

fn scene(cx: &mut App, name: &str, extra: &'static str, marked: usize) {
    if std::env::var("SCENE").is_ok_and(|s| !name.contains(s.as_str())) {
        return;
    }
    let css: &'static str = Box::leak(format!("{BASE}\n{extra}").into_boxed_str());
    let h = open(cx, Scene { css, on: false, marked });
    let f = first(cx, h.id);
    report(name, f, frames(cx, h.id, 60));
    // a window left open keeps animating under the next scene
    cx.close_window(h.id);
}

#[test]
#[ignore = "benchmark"]
fn transitions_on_300_rows() {
    Application::headless().run(|cx| {
        let css: &'static str = Box::leak(format!("{BASE}
            .row {{ transition: opacity .7s, transform .7s, color .7s }}
            .on .row {{ opacity: .5; transform: translateY(-40px); color: #fff }}").into_boxed_str());
        let h = open(cx, Scene { css, on: false, marked: 0 });
        let f = first(cx, h.id);
        h.update(cx, |v, _, cx| {
            v.on = true;
            cx.notify();
        });
        report("transitions x300", f, frames(cx, h.id, 40));
    });
}

#[test]
#[ignore = "benchmark"]
fn scale_on_text() {
    Application::headless().run(|cx| {
        scene(cx, "scale rise x40 rows", "
            .m { animation: rise 1s ease-out infinite; transform-origin: left center }
            @keyframes rise { 0% { scale: .95 } 70% { scale: 1.025 } 100% { scale: 1 } }", 40);
    });
}

#[test]
#[ignore = "benchmark"]
fn blur() {
    Application::headless().run(|cx| {
        scene(cx, "blur 300 rows, still", ".row { filter: blur(2px) }", 0);
        scene(cx, "blur 7 rows, column moves", "
            .m { filter: blur(1.5px) }
            #list { animation: glide 2s linear infinite }
            @keyframes glide { from { transform: translateY(0px) } to { transform: translateY(-300px) } }", 7);
        scene(cx, "blur radius animated x7", "
            .m { animation: focus 1s linear infinite }
            @keyframes focus { from { filter: blur(3px) } to { filter: blur(0px) } }", 7);
        scene(cx, "backdrop blur 64px, still", "
            #back { position: absolute; left: -200px; top: -200px; width: 1680px; height: 1200px;
                    background: linear-gradient(45deg, #f40, #04f, #0c4); filter: blur(64px) }", 0);
        scene(cx, "backdrop blur 64px, turning", "
            #back { position: absolute; left: -200px; top: -200px; width: 1680px; height: 1200px;
                    background: linear-gradient(45deg, #f40, #04f, #0c4); filter: blur(64px);
                    animation: turn 20s linear infinite }
            @keyframes turn { from { rotate: 0deg } to { rotate: 360deg } }", 0);
        // a cover image (as a lyrics backdrop has) rather than a gradient
        let cover = concat!(env!("CARGO_MANIFEST_DIR"), "/../../../docs/images/components.png").replace('\\', "/");
        let back = format!("#back {{ position: absolute; left: -200px; top: -200px; width: 1680px; height: 1200px;
            background: url('{cover}') center / cover; filter: blur(64px) }}");
        scene(cx, "cover blur 64px, still", Box::leak(back.clone().into_boxed_str()), 0);
        let turning = format!("{back}
            #back {{ animation: turn 20s linear infinite }}
            @keyframes turn {{ from {{ rotate: 0deg }} to {{ rotate: 360deg }} }}");
        scene(cx, "cover blur 64px, turning", Box::leak(turning.into_boxed_str()), 0);
    });
}
