//! New text in a box that keeps its layout: a single word of the same width does not lay out
//! again, and every change must give the frame a fresh window gives (incremental equals full).

use blenderui::prelude::*;

fn open<V: Render>(cx: &mut App, w: f32, h: f32, v: V) -> WindowHandle<V> {
    let opts = WindowOptions {
        window_bounds: Some(WindowBounds::Windowed(Bounds {
            origin: point(px(0.0), px(0.0)),
            size: size(px(w), px(h)),
        })),
        ..Default::default()
    };
    cx.open_window(opts, |_, cx| cx.new(|_| v)).unwrap()
}

struct Label {
    text: &'static str,
}

impl Render for Label {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        body().m_0().child(
            div()
                .id("row")
                .flex()
                .child(div().id("t").child(self.text))
                .child(div().id("after").w(px(10.0)).h(px(10.0)).bg(rgb(0xff0000))),
        )
    }
}

#[test]
fn text_change_matches_a_fresh_frame() {
    // same width (a digit for a digit), wider, narrower, a word to two words and back
    let steps = ["100", "101", "1000000", "7", "two words", "solo"];
    Application::headless().run(|cx| {
        let h = open(cx, 300.0, 60.0, Label { text: "000" });
        let win = h.id;
        cx.draw(win);
        for text in steps {
            h.update(cx, |v, _, cx| {
                v.text = text;
                cx.notify();
            });
            let incremental = cx.draw(win);
            let after = cx.element_bounds(win, "after").unwrap();
            let fresh = open(cx, 300.0, 60.0, Label { text });
            let full = cx.draw(fresh.id);
            assert_eq!(after, cx.element_bounds(fresh.id, "after").unwrap(), "layout after {text:?}");
            assert!(incremental == full, "pixels after {text:?}");
        }
    });
}
