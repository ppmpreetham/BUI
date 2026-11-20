//! `Window::scroll_into_view`: an element brought to a place in its scroll
//! container after the frame's layout, in the same frame for a jump, and
//! through scroll frames (no render) for a glide; a wheel stops a glide.

use std::sync::atomic::{AtomicU32, Ordering::Relaxed};
use std::time::Duration;

use blenderui::prelude::*;

static RENDERS: AtomicU32 = AtomicU32::new(0);

struct List {
    target: usize,
    glide: Option<f32>,
}

impl Render for List {
    fn render(&mut self, window: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        RENDERS.fetch_add(1, Relaxed);
        window.scroll_into_view(&format!("row{}", self.target), 0.3, self.glide);
        let rows = (0..60).map(|i| div().id(format!("row{i}")).class("row").child(format!("row {i}")));
        body().child(style().child("html, body { margin: 0 }
            #list { height: 400px; overflow-y: scroll; scrollbar-width: none }
            .row { height: 40px }"))
            .child(div().id("list").children(rows))
    }
}

fn open(cx: &mut App, list: List) -> WindowHandle<List> {
    let opts = WindowOptions {
        window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(300.0), px(400.0)) })),
        ..Default::default()
    };
    cx.open_window(opts, |_, cx| cx.new(|_| list)).unwrap()
}

/// Where row `i` is, from the top of the list.
fn top(cx: &mut App, w: WindowId, i: usize) -> f32 {
    cx.element_bounds(w, &format!("row{i}")).unwrap().origin.y
}

#[test]
fn a_jump_lands_in_the_frame_it_was_asked_in() {
    Application::headless().run(|cx| {
        let h = open(cx, List { target: 30, glide: None });
        cx.draw(h.id);
        // 30% down a 400 px list: 120 px
        assert_eq!(top(cx, h.id, 30), 120.0);
    });
}

#[test]
fn a_glide_moves_through_scroll_frames_and_a_wheel_stops_it() {
    Application::headless().run(|cx| {
        let h = open(cx, List { target: 0, glide: Some(0.5) });
        cx.draw(h.id);
        assert_eq!(top(cx, h.id, 0), 0.0, "row 0 can't go lower than the top");
        h.update(cx, |v, _, cx| {
            v.target = 30;
            cx.notify();
        });
        cx.draw(h.id);
        let rendered = RENDERS.load(Relaxed);
        cx.advance_clock(Duration::from_millis(100));
        cx.draw(h.id);
        let midway = top(cx, h.id, 30);
        assert!(midway < 1200.0 && midway > 120.0, "on its way: {midway}");
        for _ in 0..10 {
            cx.advance_clock(Duration::from_millis(50));
            cx.draw(h.id);
        }
        assert_eq!(top(cx, h.id, 30), 120.0, "there");
        assert_eq!(RENDERS.load(Relaxed), rendered, "the glide rendered nothing");
        // a new target, then the wheel: the glide stops where the wheel left it
        h.update(cx, |v, _, cx| {
            v.target = 50;
            cx.notify();
        });
        cx.draw(h.id);
        cx.advance_clock(Duration::from_millis(50));
        cx.draw(h.id);
        cx.simulate_scroll(h.id, 100.0, 100.0, 1);
        let stopped = top(cx, h.id, 50);
        cx.advance_clock(Duration::from_millis(400));
        cx.draw(h.id);
        assert_eq!(top(cx, h.id, 50), stopped, "the wheel stopped the glide");
    });
}

/// The row sits in a strip that scrolls only sideways, inside the list: the
/// list (the nearest container that scrolls vertically) is the one that moves.
struct Strip;

impl Render for Strip {
    fn render(&mut self, window: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        window.scroll_into_view("deep", f32::NAN, Some(f32::INFINITY));
        let rows = (0..60).map(|i| {
            let strip = div().class("strip").child(div().class("wide").when(i == 40, |d| d.id("deep")).child(format!("row {i}")));
            div().class("row").child(strip)
        });
        body().child(style().child("html, body { margin: 0 }
            #list { height: 400px; overflow-y: scroll }
            .row { height: 40px }
            .strip { overflow-x: scroll; overflow-y: hidden; height: 40px }
            .wide { width: 2000px; height: 40px }"))
            .child(div().id("list").children(rows))
    }
}

#[test]
fn a_sideways_scroller_does_not_stop_it_and_bad_numbers_are_a_jump_to_the_top() {
    Application::headless().run(|cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(300.0), px(400.0)) })),
            ..Default::default()
        };
        let h = cx.open_window(opts, |_, cx| cx.new(|_| Strip)).unwrap();
        cx.draw(h.id);
        // NaN `at` is 0 (the top), an infinite glide a jump
        assert_eq!(cx.element_bounds(h.id, "deep").unwrap().origin.y, 0.0);
    });
}

/// A list gliding to a row next to a CSS animation: the glide's scroll frames
/// move the animation on too (it doesn't wait for the glide to end).
struct GlideBeside {
    target: usize,
}

impl Render for GlideBeside {
    fn render(&mut self, window: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        window.scroll_into_view(&format!("row{}", self.target), 0.3, Some(1.0));
        let rows = (0..60).map(|i| div().id(format!("row{i}")).class("row").child(format!("row {i}")));
        body().child(style().child("html, body { margin: 0 }
            #list { height: 400px; width: 200px; overflow-y: scroll; scrollbar-width: none }
            .row { height: 40px }
            #a { position: absolute; left: 220px; top: 0; width: 20px; height: 20px; background: #f00;
                 animation: slide 2s linear both }
            @keyframes slide { from { transform: translateX(0px) } to { transform: translateX(100px) } }"))
            .child(div().id("list").children(rows))
            .child(div().id("a"))
    }
}

#[test]
fn animations_move_on_while_a_glide_scrolls() {
    Application::headless().run(|cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(400.0), px(400.0)) })),
            ..Default::default()
        };
        let h = cx.open_window(opts, |_, cx| cx.new(|_| GlideBeside { target: 0 })).unwrap();
        cx.draw(h.id);
        h.update(cx, |v, _, cx| {
            v.target = 30;
            cx.notify();
        });
        cx.draw(h.id);
        let x = |cx: &mut App| cx.element_bounds(h.id, "a").unwrap().origin.x;
        let (start, row) = (x(cx), top(cx, h.id, 30));
        cx.advance_clock(Duration::from_millis(400));
        cx.draw(h.id);
        assert_ne!(top(cx, h.id, 30), row, "the glide scrolls");
        // 400 ms of 2 s to 100 px: 20 px on
        let moved = x(cx) - start;
        assert!((15.0..=25.0).contains(&moved), "the animation moved {moved} px during the glide");
    });
}
