// SPDX-License-Identifier: GPL-2.0-or-later
//! A host with no event loop of its own (a web page): `Application::start` keeps the application alive,
//! the host asks for frames, resizes (also to another pixel ratio) and frees it.

use blenderui::prelude::*;

struct Page;

impl Render for Page {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        body().m_0().child(p().child("hello"))
    }
}

#[test]
fn frames_resize_scale_and_shutdown() {
    let mut app = Application::headless().start(|cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds {
                origin: point(px(0.0), px(0.0)),
                size: size(px(120.0), px(80.0)),
            })),
            ..Default::default()
        };
        cx.open_window(opts, |_, cx| cx.new(|_| Page)).unwrap();
    });
    let id = app.window_ids()[0];

    let (w, h, _) = app.frame_if_needed(id).expect("a first frame");
    assert_eq!((w, h), (120, 80));
    assert!(app.frame_if_needed(id).is_none(), "nothing changed: no frame");

    // a new size, and a new pixel ratio at the same logical size: device pixels follow the ratio
    app.resize_window(id, 100, 60, 1.0);
    assert_eq!(app.frame_if_needed(id).map(|f| (f.0, f.1)), Some((100, 60)));
    app.resize_window(id, 200, 120, 2.0);
    assert_eq!(app.frame_if_needed(id).map(|f| (f.0, f.1)), Some((200, 120)));
    let scale = app.window(id).map(Window::scale_factor);
    assert_eq!(scale, Some(2.0), "the window's scale followed");
    // page zoom out: a ratio below 1 is kept (a 120 x 80 CSS canvas at 0.75 is 90 x 60 device pixels)
    app.resize_window(id, 90, 60, 0.75);
    assert_eq!(app.frame_if_needed(id).map(|f| (f.0, f.1)), Some((90, 60)));
    assert_eq!(app.window(id).map(Window::scale_factor), Some(0.75));
    app.resize_window(id, 200, 120, 2.0);
    assert_eq!(app.frame_if_needed(id).map(|f| (f.0, f.1)), Some((200, 120)));
    // the same call again changes nothing
    app.resize_window(id, 200, 120, 2.0);
    assert!(app.frame_if_needed(id).is_none());

    // freeing is safe twice (and again on drop)
    app.shutdown();
    app.shutdown();
    assert!(app.window_ids().is_empty());
    assert!(app.frame_if_needed(id).is_none());
}
