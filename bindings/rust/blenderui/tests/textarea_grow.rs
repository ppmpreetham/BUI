//! `field-sizing: content` on a textarea: it wraps at its width and is as tall as its lines
//! (at least `rows`, at most `max-height`); without it the height stays the rows'.

use blenderui::prelude::*;

struct Area {
    text: &'static str,
    style: &'static str,
}

impl Render for Area {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        rsx! {
            <body style="margin: 0">
                <textarea id="t" rows="1" style={self.style} value={self.text.to_string()} />
            </body>
        }
    }
}

const SHORT: &str = "hi";
const LONG: &str = "a long message that should wrap onto several lines when the field is this narrow";

fn height(cx: &mut App, text: &'static str, style: &'static str) -> f32 {
    let opts = WindowOptions {
        window_bounds: Some(WindowBounds::Windowed(Bounds {
            origin: point(px(0.0), px(0.0)),
            size: size(px(400.0), px(300.0)),
        })),
        ..Default::default()
    };
    let id = cx.open_window(opts, |_, cx| cx.new(|_| Area { text, style })).unwrap().id;
    cx.draw(id);
    cx.element_bounds(id, "t").unwrap().size.height
}

#[test]
fn grows_with_the_wrapped_lines() {
    Application::headless().run(|cx| {
        let grow = "width: 140px; field-sizing: content";
        let one = height(cx, SHORT, grow);
        let many = height(cx, LONG, grow);
        let narrow = height(cx, LONG, "width: 90px; field-sizing: content");
        assert!(many > one * 1.9, "a long text wraps onto more lines ({one} -> {many})");
        assert!(narrow > many, "a narrower field wraps onto more ({many} -> {narrow})");
    });
}

#[test]
fn stays_the_rows_without_field_sizing() {
    Application::headless().run(|cx| {
        let fixed = "width: 140px";
        assert_eq!(height(cx, SHORT, fixed), height(cx, LONG, fixed), "no field-sizing: the height is the rows'");
    });
}

#[test]
fn max_height_caps_the_growth() {
    Application::headless().run(|cx| {
        let capped = height(cx, LONG, "width: 90px; field-sizing: content; max-height: 40px");
        assert!(capped <= 40.01, "max-height wins ({capped})");
    });
}

#[test]
fn rows_attribute_sets_the_minimum_lines() {
    Application::headless().run(|cx| {
        // `rows="1"` (the helper's textarea): one line tall, not the browser's two
        let one = height(cx, SHORT, "width: 140px; field-sizing: content");
        let three = height(cx, "a\nb\nc", "width: 140px; field-sizing: content");
        assert!(one < 25.0, "a single row ({one})");
        assert!(three > one * 1.9, "three lines are taller ({one} -> {three})");
    });
}

#[test]
fn padding_sits_inside_the_field_without_a_gap() {
    Application::headless().run(|cx| {
        // the box is the lines plus the padding, once: no extra gap under the text
        let plain = height(cx, SHORT, "width: 140px; field-sizing: content");
        let padded = height(cx, SHORT, "width: 140px; field-sizing: content; padding: 8px");
        assert!((padded - plain - 16.0).abs() < 1.5, "padding adds 2 x 8px ({plain} -> {padded})");
    });
}

#[test]
fn max_height_shows_whole_lines() {
    Application::headless().run(|cx| {
        // 3 lines + 2 x 8px padding = 15hpx*3 + 6 + 16; a cap of 62px must not leave a cut fourth line
        let lines = |h: f32| ((h - 22.0) / 15.0).fract();
        let capped = height(cx, LONG, "width: 90px; field-sizing: content; padding: 8px; max-height: 62px");
        assert!(capped <= 62.01, "max-height wins ({capped})");
        assert!(lines(capped).abs() < 0.07 || (1.0 - lines(capped)).abs() < 0.07, "whole lines ({capped})");
    });
}

#[test]
fn growing_does_not_leave_the_text_scrolled() {
    // typing a newline scrolls the caret into the old, shorter box; once the box has grown the
    // first line is back at the top
    Application::headless().run(|cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds {
                origin: point(px(0.0), px(0.0)),
                size: size(px(400.0), px(300.0)),
            })),
            ..Default::default()
        };
        let id = cx.open_window(opts, |_, cx| cx.new(|_| Area { text: "a\nb\nc\nd", style: "width: 140px; field-sizing: content" })).unwrap().id;
        let img = cx.draw(id);
        let b = cx.element_bounds(id, "t").unwrap();
        // ink in the first row of the box: the first line is drawn at the top
        let s = img.0 as f32 / 400.0;
        let (x0, y0) = ((b.origin.x * s) as usize + 2, (b.origin.y * s) as usize + 2);
        let dark = (0..(14.0 * s) as usize).any(|dy| (0..(40.0 * s) as usize).any(|dx| (img.2[(y0 + dy) * img.0 + x0 + dx] & 0xff) < 100));
        assert!(dark, "the first line is visible at the top of the box");
    });
}
