//! A title bar the app draws: `cargo run --example titlebar`.
//!
//! `WindowDecorations::Client` takes the system's title bar away; `window_control_area` marks
//! what the app draws instead. On Windows the resize edges, Aero Snap, the shadow and the snap
//! layouts over the maximize button stay the system's.

use blenderui::prelude::*;

struct App_ {
    clicks: u32,
}

impl Render for App_ {
    fn render(&mut self, _: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        body()
            .child(style().child("
                html, body { margin: 0; height: 100%; background: #303030; color: #e6e6e6 }
                body { display: flex; flex-direction: column }
                #bar { display: flex; align-items: center; height: 32px; background: #1d1d1d;
                       font-size: 12px; user-select: none }
                #file { border-radius: 4px;
                        margin-left: 6px; padding: 3px 10px }
                #file:hover { background: #3d3d3d }
                #title { flex: 1; text-align: center; color: #9a9a9a }
                .control { width: 46px; height: 100%; display: flex; align-items: center;
                           justify-content: center }
                .control:hover { background: #3d3d3d }
                #close:hover { background: #c42b1c; color: white }
                .min-icon { width: 10px; height: 1px; background: currentColor }
                .max-icon { width: 9px; height: 9px; border: 1px solid currentColor }
                #page { flex: 1; padding: 24px }"))
            .child(div().id("bar").window_control_area(WindowControlArea::Drag)
                .child(div().id("file").child("File").on_click(cx.listener(|this, _, _, cx| {
                    this.clicks += 1;
                    cx.notify();
                })))
                .child(div().id("title").child("Custom title bar"))
                .child(div().class("control").window_control_area(WindowControlArea::Min)
                    .child(div().class("min-icon")))
                .child(div().class("control").window_control_area(WindowControlArea::Max)
                    .child(div().class("max-icon")))
                .child(div().id("close").class("control").window_control_area(WindowControlArea::Close)
                    .child("✕")))
            .child(div().id("page").child(p().child(format!(
                "Drag the bar to move the window, double-click it to maximize. Clicks on \"File\": {}.",
                self.clicks))))
    }
}

fn main() {
    Application::new().run(|cx: &mut App| {
        let opts = WindowOptions { window_decorations: WindowDecorations::Client, ..Default::default() };
        cx.open_window(opts, |_, cx| cx.new(|_| App_ { clicks: 0 })).unwrap();
    });
}
