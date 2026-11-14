//! Selecting text, like a browser: press and drag over the paragraphs, double-click a word,
//! triple-click a paragraph, `Ctrl+A`, `Ctrl+C`, `Escape`. `user-select` takes text out
//! (`select-none`) or takes it whole (`select-all`). The text field keeps its own selection.
//! `cargo run --release --example selectable` (`BUI_GPU=0` for the software raster).

use blenderui::prelude::*;

struct Selectable {
    heard: String,
}

impl Render for Selectable {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        rsx! {
            <body m-0 p-6 min-h-screen flex flex-col gap-3 bg-zinc-900 text-zinc-100 text-base>
                <h1 text-2xl font-bold>"Selectable text"</h1>
                <p>
                    "Press and drag across this paragraph and the next. A drag selects text, "
                    "a double-click a word, a triple-click the whole paragraph. "
                    <b>"Bold"</b> " and " <i>"italic"</i> " pieces of one paragraph are one selection."
                </p>
                <p>"Ctrl+C copies the selection, Escape or a click elsewhere clears it. h\u{e9}llo w\u{f6}rld \u{f1}and\u{fa}."</p>
                <p select-none text-zinc-500>"This paragraph is select-none: a drag passes through it."</p>
                <p>"An id: " <code select-all px-1 rounded bg-zinc-700>"7f3a-91c2-b40e"</code> " (select-all: one click takes it whole)."</p>
                <input type="text" value="A text field keeps its own selection" class="w-80" />
                <div id="heard" text-sm text-zinc-400>{self.heard.clone()}</div>
            </body>
        }
    }
}

fn main() {
    Application::new().run(|cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds {
                origin: point(px(0.0), px(0.0)),
                size: size(px(520.0), px(400.0)),
            })),
            ..Default::default()
        };
        let win = cx.open_window(opts, |_, cx| cx.new(|_| Selectable { heard: String::new() })).unwrap();
        win.update(cx, |_, w, cx| {
            let me = cx.weak_entity();
            w.on_selection_change(move |text, _, app| {
                let line = text.map_or_else(String::new, |t| format!("selected {} characters", t.chars().count()));
                me.update(app, |this, cx| {
                    this.heard = line;
                    cx.notify();
                });
            });
        });
    });
}
