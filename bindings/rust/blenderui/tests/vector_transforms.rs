//! Transforms that only move and scale are drawn as vectors (no pixel
//! layer): scaled text is text at the scaled size; a layer reaches as far as
//! what is scaled inside it; and a layer that only moves is drawn from the
//! cache, not rasterized and filtered again.

use std::time::Duration;

use blenderui::effects::layers_drawn;
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

struct Page {
    css: &'static str,
    body: fn() -> Div,
}

impl Render for Page {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        (self.body)().child(style().child(self.css))
    }
}

/// The columns (from, to) of the lit pixels in rows `y0..y1`.
fn ink(img: &(usize, usize, Vec<u32>), y0: usize, y1: usize) -> (usize, usize) {
    let (w, _, px) = img;
    (y0..y1)
        .flat_map(|y| (0..*w).filter(move |&x| px[y * w + x] & 0xff > 0x80))
        .fold((usize::MAX, 0), |(a, b), x| (a.min(x), b.max(x)))
}

#[test]
fn scaled_text_is_text_at_the_scaled_size() {
    Application::headless().run(|cx| {
        let win = open(cx, 400.0, 200.0, Page {
            css: "html, body { margin: 0; background: #000; color: #fff } .t { position: absolute; left: 10px }
                  #s { top: 10px; font-size: 20px; scale: 2; transform-origin: left top }
                  #b { top: 110px; font-size: 40px }",
            body: || body().child(div().id("s").class("t").child("MMMM")).child(div().id("b").class("t").child("MMMM")),
        });
        let img = cx.draw(win.id);
        let (scaled, big) = (ink(&img, 10, 100), ink(&img, 110, 200));
        assert!(scaled.1 > scaled.0 && big.1 > big.0, "both drawn: {scaled:?} {big:?}");
        let (ws, wb) = (scaled.1 - scaled.0, big.1 - big.0);
        assert!(ws.abs_diff(wb) <= 4, "scale(2) of 20px text is as wide as 40px text: {ws} vs {wb}");
    });
}

#[test]
fn a_layer_reaches_as_far_as_what_is_scaled_in_it() {
    Application::headless().run(|cx| {
        let win = open(cx, 200.0, 200.0, Page {
            css: "html, body { margin: 0; background: #000 }
                  #outer { position: absolute; left: 20px; top: 20px; width: 40px; height: 40px; filter: brightness(1) }
                  #inner { width: 40px; height: 40px; background: #0f0; scale: 2; transform-origin: left top }",
            body: || body().child(div().id("outer").child(div().id("inner"))),
        });
        let (w, _, px) = cx.draw(win.id);
        let at = |x: usize, y: usize| px[y * w + x] & 0xffffff;
        // scaled from its top left: 20..100 on both axes, past its parent's box
        assert_eq!(at(95, 95), 0x00ff00, "the scaled box is not cut at its layer's box");
        assert_eq!(at(105, 105), 0x000000, "and ends where it should");
    });
}

#[test]
fn a_non_uniform_scale_stretches_text() {
    Application::headless().run(|cx| {
        let win = open(cx, 400.0, 200.0, Page {
            css: "html, body { margin: 0; background: #000; color: #fff } .t { position: absolute; left: 10px; font-size: 20px }
                  #s { top: 10px; scale: 2 1; transform-origin: left top }
                  #n { top: 110px }",
            body: || body().child(div().id("s").class("t").child("MMMM")).child(div().id("n").class("t").child("MMMM")),
        });
        let img = cx.draw(win.id);
        let (wide, plain) = (ink(&img, 10, 60), ink(&img, 110, 160));
        assert!(wide.1 > wide.0 && plain.1 > plain.0, "both drawn: {wide:?} {plain:?}");
        let (ww, wp) = (wide.1 - wide.0, plain.1 - plain.0);
        assert!(ww.abs_diff(2 * wp) <= 6, "scale(2, 1) doubles the text's width: {ww} vs {wp}");
    });
}

#[test]
fn a_clip_scales_with_the_box_that_clips() {
    Application::headless().run(|cx| {
        let win = open(cx, 300.0, 100.0, Page {
            css: "html, body { margin: 0; background: #000 }
                  #p { position: absolute; left: 0; top: 0; width: 100px; height: 50px; overflow: hidden; scale: 2; transform-origin: left top }
                  #c { width: 100px; height: 50px; background: #0f0; scale: 2; transform-origin: left top }",
            body: || body().child(div().id("p").child(div().id("c"))),
        });
        let (w, _, px) = cx.draw(win.id);
        let at = |x: usize, y: usize| px[y * w + x] & 0xffffff;
        // the parent's 100 px clip is 200 px through its own scale
        assert_eq!(at(190, 10), 0x00ff00, "the clip is where its box is drawn");
        assert_eq!(at(210, 10), 0x000000, "and cuts there");
    });
}

struct Moving;

impl Render for Moving {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        rsx! {
            <body style="margin: 0; background: #000">
                <motion.div style="position: absolute; left: 20px; top: 20px; width: 100px; height: 300px"
                     animate={{ y: 200, transition: { duration: 1, ease: "linear" } }}>
                    <div style="width: 60px; height: 60px; background: #0f0; filter: blur(6px)" />
                </motion.div>
            </body>
        }
    }
}

/// The first and last rows of column `x` that are green (over half, and not white).
fn green_rows(img: &(usize, usize, Vec<u32>), x: usize) -> Option<(usize, usize)> {
    let (w, h, px) = img;
    let green = |p: u32| (p >> 8) & 0xff > 0x80 && p & 0xff < 0x80;
    let lit: Vec<usize> = (0..*h).filter(|&y| green(px[y * w + x])).collect();
    Some((*lit.first()?, *lit.last()?))
}

#[test]
fn a_blurred_layer_that_only_moves_is_not_blurred_again() {
    Application::headless().run(|cx| {
        let win = open(cx, 300.0, 600.0, Moving);
        let first = green_rows(&cx.draw(win.id), 50).expect("the blurred box is drawn");
        let before = layers_drawn();
        let mut last = cx.draw(win.id);
        for _ in 0..10 {
            cx.advance_clock(Duration::from_millis(16));
            last = cx.draw(win.id);
        }
        let drawn = layers_drawn() - before;
        assert!(drawn <= 1, "the blurred box moved 10 frames and was rasterized {drawn} times");
        // 160 ms of a 1 s glide to 200 px: about 32 px down, the same height
        let moved = green_rows(&last, 50).expect("the blurred box is still drawn");
        let dy = moved.0 as isize - first.0 as isize;
        assert!((24..=40).contains(&dy), "it moved down {dy} px: {first:?} -> {moved:?}");
        assert_eq!(moved.1 - moved.0, first.1 - first.0, "the same box, moved");
    });
}

struct LongList;

impl Render for LongList {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        let rows = (0..300).map(|i| div().class("row").child(format!("row {i}")));
        body().child(style().child("html, body { margin: 0; background: #000; color: #fff }
            .row { height: 40px; filter: blur(2px) }")).children(rows)
    }
}

#[test]
fn blurred_rows_below_the_fold_cost_nothing() {
    Application::headless().run(|cx| {
        let before = layers_drawn();
        let win = open(cx, 400.0, 400.0, LongList);
        let (w, _, px) = cx.draw(win.id);
        // 400 px of 40 px rows: ten rows show, give or take the edges
        let drawn = layers_drawn() - before;
        assert!(drawn <= 12, "300 blurred rows, 10 on screen: {drawn} were blurred");
        // and those ten are drawn: (blurred) text in the first row and the last on screen
        let inked = |y0: usize, y1: usize| (y0..y1).any(|y| (0..w).any(|x| px[y * w + x] & 0xff > 0x20));
        assert!(inked(0, 40) && inked(360, 400), "the rows on screen show their text");
    });
}
