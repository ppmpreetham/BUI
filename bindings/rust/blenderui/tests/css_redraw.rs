//! Frames of CSS animations whose values only paint (colour, opacity,
//! translate, scale ...) are redraws, as cheap as Motion's: the pixels move,
//! the view doesn't render, cascade or lay out again. An animation of a
//! value that changes layout still renders every frame.

use std::sync::atomic::{AtomicU32, Ordering::Relaxed};
use std::time::Duration;

use blenderui::prelude::*;

static PAINT_RENDERS: AtomicU32 = AtomicU32::new(0);
static LAYOUT_RENDERS: AtomicU32 = AtomicU32::new(0);

const CSS: &str = "
    html, body { margin: 0; background: #000 }
    .b { position: absolute; width: 50px; height: 50px }
    #c { left: 0; top: 0; animation: fade 1s linear both }
    #t { left: 0; top: 60px; background: #fff; animation: slide 1s linear both }
    #s { left: 200px; top: 150px; background: #fff; animation: grow 1s linear both }
    #w { left: 0; top: 0; height: 20px; background: #fff; animation: widen 1s linear both }
    @keyframes fade { from { background-color: #ff0000 } to { background-color: #0000ff } }
    @keyframes slide { from { transform: translateX(0px) } to { transform: translateX(100px) } }
    @keyframes grow { from { scale: 1 } to { scale: 3 } }
    @keyframes widen { from { width: 10px } to { width: 110px } }
";

struct Paints;

impl Render for Paints {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        PAINT_RENDERS.fetch_add(1, Relaxed);
        body().child(style().child(CSS))
            .child(div().id("c").class("b"))
            .child(div().id("t").class("b"))
            .child(div().id("s").class("b"))
    }
}

struct Layouts;

impl Render for Layouts {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        LAYOUT_RENDERS.fetch_add(1, Relaxed);
        body().child(style().child(CSS)).child(div().id("w").class("b"))
    }
}

fn open<V: Render>(cx: &mut App, v: V) -> WindowId {
    let opts = WindowOptions {
        window_bounds: Some(WindowBounds::Windowed(Bounds {
            origin: point(px(0.0), px(0.0)),
            size: size(px(400.0), px(300.0)),
        })),
        ..Default::default()
    };
    cx.open_window(opts, |_, cx| cx.new(|_| v)).unwrap().id
}

fn pixel(cx: &mut App, w: WindowId, x: usize, y: usize) -> (u32, u32, u32) {
    let (pw, _, px) = cx.draw(w);
    let p = px[y * pw + x];
    ((p >> 16) & 255, (p >> 8) & 255, p & 255)
}

/// Advance the clock to `ms` milliseconds in 50 ms frames.
fn run_to(cx: &mut App, w: WindowId, from: u64, ms: u64) {
    for _ in (from..ms).step_by(50) {
        cx.advance_clock(Duration::from_millis(50));
        cx.draw(w);
    }
}

#[test]
fn paint_animations_are_redraws() {
    Application::headless().run(|cx| {
        let w = open(cx, Paints);
        cx.draw(w);
        let rendered = PAINT_RENDERS.load(Relaxed);
        run_to(cx, w, 0, 500);
        let (r, _, b) = pixel(cx, w, 25, 25);
        assert!(r.abs_diff(128) <= 8 && b.abs_diff(128) <= 8, "half way from red to blue: {r} {b}");
        // translateX(50px) half way: the box covers 50..100
        assert_eq!(pixel(cx, w, 75, 85), (255, 255, 255), "the slid box is half way");
        assert_eq!(pixel(cx, w, 25, 85), (0, 0, 0), "and left its start");
        // scale 2 half way, about the centre (225, 175): 50px grows to 100px
        assert_eq!(pixel(cx, w, 180, 175), (255, 255, 255), "the box grew");
        assert_eq!(PAINT_RENDERS.load(Relaxed), rendered, "no frame rendered the view again");
        run_to(cx, w, 500, 1100);
        assert_eq!(pixel(cx, w, 125, 85), (255, 255, 255), "the slide ends at 100px");
        assert!(!cx.window(w).unwrap().is_animating(), "and stops asking for frames");
    });
}

#[test]
fn layout_animations_still_render() {
    Application::headless().run(|cx| {
        let w = open(cx, Layouts);
        cx.draw(w);
        let rendered = LAYOUT_RENDERS.load(Relaxed);
        run_to(cx, w, 0, 500);
        assert!(LAYOUT_RENDERS.load(Relaxed) > rendered, "a width animation lays out again");
        assert_eq!(pixel(cx, w, 55, 10), (255, 255, 255), "half way: 60px wide");
        assert_eq!(pixel(cx, w, 65, 10), (0, 0, 0));
    });
}

struct Over;

impl Render for Over {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        body().child(style().child("html, body { margin: 0; background: #000 }
            #g { position: absolute; left: 0; top: 0; width: 200px; height: 50px; display: flex;
                 animation: over 0.5s linear forwards }
            #k { width: 50px; height: 50px; background: #fff }
            @keyframes over { 0% { justify-content: flex-start } 100% { justify-content: flex-end } }"))
            .child(div().id("g").child(div().id("k")))
    }
}

/// A discrete value at an explicit 100% key is the one that holds at the end
/// (`forwards`), not the underlying one.
#[test]
fn an_explicit_last_discrete_key_holds() {
    Application::headless().run(|cx| {
        let w = open(cx, Over);
        assert_eq!(pixel(cx, w, 25, 25), (255, 255, 255), "at the start");
        run_to(cx, w, 0, 700);
        assert_eq!(pixel(cx, w, 175, 25), (255, 255, 255), "justify-content: flex-end at 100% holds");
    });
}
