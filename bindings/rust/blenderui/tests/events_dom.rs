//! HTML's events: the capture pass before the bubbling one, `stopPropagation`, `mouseenter` against
//! `mouseover`, listeners of the whole window, pointer capture, the pointer events, focus and scroll.

use std::sync::Mutex;

use blenderui::prelude::*;

static LOG: Mutex<Vec<String>> = Mutex::new(Vec::new());

fn log(s: &str) {
    LOG.lock().unwrap().push(s.to_owned());
}

fn take() -> Vec<String> {
    std::mem::take(&mut *LOG.lock().unwrap())
}

/// A tree of three boxes inside each other, a box beside them, and a scroller.
struct Page;

impl Render for Page {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        div().id("body").size_full()
            .child(
                div().id("outer").absolute().left(px(0.0)).top(px(0.0)).w(px(200.0)).h(px(200.0))
                    .on_click_capture(|_, _, _| log("outer capture"))
                    .on_click(|_, _, _| log("outer bubble"))
                    .on_mouse_enter(|_, _, _| log("outer enter"))
                    .on_mouse_leave(|_, _, _| log("outer leave"))
                    .on_mouse_over(|_, _, _| log("outer over"))
                    .on_mouse_out(|_, _, _| log("outer out"))
                    .child(
                        div().id("inner").absolute().left(px(50.0)).top(px(50.0)).w(px(100.0)).h(px(100.0))
                            .on_click_capture(|_, _, _| log("inner capture"))
                            .on_click(|_, _, _| log("inner bubble"))
                            .on_double_click(|_, _, _| log("inner dblclick"))
                            .on_aux_click(|_, _, _| log("inner auxclick"))
                            .on_context_menu(|e, _, _| log(&format!("inner contextmenu {}", e.position.x.0 as i32)))
                            .on_pointer_down(|_, _, _| log("inner pointerdown"))
                            .on_press_drag(|e, _, _| log(&format!("inner pressdrag {:?}", e.direction)))
                            .on_any_mouse_down(|_, _, _| log("inner mousedown"))
                            .on_mouse_enter(|_, _, _| log("inner enter"))
                            .on_mouse_leave(|_, _, _| log("inner leave"))
                            .on_mouse_over(|_, _, _| log("inner over"))
                            .on_mouse_out(|_, _, _| log("inner out")),
                    ),
            )
            .child(
                div().id("side").absolute().left(px(300.0)).top(px(0.0)).w(px(100.0)).h(px(100.0))
                    .on_mouse_enter(|_, _, _| log("side enter")),
            )
    }
}

fn open(cx: &mut App) -> WindowId {
    blenderui::motion::lag_smoothing(0.0, 0.0);
    let opts = WindowOptions {
        window_bounds: Some(WindowBounds::Windowed(Bounds {
            origin: point(px(0.0), px(0.0)),
            size: size(px(500.0), px(300.0)),
        })),
        ..Default::default()
    };
    let w = cx.open_window(opts, |_, cx| cx.new(|_| Page)).unwrap().id;
    cx.draw(w);
    w
}

#[test]
fn passes_and_enter_leave() {
    Application::headless().run(|cx| {
        let w = open(cx);
        // capture runs from the root down, then bubble back up
        cx.simulate_click(w, 100.0, 100.0);
        let l = take();
        let at = |s: &str| l.iter().position(|x| x == s).unwrap_or(usize::MAX);
        assert!(at("outer capture") < at("inner capture") && at("inner capture") < at("inner bubble")
                && at("inner bubble") < at("outer bubble"), "click: {l:?}");
        // entering the inner box: over bubbles (and the pointer came from outside everything), enter is
        // for each element on its own, outermost first
        assert!(l.contains(&"outer enter".to_owned()) && l.contains(&"inner enter".to_owned()), "enter: {l:?}");
        assert!(at("outer enter") < at("inner enter"), "outermost first: {l:?}");

        // from the inner box to the outer one: the inner box is left, the outer one is not
        take();
        cx.simulate_mouse_move(w, 20.0, 20.0);
        let l = take();
        assert!(l.contains(&"inner leave".to_owned()) && !l.contains(&"outer leave".to_owned()), "leave: {l:?}");
        assert!(l.contains(&"inner out".to_owned()), "out bubbles from the left box: {l:?}");
        assert!(!l.contains(&"outer enter".to_owned()), "no second enter for the parent: {l:?}");

        // out of everything
        take();
        cx.simulate_mouse_move(w, 450.0, 250.0);
        let l = take();
        assert!(l.contains(&"outer leave".to_owned()), "left it: {l:?}");
    });
}

#[test]
fn pointer_events_come_before_mouse_events() {
    Application::headless().run(|cx| {
        let w = open(cx);
        take();
        cx.simulate_press(w, 100.0, 100.0);
        let l = take();
        let at = |s: &str| l.iter().position(|x| x == s).unwrap_or(usize::MAX);
        assert!(at("inner pointerdown") < at("inner mousedown"), "{l:?}");
        cx.simulate_release(w, 100.0, 100.0);
    });
}

static WIDE: Mutex<Vec<String>> = Mutex::new(Vec::new());

/// A drag: it takes the pointer on press, and hears the moves and the release wherever they are.
struct Drag;

impl Render for Drag {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        div().id("page").size_full()
            .child(
                div().id("handle").absolute().left(px(10.0)).top(px(10.0)).w(px(50.0)).h(px(50.0))
                    .on_pointer_down(|_, window, app| {
                        window.set_pointer_capture(app);
                        WIDE.lock().unwrap().push("down".into());
                    })
                    .on_pointer_move(|e, _, _| WIDE.lock().unwrap().push(format!("move {}", e.position.x.0 as i32)))
                    .on_pointer_up(|_, _, _| WIDE.lock().unwrap().push("up".into()))
                    .on_got_pointer_capture(|_, _, _| WIDE.lock().unwrap().push("got".into()))
                    .on_lost_pointer_capture(|_, _, _| WIDE.lock().unwrap().push("lost".into())),
            )
            .child(
                // hears everything in the window, in both passes
                div().id("watcher")
                    .on_mouse_event(|e: &MouseMoveEvent, phase, _, _| {
                        WIDE.lock().unwrap().push(format!("wide {:?} {}", phase, e.position.x.0 as i32));
                    }),
            )
    }
}

#[test]
fn pointer_capture_and_window_wide_listeners() {
    Application::headless().run(|cx| {
        blenderui::motion::lag_smoothing(0.0, 0.0);
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds {
                origin: point(px(0.0), px(0.0)),
                size: size(px(500.0), px(300.0)),
            })),
            ..Default::default()
        };
        let w = cx.open_window(opts, |_, cx| cx.new(|_| Drag)).unwrap().id;
        cx.draw(w);
        cx.simulate_mouse_move(w, 30.0, 30.0);
        WIDE.lock().unwrap().clear();
        cx.simulate_press(w, 30.0, 30.0);
        // far outside the handle: the handle still hears the pointer
        cx.simulate_mouse_move(w, 400.0, 200.0);
        cx.simulate_release(w, 400.0, 200.0);
        let l = std::mem::take(&mut *WIDE.lock().unwrap());
        assert!(l.contains(&"got".to_owned()), "took the pointer: {l:?}");
        assert!(l.contains(&"move 400".to_owned()), "heard a move far outside: {l:?}");
        assert!(l.contains(&"up".to_owned()), "and the release: {l:?}");
        assert!(l.contains(&"lost".to_owned()), "and let go with the button: {l:?}");
        // the window-wide listener: capture pass before bubble pass, for a move anywhere
        let cap = l.iter().position(|x| x == "wide Capture 400");
        let bub = l.iter().position(|x| x == "wide Bubble 400");
        assert!(cap.is_some() && bub.is_some() && cap < bub, "both passes: {l:?}");

        // without capture, the handle hears nothing far away
        WIDE.lock().unwrap().clear();
        cx.simulate_mouse_move(w, 450.0, 250.0);
        let l = std::mem::take(&mut *WIDE.lock().unwrap());
        assert!(!l.iter().any(|x| x.starts_with("move")), "no capture, no move: {l:?}");
    });
}

struct Stop;

impl Render for Stop {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        div().id("a").size_full()
            .on_click(|_, _, _| log("a bubble"))
            .child(
                div().id("b").absolute().left(px(0.0)).top(px(0.0)).w(px(100.0)).h(px(100.0))
                    .on_click_capture(|_, _, app| {
                        log("b capture");
                        app.stop_propagation();
                    })
                    .on_click(|_, _, _| log("b bubble"))
                    .child(div().id("c").size_full().on_click(|_, _, _| log("c bubble"))),
            )
    }
}

#[test]
fn stop_propagation_in_the_capture_pass() {
    Application::headless().run(|cx| {
        blenderui::motion::lag_smoothing(0.0, 0.0);
        let w = cx.open_window(WindowOptions::default(), |_, cx| cx.new(|_| Stop)).unwrap().id;
        cx.draw(w);
        take();
        cx.simulate_click(w, 50.0, 50.0);
        assert_eq!(take(), ["b capture"], "stopped on the way down: nothing below or after it hears the click");
    });
}

struct Focus;

impl Render for Focus {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        div().id("page").size_full()
            .child(div().id("one").absolute().left(px(0.0)).top(px(0.0)).w(px(100.0)).h(px(100.0))
                .on_focus(|_, _, _| log("one focus")).on_blur(|_, _, _| log("one blur"))
                .on_focus_in(|_, _, _| log("one focusin")).on_focus_out(|_, _, _| log("one focusout")))
            .child(div().id("two").absolute().left(px(200.0)).top(px(0.0)).w(px(100.0)).h(px(100.0))
                .on_focus(|_, _, _| log("two focus")).on_blur(|_, _, _| log("two blur")))
            .child(
                div().id("scroller").absolute().left(px(0.0)).top(px(150.0)).w(px(100.0)).h(px(100.0))
                    .overflow_y_scroll()
                    .on_scroll(|e, _, _| log(&format!("scroll {}", e.offset.y.0 as i32)))
                    .child(div().h(px(500.0)).w(px(50.0))),
            )
    }
}

#[test]
fn focus_and_scroll() {
    Application::headless().run(|cx| {
        blenderui::motion::lag_smoothing(0.0, 0.0);
        let w = cx.open_window(WindowOptions::default(), |_, cx| cx.new(|_| Focus)).unwrap().id;
        cx.draw(w);
        take();
        cx.simulate_click(w, 50.0, 50.0);
        assert_eq!(take(), ["one focus", "one focusin"], "focused");
        cx.simulate_click(w, 250.0, 50.0);
        assert_eq!(take(), ["one blur", "one focusout", "two focus"], "focus moves: out of one, into two");
        cx.simulate_scroll(w, 50.0, 200.0, -1);
        let l = take();
        assert!(l.iter().any(|x| x.starts_with("scroll ") && x != "scroll 0"), "the scroller scrolled: {l:?}");
    });
}

#[test]
fn double_click_and_the_other_buttons() {
    Application::headless().run(|cx| {
        let w = open(cx);
        take();
        cx.simulate_click(w, 100.0, 100.0);
        cx.simulate_click(w, 100.0, 100.0);
        let l = take();
        assert_eq!(l.iter().filter(|x| *x == "inner bubble").count(), 2, "two clicks: {l:?}");
        assert_eq!(l.iter().filter(|x| *x == "inner dblclick").count(), 1, "one double click: {l:?}");

        take();
        cx.simulate_button(w, 100.0, 100.0, MouseButton::Right);
        let l = take();
        assert!(l.contains(&"inner auxclick".to_owned()) && l.contains(&"inner contextmenu 100".to_owned()), "{l:?}");
        assert!(!l.contains(&"inner bubble".to_owned()), "a right click isn't a click: {l:?}");
    });
}

struct Rsx;

impl Render for Rsx {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        rsx! {
            <div id="a" style="width: 100px; height: 100px" onClickCapture={|| log("rsx capture")} onClick={|| log("rsx bubble")}
                 onPointerDown={|| log("rsx pointerdown")}>
                <div id="b" style="width: 50px; height: 50px" onMouseEnter={|| log("rsx enter")} />
            </div>
        }
    }
}

#[test]
fn rsx_spellings() {
    Application::headless().run(|cx| {
        blenderui::motion::lag_smoothing(0.0, 0.0);
        let w = cx.open_window(WindowOptions::default(), |_, cx| cx.new(|_| Rsx)).unwrap().id;
        cx.draw(w);
        take();
        cx.simulate_click(w, 20.0, 20.0);
        let l = take();
        assert!(l.contains(&"rsx pointerdown".to_owned()) && l.contains(&"rsx enter".to_owned()), "{l:?}");
        let at = |s: &str| l.iter().position(|x| x == s).unwrap_or(usize::MAX);
        assert!(at("rsx capture") < at("rsx bubble"), "{l:?}");
    });
}

#[test]
fn press_drag_comes_once_past_the_threshold() {
    Application::headless().run(|cx| {
        let w = open(cx);
        cx.simulate_mouse_move(w, 100.0, 100.0);
        take();
        cx.simulate_press(w, 100.0, 100.0);
        cx.simulate_mouse_move(w, 102.0, 100.0); // 2 px: not a drag (Blender's threshold is 3)
        assert!(!take().iter().any(|x| x.starts_with("inner pressdrag")));
        cx.simulate_mouse_move(w, 110.0, 100.0);
        cx.simulate_mouse_move(w, 130.0, 100.0);
        let l = take();
        assert_eq!(l.iter().filter(|x| x.starts_with("inner pressdrag")).count(), 1, "once per press: {l:?}");
        assert!(l.contains(&"inner pressdrag East".to_owned()), "to the right: {l:?}");
        cx.simulate_release(w, 130.0, 100.0);
    });
}

/// A frame renders again for a hover only when something is styled by it. The built-in sheets style nothing
/// by interaction but links (`a:hover`), which the frame knows by the tag: another rule there needs the
/// frame to learn about it (`layout::div_states`).
#[test]
fn the_built_in_sheets_only_style_links_by_hover() {
    for css in [blenderui::html::UA_CSS, blenderui::html::UA_STYLED_CSS] {
        assert_eq!(css.matches(":hover").count(), css.matches("a:hover").count(), "a hover rule that isn't a link's");
        assert_eq!(css.matches(":active").count(), 0, ":active in a built-in sheet");
        assert_eq!(css.matches(":focus").count(), 0, ":focus in a built-in sheet");
    }
}

struct Hover;

impl Render for Hover {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        // the first renders count: they are what a hover would make again
        RENDERS.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        div().size_full()
            .child(div().id("plain").absolute().left(px(0.0)).top(px(0.0)).w(px(100.0)).h(px(100.0)))
            .child(div().id("styled").absolute().left(px(200.0)).top(px(0.0)).w(px(100.0)).h(px(100.0))
                .hover(|s| s.bg(rgb(0xff0000))))
    }
}

static RENDERS: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);

#[test]
fn hover_renders_again_only_when_styled_by_it() {
    use std::sync::atomic::Ordering::Relaxed;
    Application::headless().run(|cx| {
        let w = cx.open_window(WindowOptions::default(), |_, cx| cx.new(|_| Hover)).unwrap().id;
        cx.draw(w);
        // with a hover style somewhere, the pointer entering an element renders again
        let before = RENDERS.load(Relaxed);
        cx.simulate_mouse_move(w, 250.0, 50.0);
        assert!(RENDERS.load(Relaxed) > before, "styled by hover: rendered again");
    });
}

struct Plain;

static PLAIN_RENDERS: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);

impl Render for Plain {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        PLAIN_RENDERS.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        div().size_full()
            .child(div().id("a").absolute().left(px(0.0)).top(px(0.0)).w(px(100.0)).h(px(100.0))
                .on_mouse_enter(|_, _, _| log("a enter")))
            .child(div().id("b").absolute().left(px(200.0)).top(px(0.0)).w(px(100.0)).h(px(100.0)))
    }
}

#[test]
fn nothing_styled_by_hover_means_no_render() {
    use std::sync::atomic::Ordering::Relaxed;
    Application::headless().run(|cx| {
        let w = cx.open_window(WindowOptions::default(), |_, cx| cx.new(|_| Plain)).unwrap().id;
        cx.draw(w);
        take();
        let before = PLAIN_RENDERS.load(Relaxed);
        for x in [50.0, 250.0, 50.0, 250.0] {
            cx.simulate_mouse_move(w, x, 50.0);
        }
        cx.simulate_click(w, 50.0, 50.0);
        assert_eq!(PLAIN_RENDERS.load(Relaxed), before, "moving and clicking over unstyled elements doesn't render the view");
        assert!(take().contains(&"a enter".to_owned()), "and the listeners still hear them");
    });
}
