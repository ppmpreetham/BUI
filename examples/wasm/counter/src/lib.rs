//! The README's counter, in a web page: the same code as on a desktop, plus the one line that makes it a
//! page (`blenderui_web::export!`).

use blenderui::prelude::*;

struct Counter {
    count: i32,
}

impl Render for Counter {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        // the page's body, with no margin, as tall as the canvas
        body()
            .styled()
            .m_0()
            .css("height", "100vh")
            .flex()
            .flex_col()
            .gap_2()
            .p_4()
            .child(h1().child(format!("Count: {}", self.count)))
            .child(
                p().child("Any ")
                    .child(b().child("HTML element"))
                    .child(", styled with Tailwind-style methods or CSS."),
            )
            .child(button().child("Add one").on_click(cx.listener(|this, _, _, cx| {
                this.count += 1;
                cx.notify();
            })))
            // a Blender slider and a text field
            .child(input().input_type("range").min(0.0).max(10.0))
            .child(input().placeholder("Type here"))
    }
}

blenderui_web::export!(|cx, options| {
    cx.open_window(options, |_, cx| cx.new(|_| Counter { count: 0 })).expect("opening the window");
});
