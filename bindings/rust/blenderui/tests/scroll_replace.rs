//! Scrolling alone places the last frame again (no render, no layout); a view that reads scroll
//! positions renders again. Both end on the pixels a full render of the new offset draws.

use std::sync::atomic::{AtomicU32, Ordering::Relaxed};

use blenderui::prelude::*;

/// How often a view rendered (counted at compile time: no shared ownership).
static PLAIN: AtomicU32 = AtomicU32::new(0);
static READS: AtomicU32 = AtomicU32::new(0);

fn rows(label: String) -> impl IntoElement {
    let mut col = div().flex().flex_col();
    for i in 0..60 {
        col = col.child(div().id(format!("r{i}")).h(px(20.0)).bg(rgb(if i % 2 == 0 { 0xff0000 } else { 0x0000ff })).child(format!("{label} {i}")));
    }
    body().m_0().child(div().id("sc").w(px(200.0)).h(px(100.0)).overflow_y_scroll().child(col))
}

struct Plain;

impl Render for Plain {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        PLAIN.fetch_add(1, Relaxed);
        rows("row".into())
    }
}

/// Shows the scroll position it read.
struct Reads;

impl Render for Reads {
    fn render(&mut self, window: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        READS.fetch_add(1, Relaxed);
        rows(format!("at {}", window.scroll_offset("sc").1))
    }
}

fn open<V: Render>(cx: &mut App, v: V) -> WindowHandle<V> {
    let opts = WindowOptions {
        window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(200.0), px(150.0)) })),
        ..Default::default()
    };
    cx.open_window(opts, |_, cx| cx.new(|_| v)).unwrap()
}

#[test]
fn scrolling_places_again_without_rendering() {
    Application::headless().run(|cx| {
        let (ha, hb) = (open(cx, Plain), open(cx, Plain));
        let (a, b) = (ha.id, hb.id);
        cx.draw(a);
        cx.draw(b);
        let renders = PLAIN.load(Relaxed);
        assert_eq!(cx.element_bounds(a, "r10").unwrap().origin.y, 200.0);
        cx.simulate_scroll(a, 50.0, 50.0, -2); // the wheel's two lines down: 80 px
        let (_, _, scrolled) = cx.draw(a);
        assert_eq!(PLAIN.load(Relaxed), renders, "the view did not render for a scroll");
        assert_eq!(cx.element_bounds(a, "r10").unwrap().origin.y, 120.0, "rows moved with the offset");
        // the same offset through a full render (`set_scroll_offset` renders)
        hb.update(cx, |_, window, _| window.set_scroll_offset("sc", 0.0, 80.0));
        let (_, _, full) = cx.draw(b);
        assert_eq!(PLAIN.load(Relaxed), renders + 1, "set_scroll_offset renders");
        assert!(scrolled == full, "the pixels of a placed-again frame are those of a render");
        // and back to the top
        cx.simulate_scroll(a, 50.0, 50.0, 2);
        cx.draw(a);
        assert_eq!(cx.element_bounds(a, "r10").unwrap().origin.y, 200.0);
        assert_eq!(PLAIN.load(Relaxed), renders + 1, "still no render for a's scrolls");
    });
}

#[test]
fn a_view_that_reads_the_scroll_renders_again() {
    Application::headless().run(|cx| {
        let w = open(cx, Reads).id;
        cx.draw(w);
        let renders = READS.load(Relaxed);
        cx.simulate_scroll(w, 50.0, 50.0, -2);
        cx.draw(w);
        assert!(READS.load(Relaxed) > renders, "the view read the scroll position: it renders");
    });
}
