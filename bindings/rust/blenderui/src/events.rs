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

#[derive(Clone, Debug)]
pub struct ScrollWheelEvent {
    pub position: Point<Pixels>,
    /// Lines: positive scrolls the content up (wheel away from the user).
    pub delta: f32,
    pub modifiers: Modifiers,
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

/// HTML's two passes over the elements an event goes through: down from the root to the target
/// (`Capture`), then back up (`Bubble`). A listener of the whole window hears both.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DispatchPhase {
    Capture,
    Bubble,
}

impl DispatchPhase {
    #[must_use]
    pub const fn bubble(self) -> bool {
        matches!(self, Self::Bubble)
    }

    #[must_use]
    pub const fn capture(self) -> bool {
        matches!(self, Self::Capture)
    }
}

/// Which event a listener hears: HTML's `on*` attributes, one for one.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum EventKind {
    // mouse
    Click,
    DoubleClick,
    AuxClick,
    ContextMenu,
    MouseDown,
    MouseUp,
    MouseMove,
    MouseEnter,
    MouseLeave,
    MouseOver,
    MouseOut,
    Wheel,
    // pointer
    PointerDown,
    PointerUp,
    PointerMove,
    PointerEnter,
    PointerLeave,
    PointerOver,
    PointerOut,
    PointerCancel,
    GotPointerCapture,
    LostPointerCapture,
    // keyboard
    KeyDown,
    KeyUp,
    // focus
    Focus,
    Blur,
    FocusIn,
    FocusOut,
    // the rest
    Scroll,
    Hover,
    Change,
    Input,
}

impl EventKind {
    /// Whether it goes up to the ancestors after the target (`mouseenter`, `focus`, `scroll` ... don't).
    #[must_use]
    pub const fn bubbles(self) -> bool {
        !matches!(
            self,
            Self::MouseEnter | Self::MouseLeave | Self::PointerEnter | Self::PointerLeave | Self::Focus | Self::Blur
                | Self::Scroll | Self::Hover | Self::GotPointerCapture | Self::LostPointerCapture
        )
    }

    pub(crate) const fn bit(self) -> u64 {
        1 << self as u8
    }
}

/// Who a pointer is (HTML's `pointerType`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PointerType {
    Mouse,
    Pen,
    Touch,
}

/// HTML's `PointerEvent`: what `pointerdown`, `pointermove`, `pointerup`, `pointerenter`, `pointerleave`,
/// `pointerover`, `pointerout`, `pointercancel` and the capture events carry. The mouse is pointer 1.
#[derive(Clone, Debug)]
pub struct PointerEvent {
    pub pointer_id: i32,
    pub pointer_type: PointerType,
    pub is_primary: bool,
    /// The button that changed (`None` for moves and the like).
    pub button: Option<MouseButton>,
    /// The buttons held, as the DOM's `buttons` bits: 1 left, 2 right, 4 middle.
    pub buttons: u8,
    pub position: Point<Pixels>,
    pub modifiers: Modifiers,
    /// 0.5 while a button is held, else 0 (a mouse has no pressure of its own).
    pub pressure: f32,
    pub click_count: usize,
}

impl PointerEvent {
    pub(crate) fn mouse(position: Point<Pixels>, button: Option<MouseButton>, held: Option<MouseButton>,
                        modifiers: Modifiers, click_count: usize) -> Self {
        let bits = |b: MouseButton| match b {
            MouseButton::Left => 1,
            MouseButton::Right => 2,
            MouseButton::Middle => 4,
        };
        Self {
            pointer_id: 1,
            pointer_type: PointerType::Mouse,
            is_primary: true,
            button,
            buttons: held.map_or(0, bits),
            position,
            modifiers,
            pressure: if held.is_some() { 0.5 } else { 0.0 },
            click_count,
        }
    }
}

/// `contextmenu`: the secondary button (or the menu key) asked for a context menu.
#[derive(Clone, Debug)]
pub struct ContextMenuEvent {
    pub position: Point<Pixels>,
    pub modifiers: Modifiers,
}

/// `focus`, `blur`, `focusin` and `focusout`.
#[derive(Clone, Copy, Debug, Default)]
pub struct FocusEvent;

/// `scroll`: a scroll container moved.
#[derive(Clone, Debug)]
pub struct ScrollEvent {
    /// Where its content is scrolled to (from the top left).
    pub offset: Point<Pixels>,
}
