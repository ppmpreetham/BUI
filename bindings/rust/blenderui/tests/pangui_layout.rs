//! PanGui's layout benchmark scenes (pangui.io, "Layout rework and benchmarks")
//! on our layout: pixels, percent, ratio, expand, fit, min / max, wrap, padding,
//! margin and gap, with `calc()`, `min()`, `max()` and `clamp()` as the custom
//! math. Every test checks exact boxes against numbers worked out by hand (or by
//! a small independent formula). `scene_timings` prints the layout time per node
//! for the same scenes: `cargo test --release --test pangui_layout -- --ignored --nocapture`.

use blenderui::prelude::*;
use std::time::Instant;

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

/// `(x, y, w, h)` of the element with `id`.
fn rect(cx: &mut App, win: WindowId, id: &str) -> (f32, f32, f32, f32) {
    let b = cx.element_bounds(win, id).unwrap_or_else(|| panic!("no element {id}"));
    (b.origin.x, b.origin.y, b.size.width, b.size.height)
}

/// A scene: the root `Div` for a tick (the tick changes the root width by one pixel, so a
/// timed frame lays out the whole tree again).
struct Scene {
    build: fn(u32) -> Div,
    tick: u32,
}

impl Render for Scene {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        body().m_0().child((self.build)(self.tick))
    }
}

fn draw(cx: &mut App, build: fn(u32) -> Div) -> WindowId {
    let win = open(cx, 400.0, 300.0, Scene { build, tick: 0 });
    cx.draw(win.id);
    win.id
}

/// A box of `css`, with an id.
fn node(id: impl Into<ElementId>, css: impl Into<String>) -> Div {
    div().id(id).style(css.into())
}

/// A box of `css` without an id.
fn anon(css: impl Into<String>) -> Div {
    div().style(css.into())
}

// -- expand with a max or min constraint ----------------------------------------

/// 1000 pairs in a row: `A` expands, `B` expands up to a maximum. The free space left by `B`
/// goes to `A` (CSS flex: B freezes at its max, A takes the rest).
#[test]
fn expand_with_max_constraint() {
    Application::headless().run(|cx| {
        let w = draw(cx, |_| {
            node("row", "display: flex; width: 3000px").children((0..1000).flat_map(|i| {
                [
                    node(format!("a{i}"), "flex: 1 1 0; height: 10px"),
                    node(format!("b{i}"), "flex: 1 1 0; max-width: 1px; height: 10px"),
                ]
            }))
        });
        assert_eq!(rect(cx, w, "a0"), (0.0, 0.0, 2.0, 10.0));
        assert_eq!(rect(cx, w, "b0"), (2.0, 0.0, 1.0, 10.0));
        assert_eq!(rect(cx, w, "a999"), (2997.0, 0.0, 2.0, 10.0));
        assert_eq!(rect(cx, w, "b999"), (2999.0, 0.0, 1.0, 10.0));
    });
}

/// The same with a minimum: `B` would get 3 px but needs 4, so `A` gets what is left.
#[test]
fn expand_with_min_constraint() {
    Application::headless().run(|cx| {
        let w = draw(cx, |_| {
            node("row", "display: flex; width: 6000px").children((0..1000).flat_map(|i| {
                [
                    node(format!("a{i}"), "flex: 1 1 0; height: 10px"),
                    node(format!("b{i}"), "flex: 1 1 0; min-width: 4px; height: 10px"),
                ]
            }))
        });
        assert_eq!(rect(cx, w, "a0").2, 2.0);
        assert_eq!(rect(cx, w, "b0").2, 4.0);
        assert_eq!(rect(cx, w, "b999"), (5996.0, 0.0, 4.0, 10.0));
    });
}

/// A fixed size with a `min` / `max` / `clamp` bound is the same as an expand with constraints:
/// `clamp(40px, 25%, 100px)` follows the container between its two bounds.
#[test]
fn clamp_is_a_min_max_constraint() {
    Application::headless().run(|cx| {
        let w = draw(cx, |_| {
            anon("width: 400px")
                .child(node("big", "width: clamp(40px, 25%, 100px); height: 4px"))
                .child(anon("width: 200px").child(node("mid", "width: clamp(40px, 25%, 100px); height: 4px")))
                .child(anon("width: 100px").child(node("small", "width: clamp(40px, 25%, 100px); height: 4px")))
                .child(anon("width: 200px").child(node("mx", "width: max(10px, calc(50% - 30px)); height: 4px")))
                .child(anon("width: 200px").child(node("mn", "width: min(50%, 80px); height: 4px")))
        });
        assert_eq!(rect(cx, w, "big").2, 100.0);
        assert_eq!(rect(cx, w, "mid").2, 50.0);
        assert_eq!(rect(cx, w, "small").2, 40.0);
        assert_eq!(rect(cx, w, "mx").2, 70.0);
        assert_eq!(rect(cx, w, "mn").2, 80.0);
    });
}

// -- fit ---------------------------------------------------------------------------

/// `depth` levels of containers that fit their content (rows and columns alternate, 1 px of
/// padding, 2 px of gap, two children per level, 3 leaves of 10x10 at the bottom), and the
/// size of such a tree by a formula of its own.
fn fit_tree(depth: u32, id: &str) -> Div {
    let column = depth % 2 == 1;
    let dir = if column { "column" } else { "row" };
    let kids: Vec<Div> = if depth == 0 {
        (0..3).map(|i| node(format!("{id}.{i}"), "width: 10px; height: 10px")).collect()
    } else {
        (0..2).map(|i| fit_tree(depth - 1, &format!("{id}.{i}"))).collect()
    };
    node(id.to_string(), format!("display: flex; flex-direction: {dir}; padding: 1px; gap: 2px; width: fit-content"))
        .children(kids)
}

/// `(width, height)` of `fit_tree(depth)`: the kids along the main axis add up with the gaps,
/// across it the largest counts, and 2 px of padding go around.
fn fit_size(depth: u32) -> (f32, f32) {
    let (kid, count) = if depth == 0 { ((10.0, 10.0), 3.0) } else { (fit_size(depth - 1), 2.0) };
    let along = |size: f32| size * count + 2.0 * (count - 1.0);
    if depth % 2 == 1 {
        (kid.0 + 2.0, along(kid.1) + 2.0)
    } else {
        (along(kid.0) + 2.0, kid.1 + 2.0)
    }
}

#[test]
fn fit_nesting() {
    Application::headless().run(|cx| {
        let w = draw(cx, |_| fit_tree(6, "t"));
        let (fw, fh) = fit_size(6);
        assert_eq!(rect(cx, w, "t"), (0.0, 0.0, fw, fh));
        let first = fit_size(5);
        assert_eq!(rect(cx, w, "t.0").2, first.0);
        assert_eq!(rect(cx, w, "t.0").3, first.1);
        // the second child follows the first after the padding and the gap (6 is even: a row)
        assert_eq!(rect(cx, w, "t.1").0, 1.0 + first.0 + 2.0);
        assert_eq!(rect(cx, w, "t.0.0.0.0.0.0.2").2, 10.0);
    });
}

// -- weights ------------------------------------------------------------------------

#[test]
fn flex_expand_equal_weights() {
    Application::headless().run(|cx| {
        let w = draw(cx, |_| {
            node("row", "display: flex; width: 15000px")
                .children((0..1500).map(|i| node(format!("c{i}"), "flex: 1 1 0; height: 4px")))
        });
        assert_eq!(rect(cx, w, "c0"), (0.0, 0.0, 10.0, 4.0));
        assert_eq!(rect(cx, w, "c1499"), (14990.0, 0.0, 10.0, 4.0));
    });
}

/// Weights 1, 2 and 3 share 600 px as 100, 200 and 300.
#[test]
fn flex_expand_weights() {
    Application::headless().run(|cx| {
        let w = draw(cx, |_| {
            node("row", "display: flex; width: 600px").children((0..3).map(|i| {
                node(format!("c{i}"), format!("flex: {} 1 0; height: 4px", i + 1))
            }))
        });
        assert_eq!(rect(cx, w, "c0"), (0.0, 0.0, 100.0, 4.0));
        assert_eq!(rect(cx, w, "c1"), (100.0, 0.0, 200.0, 4.0));
        assert_eq!(rect(cx, w, "c2"), (300.0, 0.0, 300.0, 4.0));
    });
}

/// Pixel sizes that also expand, with a minimum: 20 children of `flex: 1 0 10px` share 1000 px
/// as 50 each, and keep their 40 px minimum (and overflow) in 600 px.
#[test]
fn pixels_with_min_expand_constraint() {
    Application::headless().run(|cx| {
        let w = draw(cx, |_| {
            node("wide", "display: flex; width: 1000px")
                .children((0..20).map(|i| node(format!("w{i}"), "flex: 1 0 10px; min-width: 40px; height: 4px")))
        });
        assert_eq!(rect(cx, w, "w19"), (950.0, 0.0, 50.0, 4.0));
        let narrow = draw(cx, |_| {
            node("narrow", "display: flex; width: 600px")
                .children((0..20).map(|i| node(format!("n{i}"), "flex: 1 0 10px; min-width: 40px; height: 4px")))
        });
        assert_eq!(rect(cx, narrow, "n19"), (760.0, 0.0, 40.0, 4.0));
    });
}

// -- nesting, spacing, percent and ratio ---------------------------------------------

/// Columns nested `DEPTH` deep; each holds a 10 px leaf, a 2 px gap and the next column.
fn stack(depth: u32) -> Div {
    let inner = if depth == 1 { None } else { Some(stack(depth - 1)) };
    node(format!("s{depth}"), "display: flex; flex-direction: column; gap: 2px")
        .child(anon("height: 10px"))
        .children(inner)
}

#[test]
fn nested_vertical_stack() {
    Application::headless().run(|cx| {
        let w = draw(cx, |_| stack(50));
        // level d holds d leaves and d - 1 gaps; the next column starts after a leaf and a gap
        assert_eq!(rect(cx, w, "s50").3, 50.0 * 10.0 + 49.0 * 2.0);
        assert_eq!(rect(cx, w, "s49").1, 12.0);
        assert_eq!(rect(cx, w, "s1").1, 49.0 * 12.0);
        assert_eq!(rect(cx, w, "s1").3, 10.0);
    });
}

#[test]
fn padding_and_margin() {
    Application::headless().run(|cx| {
        let w = draw(cx, |_| {
            node("p", "display: flex; flex-direction: column; padding: 5px 7px; width: 100px; height: 60px")
                .child(node("a", "margin: 3px; height: 10px"))
                .child(node("b", "margin: 2px 4px 6px 8px; height: 10px"))
        });
        assert_eq!(rect(cx, w, "a"), (10.0, 8.0, 80.0, 10.0));
        assert_eq!(rect(cx, w, "b"), (15.0, 8.0 + 10.0 + 3.0 + 2.0, 100.0 - 14.0 - 12.0, 10.0));
    });
}

#[test]
fn percentage_and_ratio() {
    Application::headless().run(|cx| {
        let w = draw(cx, |_| {
            node("box", "width: 400px; height: 200px")
                .child(node("half", "width: 50%; aspect-ratio: 2 / 1"))
                .child(node("calc", "width: calc(100% - 20px); aspect-ratio: 4 / 1"))
                .child(node("tall", "height: 25%; aspect-ratio: 1"))
        });
        assert_eq!(rect(cx, w, "half"), (0.0, 0.0, 200.0, 100.0));
        assert_eq!(rect(cx, w, "calc"), (0.0, 100.0, 380.0, 95.0));
        assert_eq!(rect(cx, w, "tall"), (0.0, 195.0, 50.0, 50.0));
    });
}

// -- wrapping ---------------------------------------------------------------------------

/// A wrapping row of alternating fixed (30x20) and cross-axis-expanding (30 wide, height from
/// the line) children, 3 per 100 px line: every expanding child takes its line's height.
#[test]
fn perpendicular_expand_with_wrap() {
    Application::headless().run(|cx| {
        let w = draw(cx, |_| {
            node("wrap", "display: flex; flex-wrap: wrap; width: 100px; align-content: flex-start")
                .children((0..12).map(|i| {
                    if i % 2 == 0 {
                        node(format!("f{i}"), "width: 30px; height: 20px; flex: none")
                    } else {
                        node(format!("e{i}"), "width: 30px; flex: none; align-self: stretch")
                    }
                }))
        });
        // line 0: f0 e1 f2 ; line 1: e3 f4 e5 ; line 2: f6 e7 f8 ; line 3: e9 f10 e11
        assert_eq!(rect(cx, w, "e1"), (30.0, 0.0, 30.0, 20.0));
        assert_eq!(rect(cx, w, "e3"), (0.0, 20.0, 30.0, 20.0));
        assert_eq!(rect(cx, w, "e11"), (60.0, 60.0, 30.0, 20.0));
    });
}

#[test]
fn wide_wrapping() {
    Application::headless().run(|cx| {
        let w = draw(cx, |_| {
            node("wrap", "display: flex; flex-wrap: wrap; width: 100px; align-content: flex-start")
                .children((0..1000).map(|i| node(format!("c{i}"), "width: 10px; height: 10px; flex: none")))
        });
        for i in [0u32, 9, 10, 237, 999] {
            assert_eq!(rect(cx, w, &format!("c{i}")), (10.0 * (i % 10) as f32, 10.0 * (i / 10) as f32, 10.0, 10.0));
        }
        assert_eq!(rect(cx, w, "wrap").3, 1000.0);
    });
}

// -- wide, no wrap -------------------------------------------------------------------------

/// Fixed 10x10 children in a 100x100 row that does not wrap or shrink: they run on past the end.
#[test]
fn wide_no_wrap_simple() {
    Application::headless().run(|cx| {
        let w = draw(cx, |_| {
            node("row", "display: flex; width: 100px; height: 100px; overflow: hidden")
                .children((0..10_000).map(|i| node(format!("c{i}"), "width: 10px; height: 10px; flex: none")))
        });
        assert_eq!(rect(cx, w, "c0"), (0.0, 0.0, 10.0, 10.0));
        assert_eq!(rect(cx, w, "c9999"), (99_990.0, 0.0, 10.0, 10.0));
    });
}

// -- timings ------------------------------------------------------------------------------------

/// A scene for the timings: the container width changes by a pixel with the tick.
type Timed = (&'static str, usize, fn(u32) -> Div);

fn timed_scenes() -> Vec<Timed> {
    vec![
        ("expand_with_max_constraint", 3001, |t| {
            anon(format!("display: flex; width: {}px", 3000 + t % 2)).children((0..1000).flat_map(|_| {
                [anon("flex: 1 1 0; height: 10px"), anon("flex: 1 1 0; max-width: 1px; height: 10px")]
            }))
        }),
        ("nested_vertical_stack", 10_001, |t| {
            // 100 levels, 100 leaves in each
            fn level(d: u32, w: u32) -> Div {
                let inner = if d == 0 { None } else { Some(level(d - 1, w)) };
                anon(format!("display: flex; flex-direction: column; gap: 2px; width: {w}px"))
                    .children((0..100).map(|_| anon("height: 10px")))
                    .children(inner)
            }
            level(99, 100 + t % 2)
        }),
        ("wide_wrapping", 10_001, |t| {
            anon(format!("display: flex; flex-wrap: wrap; width: {}px", 1000 + t % 2))
                .children((0..10_000).map(|_| anon("width: 10px; height: 10px; flex: none")))
        }),
        ("percentage_and_ratio", 10_001, |t| {
            anon(format!("width: {}px; height: 4000px", 400 + t % 2)).children((0..10_000).map(|_| {
                anon("width: 50%; aspect-ratio: 2 / 1")
            }))
        }),
        ("flex_expand_weights", 15_001, |t| {
            anon(format!("display: flex; width: {}px", 15_000 + t % 2))
                .children((0..15_000).map(|i| anon(format!("flex: {} 1 0; height: 4px", i % 3 + 1))))
        }),
        ("wide_no_wrap_simple_many", 100_001, |t| {
            anon(format!("display: flex; width: {}px; height: 100px", 100 + t % 2))
                .children((0..100_000).map(|_| anon("width: 10px; height: 10px; flex: none")))
        }),
    ]
}

/// The time of a frame that changes the root width by a pixel: the whole tree is built, styled
/// and laid out again, so it is more than PanGui's layout-only number (which leaves out
/// creating the nodes).
#[test]
#[ignore = "timing; run with --release --ignored --nocapture"]
fn scene_timings() {
    Application::headless().run(|cx| {
        for (name, nodes, build) in timed_scenes() {
            let live = open(cx, 400.0, 300.0, Scene { build, tick: 0 });
            cx.draw(live.id);
            let frames = 60;
            let start = Instant::now();
            for _ in 0..frames {
                live.root().update(cx, |s, _| s.tick += 1);
                cx.notify_all();
                cx.draw(live.id);
            }
            let ms = start.elapsed().as_secs_f64() * 1e3 / f64::from(frames);
            println!("{name:<28} {nodes:>7} nodes {ms:>9.3} ms {:>8.1} ns/node", ms * 1e6 / nodes as f64);
        }
    });
}
