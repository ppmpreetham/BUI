//! How long selecting and deselecting take on a big page: `cargo run --release --example bench_select [paragraphs]`.
use std::time::Instant;

use blenderui::prelude::*;

struct Page(usize);

impl Render for Page {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        let mut b = body().m_0().style("font-size: 14px");
        for i in 0..self.0 {
            let text = (0..30).map(|w| format!("word{}", (i * 31 + w) % 977)).collect::<Vec<_>>().join(" ");
            b = b.child(p().style("margin: 0 0 8px 0; width: 600px").child(text));
        }
        b
    }
}

fn main() {
    let n: usize = std::env::args().nth(1).and_then(|s| s.parse().ok()).unwrap_or(400);
    Application::headless().run(move |cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(800.0), px(600.0)) })),
            ..Default::default()
        };
        let h = cx.open_window(opts, |_, cx| cx.new(|_| Page(n))).unwrap();
        cx.draw(h.id);
        let ms = |t: Instant| t.elapsed().as_secs_f64() * 1000.0;
        for round in 0..3 {
            let t = Instant::now();
            cx.simulate_drag(h.id, (10.0, 10.0), (500.0, 400.0));
            let drag = ms(t);
            let t = Instant::now();
            cx.draw(h.id);
            let draw1 = ms(t);
            let t = Instant::now();
            cx.simulate_click(h.id, 700.0, 300.0); // empty space right of the paragraphs
            let click = ms(t);
            let t = Instant::now();
            cx.draw(h.id);
            let draw2 = ms(t);
            let t = Instant::now();
            h.update(cx, |_, w, _| w.select_all()).unwrap();
            cx.draw(h.id);
            let all = ms(t);
            let t = Instant::now();
            cx.simulate_key_with(h.id, "", "Escape", false, false);
            let esc = ms(t);
            let t = Instant::now();
            cx.draw(h.id);
            let draw3 = ms(t);
            println!("round {round} ({n} paragraphs): drag {drag:.2} ms, draw {draw1:.2}, click-away {click:.2} (+draw {draw2:.2}), select-all+draw {all:.2}, escape {esc:.2} (+draw {draw3:.2})");
        }
    });
}
