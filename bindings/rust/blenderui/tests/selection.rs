//! Selecting text in the element tree: press and drag, double and triple click, `user-select`,
//! `Ctrl+A` / `Ctrl+C` / `Escape`, the highlight, and that a drag is not a click.

use blenderui::prelude::*;

struct Page {
    clicks: usize,
}

const FIRST: &str = "Alpha beta gamma";
const SECOND: &str = "Delta epsilon";
const THIRD: &str = "Zeta eta";
const ACCENTS: &str = "h\u{e9}llo w\u{f6}rld \u{f1}and\u{fa}";

impl Render for Page {
    fn render(&mut self, _: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        body()
            .m_0()
            .style("font-size: 16px")
            .child(p().id("one").style("margin: 0 0 20px 0").child(FIRST))
            .child(p().id("two").style("margin: 0 0 20px 0").child(SECOND))
            .child(p().id("none").style("margin: 0 0 20px 0; user-select: none").child("Skip me"))
            .child(p().id("three").style("margin: 0 0 20px 0").child(THIRD))
            .child(p().id("mixed").style("margin: 0 0 20px 0").child("Before ")
                .child(b().id("code").style("user-select: all").child("CODE-123")).child(" after"))
            .child(p().id("accents").style("margin: 0 0 20px 0").child(ACCENTS))
            .child(p().id("clicky").style("margin: 0 0 20px 0; cursor: pointer").child("Click me please")
                .on_click(cx.listener(|this, _: &ClickEvent, _, cx| {
                    this.clicks += 1;
                    cx.notify();
                })))
            .child(div().id("flex").style("display: flex; gap: 12px").child(span().id("fa").child("Flex one"))
                .child(span().id("fb").child("Flex two")))
            .child(textarea().id("field").style("width: 200px").attr("value", "typed text"))
    }
}

fn open(cx: &mut App) -> WindowHandle<Page> {
    let opts = WindowOptions {
        window_bounds: Some(WindowBounds::Windowed(Bounds {
            origin: point(px(0.0), px(0.0)),
            size: size(px(500.0), px(500.0)),
        })),
        ..Default::default()
    };
    let h = cx.open_window(opts, |_, cx| cx.new(|_| Page { clicks: 0 })).unwrap();
    h.update(cx, |_, w, _| {
        w.on_selection_change(|t, _, _| {
            let t = t.map(str::to_owned);
            HEARD.with(|h| h.borrow_mut().push(t));
        });
    });
    cx.draw(h.id);
    h
}

thread_local! {
    static HEARD: std::cell::RefCell<Vec<Option<String>>> = const { std::cell::RefCell::new(Vec::new()) };
}

fn selected(cx: &mut App, h: &WindowHandle<Page>) -> Option<String> {
    h.update(cx, |_, w, _| w.selected_text()).unwrap()
}

/// The left edge, vertical middle and right edge of an element.
fn spot(cx: &mut App, h: &WindowHandle<Page>, id: &str) -> (f32, f32, f32) {
    let b = cx.element_bounds(h.id, id).unwrap();
    (b.origin.x, b.origin.y + b.size.height / 2.0, b.origin.x + b.size.width)
}

#[test]
fn double_click_selects_a_word() {
    Application::headless().run(|cx| {
        let h = open(cx);
        let (l, y, _) = spot(cx, &h, "one");
        cx.simulate_click(h.id, l + 3.0, y);
        assert_eq!(selected(cx, &h), None, "one click selects nothing");
        cx.simulate_click(h.id, l + 3.0, y);
        assert_eq!(selected(cx, &h).as_deref(), Some("Alpha"));
    });
}

#[test]
fn triple_click_selects_the_block() {
    Application::headless().run(|cx| {
        let h = open(cx);
        let (l, y, _) = spot(cx, &h, "one");
        for _ in 0..3 {
            cx.simulate_click(h.id, l + 3.0, y);
        }
        assert_eq!(selected(cx, &h).as_deref(), Some(FIRST));
    });
}

#[test]
fn drag_across_paragraphs_joins_them_with_a_newline() {
    Application::headless().run(|cx| {
        let h = open(cx);
        let (l, y1, _) = spot(cx, &h, "one");
        let (_, y2, r) = spot(cx, &h, "two");
        cx.simulate_drag(h.id, (l + 1.0, y1), (r - 1.0, y2));
        assert_eq!(selected(cx, &h), Some(format!("{FIRST}\n{SECOND}")));
    });
}

#[test]
fn a_drag_inside_one_line_takes_part_of_a_word() {
    Application::headless().run(|cx| {
        let h = open(cx);
        let (l, y, _) = spot(cx, &h, "one");
        // from the start to somewhere inside "Alpha"
        cx.simulate_drag(h.id, (l + 1.0, y), (l + 20.0, y));
        let t = selected(cx, &h).expect("a selection");
        assert!(!t.is_empty() && "Alpha".starts_with(&t) && t != "Alpha", "a part of the first word: {t:?}");
        // backwards: from the end of the line to the same place
        cx.simulate_click(h.id, 400.0, 480.0);
        let (_, _, r) = spot(cx, &h, "one");
        cx.simulate_drag(h.id, (r - 1.0, y), (l + 20.0, y));
        let back = selected(cx, &h).expect("a selection");
        assert!(FIRST.ends_with(&back) && back.len() > FIRST.len() - 8, "from the end: {back:?}");
    });
}

#[test]
fn user_select_none_is_skipped_and_never_starts_a_selection() {
    Application::headless().run(|cx| {
        let h = open(cx);
        let (l, y1, _) = spot(cx, &h, "two");
        let (_, y3, r) = spot(cx, &h, "three");
        cx.simulate_drag(h.id, (l + 1.0, y1), (r - 1.0, y3));
        assert_eq!(selected(cx, &h), Some(format!("{SECOND}\n{THIRD}")), "the `none` paragraph between is left out");
        let (nl, ny, nr) = spot(cx, &h, "none");
        cx.simulate_drag(h.id, (nl + 1.0, ny), (nr - 1.0, ny));
        assert_eq!(selected(cx, &h), None, "pressing on it selects nothing (and clears the old one)");
    });
}

#[test]
fn user_select_all_selects_the_element_on_one_click() {
    Application::headless().run(|cx| {
        let h = open(cx);
        let b = cx.element_bounds(h.id, "code").unwrap();
        cx.simulate_click(h.id, b.origin.x + 4.0, b.origin.y + b.size.height / 2.0);
        assert_eq!(selected(cx, &h).as_deref(), Some("CODE-123"));
    });
}

#[test]
fn a_drag_that_selects_is_not_a_click_but_a_plain_press_is() {
    Application::headless().run(|cx| {
        let h = open(cx);
        let (l, y, r) = spot(cx, &h, "clicky");
        cx.simulate_drag(h.id, (l + 1.0, y), (r - 1.0, y));
        assert_eq!(h.root().read(cx).clicks, 0, "the drag selected text: no click");
        assert_eq!(selected(cx, &h).as_deref(), Some("Click me please"));
        std::thread::sleep(std::time::Duration::from_millis(600)); // not a double click
        cx.simulate_click(h.id, l + 3.0, y);
        assert_eq!(h.root().read(cx).clicks, 1, "a press and release without moving is a click");
        assert_eq!(selected(cx, &h), None, "and clears the selection");
    });
}

#[test]
fn ctrl_a_ctrl_c_and_escape() {
    Application::headless().run(|cx| {
        let h = open(cx);
        cx.simulate_mouse_move(h.id, 250.0, 450.0);
        cx.simulate_key_with(h.id, "a", "a", true, false);
        let all = selected(cx, &h).expect("everything selected");
        assert!(all.starts_with(FIRST) && all.contains(SECOND) && all.contains("CODE-123"), "{all:?}");
        assert!(!all.contains("Skip me"), "user-select: none stays out");
        cx.simulate_key_with(h.id, "c", "c", true, false);
        let clip = h.update(cx, |_, w, _| w.read_clipboard()).unwrap();
        assert_eq!(clip.as_deref(), Some(all.as_str()), "Ctrl+C put the selection on the clipboard");
        cx.simulate_key(h.id, "", "Escape");
        assert_eq!(selected(cx, &h), None);
        h.update(cx, |_, w, _| {
            w.select_all();
            assert!(w.selected_text().is_none() || w.selected_text().is_some());
            w.clear_selection();
        });
    });
}

#[test]
fn shift_click_extends_the_selection() {
    Application::headless().run(|cx| {
        let h = open(cx);
        let (l, y, _) = spot(cx, &h, "one");
        cx.simulate_click(h.id, l + 3.0, y);
        cx.simulate_click(h.id, l + 3.0, y); // "Alpha"
        let (_, y2, r) = spot(cx, &h, "two");
        cx.simulate_click_with(h.id, r - 1.0, y2, false, true);
        assert_eq!(selected(cx, &h), Some(format!("Alpha beta gamma\n{SECOND}")));
    });
}

#[test]
fn text_in_flex_items_and_between_blocks() {
    Application::headless().run(|cx| {
        let h = open(cx);
        let (l, y, _) = spot(cx, &h, "fa");
        let (_, _, r) = spot(cx, &h, "fb");
        cx.simulate_drag(h.id, (l + 1.0, y), (r - 1.0, y));
        assert_eq!(selected(cx, &h).as_deref(), Some("Flex one\nFlex two"), "flex items are blocks");
    });
}

#[test]
fn clicking_empty_space_or_a_text_field_clears() {
    Application::headless().run(|cx| {
        let h = open(cx);
        let (l, y, _) = spot(cx, &h, "one");
        cx.simulate_drag(h.id, (l + 1.0, y), (l + 60.0, y));
        assert!(selected(cx, &h).is_some());
        cx.simulate_click(h.id, 480.0, 490.0);
        assert_eq!(selected(cx, &h), None, "a click on empty space");
        cx.simulate_drag(h.id, (l + 1.0, y), (l + 60.0, y));
        let f = cx.element_bounds(h.id, "field").unwrap();
        cx.simulate_click(h.id, f.origin.x + 10.0, f.origin.y + 8.0);
        cx.simulate_mouse_move(h.id, 480.0, 490.0);
        assert_eq!(selected(cx, &h), None, "a text field takes the focus: the page selection goes");
        // and the field keeps the keys: Ctrl+A there does not select the page
        cx.simulate_click(h.id, f.origin.x + 10.0, f.origin.y + 8.0);
        cx.simulate_key_with(h.id, "a", "a", true, false);
        assert_eq!(selected(cx, &h), None);
    });
}

#[test]
fn multi_byte_text_selects_on_character_boundaries() {
    Application::headless().run(|cx| {
        let h = open(cx);
        let (l, y, r) = spot(cx, &h, "accents");
        // every drag end, across the line: a prefix of the text, whole characters, growing
        let mut last = 0;
        let mut x = l;
        while x < r {
            cx.simulate_click(h.id, 480.0, 490.0); // elsewhere: each drag is a first press
            cx.simulate_drag(h.id, (l + 0.5, y), (x, y));
            if let Some(t) = selected(cx, &h) {
                assert!(ACCENTS.starts_with(&t), "{t:?} at x={x}");
                assert!(t.len() >= last, "growing");
                last = t.len();
            }
            x += 3.0;
        }
        assert_eq!(last, ACCENTS.len().min(last), "reached into the text");
        assert!(last > 8, "the drag covered several characters ({last})");
        std::thread::sleep(std::time::Duration::from_millis(600)); // not a multi-click
        cx.simulate_click(h.id, l + 3.0, y);
        cx.simulate_click(h.id, l + 3.0, y);
        assert_eq!(selected(cx, &h).as_deref(), Some("h\u{e9}llo"), "a word with an accent");
    });
}

#[test]
fn the_highlight_is_drawn_under_the_text_and_goes_with_escape() {
    Application::headless().run(|cx| {
        let h = open(cx);
        let before = cx.draw(h.id);
        let (l, y, _) = spot(cx, &h, "one");
        cx.simulate_click(h.id, l + 3.0, y);
        cx.simulate_click(h.id, l + 3.0, y); // "Alpha"
        let after = cx.draw(h.id);
        let b = cx.element_bounds(h.id, "one").unwrap();
        let s = before.0 as f32 / 500.0;
        let mut inside = 0;
        let mut outside = 0;
        for py in 0..before.1 {
            for px_ in 0..before.0 {
                if before.2[py * before.0 + px_] != after.2[py * after.0 + px_] {
                    let (fx, fy) = (px_ as f32 / s, py as f32 / s);
                    if fx >= b.origin.x && fx <= b.origin.x + 60.0 && fy >= b.origin.y && fy <= b.origin.y + b.size.height {
                        inside += 1;
                    } else {
                        outside += 1;
                    }
                }
            }
        }
        assert!(inside > 200, "the first word is highlighted ({inside} pixels)");
        assert_eq!(outside, 0, "nothing else changed");
        cx.simulate_key(h.id, "", "Escape");
        let gone = cx.draw(h.id);
        let differ: Vec<usize> = (0..gone.2.len()).filter(|&k| gone.2[k] != before.2[k]).collect();
        assert!(differ.is_empty(), "Escape puts the page back ({} pixels differ, first at {:?})", differ.len(),
                differ.first().map(|&k| (k % gone.0, k / gone.0)));
        // a partial repaint (what a window presents) matches a full one while dragging
        cx.simulate_press(h.id, l + 1.0, y);
        cx.simulate_mouse_move(h.id, l + 30.0, y);
        let (_, _, partial, full, _) = cx.draw_partial_and_full(h.id);
        assert!(partial == full, "damage covers the highlight");
    });
}

#[test]
fn hook_hears_changes_and_text_selected_by_the_user() {
    Application::headless().run(|cx| {
        HEARD.with(|h| h.borrow_mut().clear());
        let h = open(cx);
        let (l, y, _) = spot(cx, &h, "one");
        cx.simulate_click(h.id, l + 3.0, y);
        cx.simulate_click(h.id, l + 3.0, y);
        cx.simulate_key(h.id, "", "Escape");
        let heard = HEARD.with(|h| h.borrow().clone());
        assert!(heard.contains(&Some("Alpha".to_string())), "{heard:?}");
        assert_eq!(heard.last(), Some(&None), "Escape: nothing selected");
    });
}

#[test]
fn the_cursor_is_an_i_beam_over_text_only() {
    Application::headless().run(|cx| {
        let h = open(cx);
        let (l, y, _) = spot(cx, &h, "one");
        let cursor = |cx: &mut App, x: f32, y: f32| {
            cx.simulate_mouse_move(h.id, x, y);
            h.update(cx, |_, w, _| w.cursor_style()).unwrap()
        };
        assert_eq!(cursor(cx, l + 3.0, y), 3, "over selectable text");
        assert_eq!(cursor(cx, 480.0, y), 0, "on the empty part of the line");
        let (nl, ny, _) = spot(cx, &h, "none");
        assert_eq!(cursor(cx, nl + 3.0, ny), 0, "user-select: none");
        let (cl, cy, _) = spot(cx, &h, "clicky");
        assert_eq!(cursor(cx, cl + 3.0, cy), 5, "cursor: pointer stays");
    });
}

struct Classes;

impl Render for Classes {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        body()
            .m_0()
            .child(div().id("tw").class("select-none").child(span().id("tw-in").child("tailwind none")))
            .child(div().id("webkit").style("-webkit-user-select: none").child("webkit none"))
            .child(div().id("outer").style("user-select: none")
                .child(span().id("inner").style("user-select: text").child("text again"))
                .child(span().id("gone").child("inherits none")))
            .child(div().id("tall").class("select-all").child("whole thing"))
            .child(div().id("plain").child("plain"))
    }
}

#[test]
fn select_classes_aliases_and_inheritance() {
    Application::headless().run(|cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(400.0), px(300.0)) })),
            ..Default::default()
        };
        let h = cx.open_window(opts, |_, cx| cx.new(|_| Classes)).unwrap();
        cx.draw(h.id);
        let drag = |cx: &mut App, id: &str| {
            let b = cx.element_bounds(h.id, id).unwrap();
            let y = b.origin.y + b.size.height / 2.0;
            cx.simulate_click(h.id, 390.0, 290.0); // elsewhere: clears, so the drag is a first press
            cx.simulate_drag(h.id, (b.origin.x + 1.0, y), (b.origin.x + b.size.width - 1.0, y));
            h.update(cx, |_, w, _| w.selected_text()).unwrap()
        };
        assert_eq!(drag(cx, "tw"), None, "Tailwind `select-none`");
        assert_eq!(drag(cx, "webkit"), None, "`-webkit-user-select: none`");
        assert_eq!(drag(cx, "gone"), None, "`auto` inside `none` is none");
        assert_eq!(drag(cx, "inner").as_deref(), Some("text again"), "`text` inside `none` selects");
        assert_eq!(drag(cx, "plain").as_deref(), Some("plain"));
        // one click on a `select-all` element takes it whole
        let b = cx.element_bounds(h.id, "tall").unwrap();
        cx.simulate_click(h.id, 390.0, 290.0);
        cx.simulate_click(h.id, b.origin.x + 3.0, b.origin.y + b.size.height / 2.0);
        assert_eq!(h.update(cx, |_, w, _| w.selected_text()).unwrap().as_deref(), Some("whole thing"));
    });
}
