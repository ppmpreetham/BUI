//! `app()`, the root of an app, and controls inside a flex column of a definite height: the
//! window is covered by the theme, a taller page scrolls, and a button or input keeps its height.

use blenderui::prelude::*;

fn open<V: Render>(cx: &mut App, w: f32, h: f32, v: V) -> WindowHandle<V> {
    let opts = WindowOptions {
        window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(w), px(h)) })),
        ..Default::default()
    };
    cx.open_window(opts, |_, cx| cx.new(|_| v)).unwrap()
}

fn rows() -> Div {
    let mut rows = div().flex().flex_col();
    for i in 0..40 {
        rows = rows.child(div().h(px(20.0)).child(format!("Row {i}")));
    }
    rows
}

struct Fixed;

impl Render for Fixed {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        // a column 120 px tall holding more than 120 px of content
        div().styled().flex().flex_col().h(px(120.0)).child(button().id("b").child("Go")).child(input().id("i")).child(rows())
    }
}

#[test]
fn controls_keep_their_height_in_a_short_flex_column() {
    Application::headless().run(|cx| {
        let win = open(cx, 300.0, 200.0, Fixed);
        cx.draw(win.id);
        for id in ["b", "i"] {
            let b = cx.element_bounds(win.id, id).expect(id);
            assert!(b.size.height > 12.0, "{id} was squashed to {}", b.size.height);
        }
    });
}

struct Marked;

impl Render for Marked {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        app().child(div().id("top").h(px(20.0)).child("top")).child(rows())
    }
}

#[test]
fn a_touch_drag_scrolls_exactly_as_far_as_the_finger_went() {
    Application::headless().run(|cx| {
        let win = open(cx, 300.0, 200.0, Marked);
        cx.draw(win.id);
        cx.draw(win.id);
        let at = cx.element_bounds(win.id, "top").unwrap().origin.y;
        // the finger drags up by 7 px, then by 3.5: the content follows each, not a wheel notch
        cx.simulate_scroll_px(win.id, 150.0, 100.0, -7.0);
        cx.draw(win.id);
        let moved = cx.element_bounds(win.id, "top").unwrap().origin.y;
        assert!((at - moved - 7.0).abs() < 0.01, "moved {} not 7", at - moved);
        cx.simulate_scroll_px(win.id, 150.0, 100.0, -3.5);
        cx.draw(win.id);
        let then = cx.element_bounds(win.id, "top").unwrap().origin.y;
        assert!((moved - then - 3.5).abs() < 0.01, "moved {} not 3.5", moved - then);
    });
}

struct Root;

impl Render for Root {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        app().child(h1().child("Title")).child(rows())
    }
}

#[test]
fn app_covers_the_window_and_scrolls() {
    Application::headless().run(|cx| {
        let win = open(cx, 300.0, 200.0, Root);
        cx.draw(win.id); // the first frame learns the size
        let (w, _, a) = cx.draw(win.id);
        // the theme's background reaches the corner: no white margin around the app
        let corner = a[w + 1] & 0x00ff_ffff;
        assert_eq!(corner, 0x30_3030, "{corner:06x}");
        cx.simulate_scroll(win.id, 150.0, 100.0, -3); // the wheel's three lines down
        cx.draw(win.id);
        let (_, _, b) = cx.draw(win.id);
        assert!(a != b, "the page did not scroll");
    });
}
