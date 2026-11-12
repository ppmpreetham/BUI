//! Events at scale, headless: what a pointer move, a press and a release cost with thousands of
//! elements that listen, and with none.
//!
//! ```text
//! cargo run --release --example events_bench
//! ```
//!
//! Every number is microseconds per event, the median of several rounds. Works on any commit that has
//! `on_click`, `on_hover`, `on_mouse_move` and `simulate_mouse_move`, so the same file measures a change.

use std::sync::atomic::{AtomicU64, Ordering::Relaxed};
use std::time::Instant;

use blenderui::prelude::*;

static HEARD: AtomicU64 = AtomicU64::new(0);

/// `cells` boxes in a grid, each with `listen` listeners (0: none), `depth` boxes deep.
struct Grid {
    cells: usize,
    listen: bool,
    depth: usize,
}

impl Render for Grid {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        let cols = 70;
        let mut root = div().id("root").size_full().flex().flex_wrap();
        for i in 0..self.cells {
            let mut cell = div().w(px(10.0)).h(px(10.0));
            if self.listen {
                cell = cell
                    .on_click(|_, _, _| { HEARD.fetch_add(1, Relaxed); })
                    .on_mouse_move(|_, _, _| { HEARD.fetch_add(1, Relaxed); })
                    .on_hover(|_, _, _| { HEARD.fetch_add(1, Relaxed); });
            }
            // a few levels around it, as real views nest
            let mut el = cell;
            for _ in 0..self.depth {
                el = div().child(el);
            }
            root = root.child(el);
            let _ = (i, cols);
        }
        root
    }
}

fn median(mut v: Vec<f64>) -> f64 {
    v.sort_by(f64::total_cmp);
    v[v.len() / 2]
}

fn run(label: &str, cells: usize, listen: bool, depth: usize) {
    Application::headless().run(move |cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds {
                origin: point(px(0.0), px(0.0)),
                size: size(px(700.0), px(600.0)),
            })),
            ..Default::default()
        };
        let w = cx.open_window(opts, move |_, cx| cx.new(|_| Grid { cells, listen, depth })).unwrap().id;
        cx.draw(w);
        cx.draw(w);
        let (mut mv, mut click) = (Vec::new(), Vec::new());
        for round in 0..5 {
            // pointer moves across the grid: every one enters another box
            let t = Instant::now();
            let n = 400;
            for k in 0..n {
                let x = 5.0 + ((k * 13 + round) % 690) as f32;
                let y = 5.0 + ((k * 7) % 590) as f32;
                cx.simulate_mouse_move(w, x, y);
            }
            mv.push(t.elapsed().as_secs_f64() * 1e6 / n as f64);
            let t = Instant::now();
            let n = 100;
            for k in 0..n {
                cx.simulate_click(w, 5.0 + (k % 690) as f32, 5.0 + (k % 590) as f32);
            }
            click.push(t.elapsed().as_secs_f64() * 1e6 / n as f64);
        }
        println!("{label:<34} move {:>8.1} us   click {:>8.1} us", median(mv), median(click));
    });
}

fn main() {
    run("   10 boxes, none listen", 10, false, 1);
    run("  500 boxes, all listen, depth 1", 500, true, 1);
    run(" 3000 boxes, none listen, depth 1", 3000, false, 1);
    run(" 3000 boxes, all listen, depth 1", 3000, true, 1);
    run(" 3000 boxes, all listen, depth 6", 3000, true, 6);
    println!("(heard {})", HEARD.load(Relaxed));
}
