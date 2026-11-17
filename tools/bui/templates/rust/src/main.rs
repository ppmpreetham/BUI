use blenderui::prelude::*;

struct Counter {
    count: i32,
}

impl Render for Counter {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        div()
            .styled()
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
            // a Blender slider
            .child(input().input_type("range").min(0.0).max(10.0))
    }
}

fn main() {
    Application::new().run(|cx: &mut App| {
        cx.open_window(WindowOptions::default(), |_, cx| cx.new(|_| Counter { count: 0 }))
            .expect("opening the window");
    });
}
