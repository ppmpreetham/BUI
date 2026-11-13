//! Event types passed to listeners (named as in gpui).

use crate::geometry::{Pixels, Point};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MouseButton {
    Left,
    Right,
    Middle,
}

/// Modifier keys held during an event.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Modifiers {
    pub shift: bool,
    pub control: bool,
    pub alt: bool,
}

impl Modifiers {
    pub(crate) fn from_kit(mods: i32) -> Modifiers {
        Modifiers { shift: mods & 1 != 0, control: mods & 2 != 0, alt: mods & 4 != 0 }
    }
}

#[derive(Clone, Debug)]
pub struct MouseDownEvent {
    pub button: MouseButton,
    pub position: Point<Pixels>,
    pub modifiers: Modifiers,
    pub click_count: usize,
}

#[derive(Clone, Debug)]
pub struct MouseUpEvent {
    pub button: MouseButton,
    pub position: Point<Pixels>,
    pub modifiers: Modifiers,
}

#[derive(Clone, Debug)]
pub struct MouseMoveEvent {
    pub position: Point<Pixels>,
    pub pressed_button: Option<MouseButton>,
    pub modifiers: Modifiers,
}

/// A click: the press and the release.
#[derive(Clone, Debug)]
pub struct ClickEvent {
    pub down: MouseDownEvent,
    pub up: MouseUpEvent,
}

impl ClickEvent {
    pub fn click_count(&self) -> usize {
        self.down.click_count
    }
}

/// Pixels one wheel line scrolls a page; a touch pan's `delta` is its pixels over this.
pub const WHEEL_LINE_PX: f32 = 40.0;

#[derive(Clone, Debug)]
pub struct ScrollWheelEvent {
    pub position: Point<Pixels>,
    /// Lines: positive scrolls the content up (wheel away from the user).
    pub delta: f32,
    pub modifiers: Modifiers,
    /// Two fingers on a touch screen, not a wheel or trackpad: `delta` is then the pixels the
    /// fingers moved over [`WHEEL_LINE_PX`], so `delta * WHEEL_LINE_PX` keeps the content under
    /// them. `modifiers.shift` marks the horizontal part of a pan.
    pub touch: bool,
}

/// One step of a touch screen contact (`App::simulate_touch`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TouchPhase {
    Down,
    Move,
    Up,
    /// The platform took the contact back: nothing it did is kept.
    Cancel,
}

impl TouchPhase {
    pub(crate) const fn to_kit(self) -> i32 {
        match self {
            TouchPhase::Down => blenderui_sys::BK_TOUCH_DOWN,
            TouchPhase::Move => blenderui_sys::BK_TOUCH_MOVE,
            TouchPhase::Up => blenderui_sys::BK_TOUCH_UP,
            TouchPhase::Cancel => blenderui_sys::BK_TOUCH_CANCEL,
        }
    }
}

/// What a [`GestureEvent`] is: Blender's MOUSEPAN, MOUSEZOOM and MOUSEROTATE.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GestureKind {
    /// Two fingers moved together: `dx`, `dy`.
    Pan,
    /// A pinch: `magnify`.
    Magnify,
    /// A turn: `degrees`.
    Rotate,
}

/// A touch screen or trackpad gesture (`on_gesture`).
///
/// Two fingers on a touch screen pan, magnify and rotate about their centre; trackpads send
/// magnify and rotate. A listener that calls `cx.stop_propagation()` keeps a pan from also
/// scrolling the page (and from reaching `on_scroll_wheel` listeners).
#[derive(Clone, Debug)]
pub struct GestureEvent {
    pub kind: GestureKind,
    /// The gesture's centre: between the fingers, or the pointer for a trackpad.
    pub position: Point<Pixels>,
    /// `Pan`: pixels the fingers moved since the last event (the content should follow).
    pub dx: f32,
    pub dy: f32,
    /// `Magnify`: scale change since the last event (0.1 = 10% larger).
    pub magnify: f32,
    /// `Rotate`: degrees since the last event, positive counter-clockwise.
    pub degrees: f32,
    pub modifiers: Modifiers,
    /// From a touch screen (`false`: a trackpad).
    pub touch: bool,
}

/// A key: `key` is the keysym (`"a"`, `"Return"`, `"Left"` ...).
#[derive(Clone, Debug)]
pub struct Keystroke {
    pub key: String,
    pub key_char: Option<String>,
    pub modifiers: Modifiers,
}

#[derive(Clone, Debug)]
pub struct KeyDownEvent {
    pub keystroke: Keystroke,
}

#[derive(Clone, Debug)]
pub struct KeyUpEvent {
    pub keystroke: Keystroke,
}

/// A form control's value (`input`, `select`, `textarea` ...).
#[derive(Clone, Debug, Default)]
pub struct ChangeEvent {
    /// The value as text (what `.value` holds in the DOM).
    pub value: String,
    /// Checkboxes and radio buttons.
    pub checked: bool,
    /// Numbers, ranges and selects (the selected index).
    pub number: f64,
}
