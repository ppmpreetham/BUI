//! Frame-cost benchmark for the usual re-render shapes, off-screen: a big
//! static page after a tiny state change, a hovered 1500-row list, a text
//! page whose width changes, an animated page (CSS keyframes + Motion) and a
//! chat. Prints ms per frame (render, layout, paint and the software raster
//! of 1000x800 px) and heap allocations per frame.
//!
//! ```text
//! cargo run --release --example bench_frames -j 1 [-- static|list|text|anim|chat] [iterations]
//! BUI_PROFILE=1 cargo run --release --example bench_frames -j 1 -- static   # per-phase times
//! BUI_ALLOC_TRACE=1 cargo run --release --example bench_frames -j 1 -- static  # where one frame allocates
//! ```
//! Run one scenario per process: closed off-screen windows keep rendering inside the same process, which
//! slows the scenarios after them. Allocation sites need symbols (`CARGO_PROFILE_RELEASE_DEBUG=1`).

use std::alloc::{GlobalAlloc, Layout, System};
use std::cell::Cell;
use std::time::{Duration, Instant};

use blenderui::prelude::*;
use blenderui::vlist::{chat_list, synth, theme_palette, ChatList, ChatStyle, ListState, Options, VecSource};

struct Counting;
thread_local! {
    static COUNT: Cell<usize> = const { Cell::new(0) };
    static TRACE: Cell<bool> = const { Cell::new(false) };
    static SITES: std::cell::RefCell<Vec<std::backtrace::Backtrace>> = const { std::cell::RefCell::new(Vec::new()) };
}
// SAFETY: forwards to the system allocator; the counter is a const thread-local.
unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        COUNT.set(COUNT.get() + 1);
        if TRACE.get() {
            // allocation-site capture (BUI_ALLOC_TRACE=1): the capture itself allocates, so it is switched off meanwhile
            TRACE.set(false);
            let bt = std::backtrace::Backtrace::force_capture();
            SITES.with(|s| s.borrow_mut().push(bt));
            TRACE.set(true);
        }
        unsafe { System.alloc(l) }
    }
    unsafe fn dealloc(&self, p: *mut u8, l: Layout) {
        unsafe { System.dealloc(p, l) }
    }
}
#[global_allocator]
static A: Counting = Counting;

const LOREM: &str = "The quick brown fox jumps over the lazy dog while a quiet river keeps running past the old stone bridge, \
    and every paragraph repeats a little longer than anyone would need so that lines wrap in the narrow column.";

/// A big page: `n` paragraphs, a counter in the header.
struct Static {
    n: u32,
    paras: usize,
}

impl Render for Static {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        let mut col = div().flex().flex_col().gap(px(4.0)).p(px(8.0));
        for i in 0..self.paras {
            col = col.child(
                div().flex().flex_col().child(p().child(LOREM)).child(
                    div().flex().gap(px(6.0)).child(span().child(format!("item {i}"))).child(b().child("bold")).child(span().child("tail")),
                ),
            );
        }
        body().m_0().child(div().child(format!("count {}", self.n))).child(col)
    }
}

/// 1500 rows, each with a hover style (or none: `hover` false).
struct List {
    hover: bool,
}

impl Render for List {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        let mut col = div().flex().flex_col().w_full();
        for i in 0..1500 {
            let row = div().id(format!("r{i}")).flex().items_center().gap(px(10.0)).h(px(24.0)).px(px(8.0));
            let row = if self.hover { row.hover(|s| s.bg(rgb(0x3a3a3a))) } else { row };
            col = col.child(
                row
                    .child(span().child(format!("row {i}")))
                    .child(span().child("value"))
                    .child(span().child("status")),
            );
        }
        body().m_0().child(div().h(px(800.0)).overflow_y_scroll().child(col))
    }
}

/// Text that re-wraps when the column width changes.
struct Text {
    w: f32,
}

impl Render for Text {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        let mut col = div().w(px(self.w)).flex().flex_col().gap(px(4.0));
        for i in 0..300 {
            col = col.child(p().child(format!("{i}: {LOREM}")));
        }
        body().m_0().child(col)
    }
}

/// 200 CSS-keyframe boxes and 50 Motion boxes, always moving.
struct Anim;

impl Render for Anim {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        let css = rsx! {
            <style>"@keyframes pulse { 0% { opacity: 1; width: 20px } 50% { opacity: 0.3; width: 40px } 100% { opacity: 1; width: 20px } }
                .k { height: 12px; margin: 1px; background: #4772b3; animation: pulse 1s linear infinite }"</style>
        };
        let k = (0..200).map(|i| rsx! { <div class="k" id={format!("k{i}")} /> });
        let m = (0..50).map(|i| rsx! {
            <motion.div id={format!("m{i}")} style="height: 12px; width: 30px; background: #47b372"
                animate={{ x: 100, backgroundColor: "#7247b3", transition: { duration: 1, repeat: -1, repeatType: "reverse", ease: "linear" } }} />
        });
        body().m_0().child(css).children(k).children(m)
    }
}

struct Chat {
    list: Entity<ListState<VecSource, ChatStyle>>,
}

impl Render for Chat {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        body().m_0().size_full().child(div().h(px(800.0)).child(chat_list(&self.list)))
    }
}

fn open<V: Render>(cx: &mut App, v: impl FnOnce(&mut App) -> V) -> WindowHandle<V> {
    let opts = WindowOptions {
        window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(1000.0), px(800.0)) })),
        ..Default::default()
    };
    let v = v(cx);
    cx.open_window(opts, |_, cx| cx.new(|_| v)).unwrap()
}

/// Run `frame(i)` `iters` times (after a warm-up) and print the median ms and allocations per frame.
fn measure(name: &str, iters: usize, mut frame: impl FnMut(usize)) {
    for i in 0..iters.min(30) {
        frame(i);
    }
    let mut times = Vec::with_capacity(iters);
    let mut allocs = Vec::with_capacity(iters);
    for i in 0..iters {
        let a0 = COUNT.get();
        let t = Instant::now();
        frame(i);
        times.push(t.elapsed());
        allocs.push(COUNT.get() - a0);
    }
    if std::env::var_os("BUI_ALLOC_TRACE").is_some() {
        TRACE.set(true);
        frame(0);
        TRACE.set(false);
        let sites = SITES.take();
        let mut by: std::collections::HashMap<String, usize> = std::collections::HashMap::new();
        for bt in &sites {
            // the frames between the allocator and our crate are noise: keep the first 7 named frames after it
            let text = bt.to_string();
            let mut key = String::new();
            let mut n = 0;
            for line in text.lines().filter(|l| !l.trim_start().starts_with("at ")) {
                let name = line.split_once(": ").map_or(line, |x| x.1);
                if name.contains("backtrace") || name.contains("alloc::") || name.contains("Counting") || name.contains("__rust") || name.contains("RawVec") || name.contains("raw_vec")
                    || name.contains("core::ops::function") || name.contains("alloc_zeroed") { continue; }
                key.push_str(name);
                key.push_str(" <- ");
                n += 1;
                if n == 9 { break; }
            }
            *by.entry(key).or_default() += 1;
        }
        let mut v: Vec<_> = by.into_iter().collect();
        v.sort_by_key(|x| std::cmp::Reverse(x.1));
        println!("{} allocations traced", sites.len());
        for (k, n) in v.iter().take(40) {
            println!("{n:7}  {k}");
        }
    }
    times.sort();
    allocs.sort_unstable();
    let ms = |d: Duration| d.as_secs_f64() * 1000.0;
    println!(
        "{name:<10} median {:7.3} ms  p10 {:7.3}  p90 {:7.3}  allocs/frame {}",
        ms(times[iters / 2]), ms(times[iters / 10]), ms(times[iters * 9 / 10]), allocs[iters / 2]
    );
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let which = args.first().map_or("all", String::as_str).to_string();
    let iters: usize = args.get(1).and_then(|s| s.parse().ok()).unwrap_or(200);
    let want = |n: &str| which == "all" || which == n;
    Application::headless().run(move |cx| {
        if want("static") {
            let win = open(cx, |_| Static { n: 0, paras: 400 });
            cx.draw(win.id);
            measure("static", iters, |_| {
                win.update(cx, |v, _, cx| {
                    v.n += 1;
                    cx.notify();
                });
                cx.draw(win.id);
            });
            cx.close_window(win.id);
        }
        if want("list") {
            let win = open(cx, |_| List { hover: true });
            cx.draw(win.id);
            measure("list-hover", iters, |i| {
                cx.simulate_mouse_move(win.id, 40.0 + (i % 7) as f32, 12.0 + 24.0 * (i % 30) as f32);
                cx.draw(win.id);
            });
            measure("list-move", iters, |i| {
                // moves inside one row: nothing changes
                cx.simulate_mouse_move(win.id, 40.0 + (i % 100) as f32, 100.0);
                cx.draw(win.id);
            });
            measure("list-scroll", iters, |i| {
                cx.simulate_scroll(win.id, 100.0, 100.0, if i % 2 == 0 { 3 } else { -3 });
                cx.draw(win.id);
            });
            cx.close_window(win.id);
        }
        if want("plain") {
            let win = open(cx, |_| List { hover: false });
            cx.draw(win.id);
            measure("plain-scroll", iters, |i| {
                cx.simulate_scroll(win.id, 100.0, 100.0, if i % 2 == 0 { 3 } else { -3 });
                cx.draw(win.id);
            });
            cx.close_window(win.id);
        }
        if want("text") {
            let win = open(cx, |_| Text { w: 600.0 });
            cx.draw(win.id);
            measure("text-resize", iters, |i| {
                win.update(cx, |v, _, cx| {
                    v.w = 400.0 + i as f32 * 0.9; // a new width each frame: a drag-resize never revisits one
                    cx.notify();
                });
                cx.draw(win.id);
            });
            cx.close_window(win.id);
        }
        if want("anim") {
            blenderui::motion::lag_smoothing(0.0, 0.0);
            let win = open(cx, |_| Anim);
            cx.draw(win.id);
            measure("anim", iters, |_| {
                cx.advance_clock(Duration::from_millis(16));
                cx.draw(win.id);
            });
            cx.close_window(win.id);
        }
        if want("chat") {
            let win = open(cx, |cx| {
                let opts = Options { start_at_end: true, ..Options::default() };
                let list = cx.new(|cx| ListState::new(ChatList::from_messages(synth::messages(0, 20_000), theme_palette(), 1000.0, 800.0, opts), cx));
                Chat { list }
            });
            cx.draw(win.id);
            measure("chat-scroll", iters, |i| {
                cx.simulate_scroll(win.id, 500.0, 400.0, if i % 4 < 2 { 2 } else { -2 });
                cx.draw(win.id);
            });
            measure("chat-hover", iters, |i| {
                cx.simulate_mouse_move(win.id, 400.0 + (i % 50) as f32, 100.0 + (i % 20) as f32 * 20.0);
                cx.draw(win.id);
            });
            cx.close_window(win.id);
        }
    });
}
