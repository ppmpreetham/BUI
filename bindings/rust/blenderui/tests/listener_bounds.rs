//! `Window::bounds_of` inside a listener: the window lends its frame to the
//! listeners while they run, and the event's path (the target and its
//! ancestors) still answers, so a slider can turn a click into a value.

use blenderui::prelude::*;

/// A 200 px track; a click on it writes where it landed, as a fraction of
/// the track, into the signal.
struct Track(Signal<Option<f32>>);

impl Render for Track {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        let seen = self.0;
        div().p(px(20.0)).child(
            div()
                .id("track")
                .w(px(200.0))
                .h(px(20.0))
                .child(div().w(px(50.0)).h(px(20.0)))
                .on_click(move |e, window, _| {
                    let fraction = window.bounds_of("track").map(|b| (e.up.position.x.0 - b.origin.x) / b.size.width);
                    seen.set(fraction);
                }),
        )
    }
}

#[test]
fn bounds_of_answers_inside_a_listener_for_the_event_path() {
    Application::headless().run(|cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(260.0), px(60.0)) })),
            ..Default::default()
        };
        let seen = Signal::new(None);
        let win = cx.open_window(opts, |_, cx| cx.new(|_| Track(seen))).unwrap();
        cx.draw(win.id);
        let b = cx.element_bounds(win.id, "track").unwrap();
        // on the inner child first: the event bubbles up to "track", which is on its path
        cx.simulate_click(win.id, b.origin.x + b.size.width * 0.1, b.origin.y + 10.0);
        assert!(seen.get().is_some_and(|f| (f - 0.1).abs() < 0.02), "a click on the child: {:?}", seen.get());
        cx.simulate_click(win.id, b.origin.x + b.size.width * 0.75, b.origin.y + 10.0);
        assert!(seen.get().is_some_and(|f| (f - 0.75).abs() < 0.02), "a click at 3/4: {:?}", seen.get());
    });
}
