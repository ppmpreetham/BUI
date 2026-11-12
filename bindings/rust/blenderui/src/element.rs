//! Elements: one [`Div`] type for every HTML tag, built gpui-style.

use std::borrow::Cow;
use std::sync::Arc;

use crate::css::Decl;
pub use crate::styled::Styled;
use crate::events::*;
use crate::window::{App, Window};

/// Shared, cheap-to-clone text (gpui's `SharedString`).
pub type SharedString = Cow<'static, str>;

/// An element identifier (gpui's `ElementId`): keeps widget and scroll
/// state across renders.
pub type ElementId = SharedString;

/// One listener: the event it hears, in which pass, and whether it hears the whole window. Owned by its
/// element (moved into the frame, never shared); the closure takes any event type and does nothing
/// with the others, so one list holds every kind of listener.
pub(crate) struct Entry {
    pub kind: EventKind,
    /// Heard on the way down (the capture pass) rather than on the way up.
    pub capture: bool,
    /// Hears every event of its kind in the window, wherever it happens (gpui's `on_mouse_event`).
    pub wide: bool,
    pub call: EntryFn,
}

/// What an [`Entry`] calls: the event (of any type: the closure picks its own), the pass, the window and the app.
pub(crate) type EntryFn = Box<dyn Fn(&dyn std::any::Any, DispatchPhase, &mut Window, &mut App)>;

/// The listeners of an element, in the order they were added.
#[derive(Default)]
pub(crate) struct Listeners {
    pub entries: Vec<Entry>,
    /// The kinds among `entries` (bits of `EventKind::bit`): a window skips the walk for the rest.
    pub kinds: u64,
}

/// Work an element postpones until the window can lend itself: it receives
/// the window and app and finishes the element (children, styles) in place.
pub(crate) type Deferred = Box<dyn FnOnce(&mut Window, &mut App, &mut Div)>;

thread_local! {
    /// Deferred elements built since the last [`resolve_deferred`].
    static DEFERRED: std::cell::Cell<usize> = const { std::cell::Cell::new(0) };
}

/// Finish every deferred element of `el` (parents first, so what a deferred
/// element adds is resolved too). Costs nothing when none were built.
pub(crate) fn resolve_deferred(el: &mut AnyElement, window: &mut Window, app: &mut App) {
    if DEFERRED.replace(0) == 0 {
        return;
    }
    fn walk(el: &mut AnyElement, window: &mut Window, app: &mut App) {
        if let AnyElement::Element(d) = el {
            if let Some(f) = d.rare.as_mut().and_then(|r| r.deferred.take()) {
                f(window, app, d);
            }
            for c in &mut d.children {
                walk(c, window, app);
            }
        }
    }
    walk(el, window, app);
}

/// Custom painting (gpui's `canvas()` element).
pub(crate) type PaintFn = Box<dyn Fn(crate::geometry::Bounds<f32>, &mut crate::paint::Painter, &App)>;

/// The window-wide listeners' event types: which kind each one is.
pub trait WindowEvent: 'static {
    const KIND: EventKind;
}
impl WindowEvent for MouseDownEvent {
    const KIND: EventKind = EventKind::MouseDown;
}
impl WindowEvent for MouseUpEvent {
    const KIND: EventKind = EventKind::MouseUp;
}
impl WindowEvent for MouseMoveEvent {
    const KIND: EventKind = EventKind::MouseMove;
}
impl WindowEvent for ScrollWheelEvent {
    const KIND: EventKind = EventKind::Wheel;
}
impl WindowEvent for KeyDownEvent {
    const KIND: EventKind = EventKind::KeyDown;
}
impl WindowEvent for KeyUpEvent {
    const KIND: EventKind = EventKind::KeyUp;
}

/// `on_x` and `on_x_capture` for each `name, capture_name => Kind, EventType` line.
macro_rules! listeners {
    ($( $(#[$doc:meta])* $name:ident, $capture:ident => $kind:ident, $event:ty; )*) => {$(
        $(#[$doc])*
        pub fn $name(self, f: impl Fn(&$event, &mut Window, &mut App) + 'static) -> Self {
            self.listen(EventKind::$kind, false, f)
        }
        $(#[$doc])*
        /// (The capture pass: from the root down to the target.)
        pub fn $capture(self, f: impl Fn(&$event, &mut Window, &mut App) + 'static) -> Self {
            self.listen(EventKind::$kind, true, f)
        }
    )*};
}

/// An element: a tag, its attributes, styles, children and listeners.
/// `div()`, `span()`, `button()` ... all build one of these.
pub struct Div {
    pub(crate) tag: &'static str,
    pub(crate) id: Option<ElementId>,
    pub(crate) attrs: Vec<(SharedString, SharedString)>,
    pub(crate) decls: Vec<Decl>,
    /// `:hover` / `:active` / `:focus` styles, allocated only when used.
    pub(crate) states: Option<Box<StateDecls>>,
    pub(crate) children: Vec<AnyElement>,
    /// Allocated only when the element has a listener.
    pub(crate) listeners: Option<Box<Listeners>>,
    /// Allocated only when used: builder calls move the element, so it stays small.
    pub(crate) rare: Option<Box<Rare>>,
}

/// What a replaced element shows when the code gives it (else its `src`): one of these.
pub(crate) enum ReplacedSource {
    Painted(PaintFn),
    Image(Arc<crate::paint::Image>),
    Video(std::rc::Rc<crate::video::Video>),
}

/// The parts few elements use.
#[derive(Default)]
pub(crate) struct Rare {
    pub source: Option<ReplacedSource>,
    /// Runs before layout with the window and app (see [`Div::defer`]).
    pub deferred: Option<Deferred>,
    /// Motion's props (`initial`, `animate`, `whileHover` ...).
    pub motion: crate::motion::Spec,
}

/// Styles for interaction states (`.hover(|s| ...)` ...).
#[derive(Default)]
pub(crate) struct StateDecls {
    pub hover: Vec<Decl>,
    pub active: Vec<Decl>,
    pub focus: Vec<Decl>,
}

/// Anything that can be a child: an element or a text run. Elements are
/// stored inline (no box per element).
pub enum AnyElement {
    Element(Div),
    Text(SharedString),
}

/// Convert into an element (gpui's `IntoElement`).
pub trait IntoElement {
    fn into_any_element(self) -> AnyElement;
}

impl IntoElement for Div {
    fn into_any_element(self) -> AnyElement {
        AnyElement::Element(self)
    }
}
impl IntoElement for AnyElement {
    fn into_any_element(self) -> AnyElement {
        self
    }
}
impl IntoElement for &'static str {
    fn into_any_element(self) -> AnyElement {
        AnyElement::Text(Cow::Borrowed(self))
    }
}
impl IntoElement for String {
    fn into_any_element(self) -> AnyElement {
        AnyElement::Text(Cow::Owned(self))
    }
}
impl IntoElement for Cow<'static, str> {
    fn into_any_element(self) -> AnyElement {
        AnyElement::Text(self)
    }
}

impl Div {
    /// An element with any tag name (custom elements: `Div::new("my-widget")`).
    pub fn new(tag: &'static str) -> Div {
        Div {
            tag,
            id: None,
            attrs: Vec::new(),
            decls: Vec::new(),
            states: None,
            children: Vec::new(),
            listeners: None,
            rare: None,
        }
    }

    pub(crate) fn rare_mut(&mut self) -> &mut Rare {
        self.rare.get_or_insert_with(Box::default)
    }

    /// Motion's props, when it has any.
    pub(crate) fn motion(&self) -> Option<&crate::motion::Spec> {
        self.rare.as_ref().map(|r| &r.motion).filter(|m| !m.is_empty())
    }

    /// Set what this replaced element shows (the last source given wins).
    fn source(mut self, source: Option<ReplacedSource>) -> Self {
        self.rare_mut().source = source;
        self
    }

    /// Finish this element later, when the window is at hand: `f` runs
    /// once, after the view returned and before layout, and may add
    /// children and styles. Virtualized elements ([`uniform_list`], the data
    /// tables) use it to build only what their last-known size and scroll
    /// offset show.
    ///
    /// [`uniform_list`]: crate::uniform_list
    pub(crate) fn defer(mut self, f: impl FnOnce(&mut Window, &mut App, &mut Div) + 'static) -> Self {
        self.rare_mut().deferred = Some(Box::new(f));
        DEFERRED.set(DEFERRED.get() + 1);
        self
    }

    fn listeners_mut(&mut self) -> &mut Listeners {
        self.listeners.get_or_insert_with(Default::default)
    }

    fn states_mut(&mut self) -> &mut StateDecls {
        self.states.get_or_insert_with(Default::default)
    }

    /// The HTML tag name.
    pub fn tag(&self) -> &'static str {
        self.tag
    }

    /// Give the element an identity (keeps state such as scroll position and
    /// form values across renders).
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        let id = id.into();
        self.attrs.push(("id".into(), id.clone()));
        self.id = Some(id);
        self
    }

    /// Identity without an `id` attribute: keeps state (scroll position,
    /// form values) across renders, invisible to CSS (`#id` doesn't match).
    pub fn key(mut self, key: impl Into<ElementId>) -> Self {
        self.id = Some(key.into());
        self
    }

    /// Set an HTML attribute (`href`, `type`, `value`, `placeholder` ...).
    pub fn attr(mut self, name: impl Into<SharedString>, value: impl Into<SharedString>) -> Self {
        let name = name.into();
        self.attrs.retain(|(n, _)| *n != name);
        self.attrs.push((name, value.into()));
        self
    }

    /// Give this element and everything inside it Blender's look (dark
    /// theme, UI font size, themed links, code, tables ...). Without it,
    /// elements get the browsers' defaults: black text on white, with
    /// browser-look (Chrome-style) form controls; inside `styled`, form
    /// controls are Blender's widgets. The Blender components
    /// (`NumberSlider()`, `Button()` ...) are Blender's widgets anywhere.
    /// `<div styled>` in `rsx!`.
    pub fn styled(self) -> Self {
        self.attr("styled", "")
    }

    /// Add CSS classes (matched by stylesheets).
    pub fn class(mut self, classes: impl Into<SharedString>) -> Self {
        let add = classes.into();
        match self.attrs.iter_mut().find(|(n, _)| n == "class") {
            Some((_, v)) => *v = format!("{v} {add}").into(),
            None => self.attrs.push(("class".into(), add)),
        }
        self
    }

    pub(crate) fn get_attr(&self, name: &str) -> Option<&str> {
        self.attrs.iter().rev().find(|(n, _)| n == name).map(|(_, v)| v.as_ref())
    }

    // -- common HTML attributes, typed ---------------------------------------------
    pub fn href(self, url: impl Into<SharedString>) -> Self {
        self.attr("href", url)
    }
    pub fn value(self, v: impl Into<SharedString>) -> Self {
        self.attr("value", v)
    }
    pub fn placeholder(self, v: impl Into<SharedString>) -> Self {
        self.attr("placeholder", v)
    }
    pub fn input_type(self, t: impl Into<SharedString>) -> Self {
        self.attr("type", t)
    }
    pub fn name(self, n: impl Into<SharedString>) -> Self {
        self.attr("name", n)
    }
    pub fn checked(self, on: bool) -> Self {
        if on { self.attr("checked", "") } else { self.remove_attr("checked") }
    }
    pub fn disabled(self, on: bool) -> Self {
        if on { self.attr("disabled", "") } else { self.remove_attr("disabled") }
    }
    pub fn selected(self, on: bool) -> Self {
        if on { self.attr("selected", "") } else { self.remove_attr("selected") }
    }
    pub fn open(self, on: bool) -> Self {
        if on { self.attr("open", "") } else { self.remove_attr("open") }
    }
    pub fn min(self, v: f64) -> Self {
        self.attr("min", v.to_string())
    }
    pub fn max(self, v: f64) -> Self {
        self.attr("max", v.to_string())
    }
    pub fn step(self, v: f64) -> Self {
        self.attr("step", v.to_string())
    }
    pub fn src(self, path: impl Into<SharedString>) -> Self {
        self.attr("src", path)
    }
    pub fn alt(self, text: impl Into<SharedString>) -> Self {
        self.attr("alt", text)
    }
    pub fn title(self, text: impl Into<SharedString>) -> Self {
        self.attr("title", text)
    }
    pub fn colspan(self, n: u32) -> Self {
        self.attr("colspan", n.to_string())
    }

    fn remove_attr(mut self, name: &str) -> Self {
        self.attrs.retain(|(n, _)| n != name);
        self
    }

    /// Image pixels for `<img>`: straight RGBA, `width * height * 4` bytes
    /// (copied; to show the same pixels every frame, make an `Image` once and
    /// pass it to `image`).
    pub fn image_rgba(self, width: u32, height: u32, rgba: impl AsRef<[u8]>) -> Self {
        let image = crate::paint::Image::from_rgba(width, height, rgba.as_ref());
        self.source(image.map(|i| ReplacedSource::Image(Arc::new(i))))
    }

    /// Show shared image pixels (cheap to pass every frame).
    pub fn image(self, image: Arc<crate::paint::Image>) -> Self {
        self.source(Some(ReplacedSource::Image(image)))
    }

    /// `<video>` with a player you keep (play, pause, seek, events): cheap
    /// to pass every frame. Without it, `<video src>` makes its own.
    pub fn video(self, video: std::rc::Rc<crate::video::Video>) -> Self {
        self.source(Some(ReplacedSource::Video(video)))
    }

    // -- events ----------------------------------------------------------------------------
    //
    // HTML's `on*` events, one method each (`onClick` is `.on_click`, in `rsx!` too), and `_capture` for
    // the pass that runs from the root down to the target before the usual one back up (`onClickCapture`).
    // A listener gets the event, the window and the app; `window.stop_propagation()` ends the pass.

    fn listen<E: 'static>(mut self, kind: EventKind, capture: bool,
                          f: impl Fn(&E, &mut Window, &mut App) + 'static) -> Self {
        self.push_entry(Entry {
            kind,
            capture,
            wide: false,
            call: Box::new(move |e, _, w, a| {
                if let Some(e) = e.downcast_ref::<E>() {
                    f(e, w, a);
                }
            }),
        });
        self
    }

    fn push_entry(&mut self, entry: Entry) {
        let ls = self.listeners_mut();
        ls.kinds |= entry.kind.bit();
        ls.entries.push(entry);
    }

    /// Hears every event of its kind in the window, whatever element it happens on, in both passes: a
    /// drag that leaves its element keeps hearing the moves and the release (gpui's `on_mouse_event`).
    /// `E` is the event the listener takes: `MouseMoveEvent`, `MouseDownEvent`, `MouseUpEvent`,
    /// `ScrollWheelEvent`, `KeyDownEvent` or `KeyUpEvent`.
    pub fn on_mouse_event<E: WindowEvent>(self, f: impl Fn(&E, DispatchPhase, &mut Window, &mut App) + 'static) -> Self {
        self.on_window_event(E::KIND, f)
    }

    /// [`on_mouse_event`](Self::on_mouse_event) for the keyboard.
    pub fn on_key_event<E: WindowEvent>(self, f: impl Fn(&E, DispatchPhase, &mut Window, &mut App) + 'static) -> Self {
        self.on_window_event(E::KIND, f)
    }

    /// The window-wide listener of any event kind and type (the pointer events take this form).
    pub fn on_window_event<E: 'static>(mut self, kind: EventKind,
                                       f: impl Fn(&E, DispatchPhase, &mut Window, &mut App) + 'static) -> Self {
        self.push_entry(Entry {
            kind,
            capture: false,
            wide: true,
            call: Box::new(move |e, phase, w, a| {
                if let Some(e) = e.downcast_ref::<E>() {
                    f(e, phase, w, a);
                }
            }),
        });
        self
    }

    /// Press with `button` (and the capture pass of it).
    pub fn on_mouse_down(self, button: MouseButton,
                         f: impl Fn(&MouseDownEvent, &mut Window, &mut App) + 'static) -> Self {
        self.listen(EventKind::MouseDown, false, move |e: &MouseDownEvent, w, a| {
            if e.button == button {
                f(e, w, a);
            }
        })
    }
    pub fn on_mouse_down_capture(self, button: MouseButton,
                                 f: impl Fn(&MouseDownEvent, &mut Window, &mut App) + 'static) -> Self {
        self.listen(EventKind::MouseDown, true, move |e: &MouseDownEvent, w, a| {
            if e.button == button {
                f(e, w, a);
            }
        })
    }
    pub fn on_mouse_up(self, button: MouseButton,
                       f: impl Fn(&MouseUpEvent, &mut Window, &mut App) + 'static) -> Self {
        self.listen(EventKind::MouseUp, false, move |e: &MouseUpEvent, w, a| {
            if e.button == button {
                f(e, w, a);
            }
        })
    }
    pub fn on_mouse_up_capture(self, button: MouseButton,
                               f: impl Fn(&MouseUpEvent, &mut Window, &mut App) + 'static) -> Self {
        self.listen(EventKind::MouseUp, true, move |e: &MouseUpEvent, w, a| {
            if e.button == button {
                f(e, w, a);
            }
        })
    }
    /// Press with any button (`mousedown`).
    pub fn on_any_mouse_down(self, f: impl Fn(&MouseDownEvent, &mut Window, &mut App) + 'static) -> Self {
        self.listen(EventKind::MouseDown, false, f)
    }

    /// Called with `true` when the pointer enters, `false` when it leaves (does not bubble).
    pub fn on_hover(self, f: impl Fn(&bool, &mut Window, &mut App) + 'static) -> Self {
        self.listen(EventKind::Hover, false, f)
    }

    listeners! {
        /// `click`: press and release on this element; bubbles.
        on_click, on_click_capture => Click, ClickEvent;
        /// `dblclick`
        on_double_click, on_double_click_capture => DoubleClick, ClickEvent;
        /// `auxclick`: a click with a button other than the main one.
        on_aux_click, on_aux_click_capture => AuxClick, ClickEvent;
        /// `contextmenu`
        on_context_menu, on_context_menu_capture => ContextMenu, ContextMenuEvent;
        /// `mousemove`
        on_mouse_move, on_mouse_move_capture => MouseMove, MouseMoveEvent;
        /// `mouseenter`: this element only (no bubbling, and its children don't count as leaving).
        on_mouse_enter, on_mouse_enter_capture => MouseEnter, MouseMoveEvent;
        /// `mouseleave`
        on_mouse_leave, on_mouse_leave_capture => MouseLeave, MouseMoveEvent;
        /// `mouseover`: bubbles, also from the children.
        on_mouse_over, on_mouse_over_capture => MouseOver, MouseMoveEvent;
        /// `mouseout`
        on_mouse_out, on_mouse_out_capture => MouseOut, MouseMoveEvent;
        /// `wheel`
        on_scroll_wheel, on_scroll_wheel_capture => Wheel, ScrollWheelEvent;
        /// `wheel` (HTML's name for `on_scroll_wheel`)
        on_wheel, on_wheel_capture => Wheel, ScrollWheelEvent;
        /// `pointerdown`
        on_pointer_down, on_pointer_down_capture => PointerDown, PointerEvent;
        /// `pointerup`
        on_pointer_up, on_pointer_up_capture => PointerUp, PointerEvent;
        /// `pointermove`
        on_pointer_move, on_pointer_move_capture => PointerMove, PointerEvent;
        /// `pointerenter` (no bubbling)
        on_pointer_enter, on_pointer_enter_capture => PointerEnter, PointerEvent;
        /// `pointerleave`
        on_pointer_leave, on_pointer_leave_capture => PointerLeave, PointerEvent;
        /// `pointerover`
        on_pointer_over, on_pointer_over_capture => PointerOver, PointerEvent;
        /// `pointerout`
        on_pointer_out, on_pointer_out_capture => PointerOut, PointerEvent;
        /// `pointercancel`
        on_pointer_cancel, on_pointer_cancel_capture => PointerCancel, PointerEvent;
        /// `gotpointercapture`: `Window::set_pointer_capture` took the pointer for this element.
        on_got_pointer_capture, on_got_pointer_capture_capture => GotPointerCapture, PointerEvent;
        /// `lostpointercapture`
        on_lost_pointer_capture, on_lost_pointer_capture_capture => LostPointerCapture, PointerEvent;
        /// `keydown`: goes to the focused element.
        on_key_down, on_key_down_capture => KeyDown, KeyDownEvent;
        /// `keyup`
        on_key_up, on_key_up_capture => KeyUp, KeyUpEvent;
        /// `focus` (no bubbling)
        on_focus, on_focus_capture => Focus, FocusEvent;
        /// `blur` (no bubbling)
        on_blur, on_blur_capture => Blur, FocusEvent;
        /// `focusin`: like `focus`, and it bubbles.
        on_focus_in, on_focus_in_capture => FocusIn, FocusEvent;
        /// `focusout`
        on_focus_out, on_focus_out_capture => FocusOut, FocusEvent;
        /// Blender's press-drag: a button is held and the pointer moved past the drag threshold (once per press).
        on_press_drag, on_press_drag_capture => PressDrag, PressDragEvent;
        /// `scroll`: this element's scroll position changed (no bubbling).
        on_scroll, on_scroll_capture => Scroll, ScrollEvent;
        /// Form controls: the committed value changed (`change` in HTML).
        on_change, on_change_capture => Change, ChangeEvent;
        /// Form controls: every edit (`input` in HTML).
        on_input, on_input_capture => Input, ChangeEvent;
    }

    // -- state styles (gpui's .hover(|s| ...), CSS :hover / :active / :focus) ----------------
    pub fn hover(mut self, f: impl FnOnce(StyleRefinement) -> StyleRefinement) -> Self {
        self.states_mut().hover.extend(f(StyleRefinement::default()).decls);
        self
    }
    pub fn active(mut self, f: impl FnOnce(StyleRefinement) -> StyleRefinement) -> Self {
        self.states_mut().active.extend(f(StyleRefinement::default()).decls);
        self
    }
    pub fn focus(mut self, f: impl FnOnce(StyleRefinement) -> StyleRefinement) -> Self {
        self.states_mut().focus.extend(f(StyleRefinement::default()).decls);
        self
    }
}

/// A set of style declarations (the argument of `.hover(|s| ...)`).
#[derive(Clone, Default)]
pub struct StyleRefinement {
    pub(crate) decls: Vec<Decl>,
}

impl Styled for Div {
    fn decls_mut(&mut self) -> &mut Vec<Decl> {
        // one allocation for a typical element's handful of style calls
        if self.decls.capacity() == 0 {
            self.decls.reserve(8);
        }
        &mut self.decls
    }
}
impl Styled for StyleRefinement {
    fn decls_mut(&mut self) -> &mut Vec<Decl> {
        &mut self.decls
    }
}

/// Children (gpui's `ParentElement`).
pub trait ParentElement: Sized {
    fn children_mut(&mut self) -> &mut Vec<AnyElement>;

    fn child(mut self, child: impl IntoElement) -> Self {
        self.children_mut().push(child.into_any_element());
        self
    }

    fn children(mut self, children: impl IntoIterator<Item = impl IntoElement>) -> Self {
        self.children_mut().extend(children.into_iter().map(IntoElement::into_any_element));
        self
    }
}

impl ParentElement for Div {
    fn children_mut(&mut self) -> &mut Vec<AnyElement> {
        &mut self.children
    }
}

/// Conditional building (gpui's `FluentBuilder`).
pub trait FluentBuilder: Sized {
    fn when(self, condition: bool, then: impl FnOnce(Self) -> Self) -> Self {
        if condition { then(self) } else { self }
    }
    fn when_some<T>(self, option: Option<T>, then: impl FnOnce(Self, T) -> Self) -> Self {
        match option {
            Some(v) => then(self, v),
            None => self,
        }
    }
    fn when_none<T>(self, option: &Option<T>, then: impl FnOnce(Self) -> Self) -> Self {
        if option.is_none() { then(self) } else { self }
    }
    fn map<U>(self, f: impl FnOnce(Self) -> U) -> U {
        f(self)
    }
}

impl FluentBuilder for Div {}
impl FluentBuilder for StyleRefinement {}

/// gpui's `canvas(prepaint, paint)`: an element that paints itself.
pub fn canvas(paint: impl Fn(crate::geometry::Bounds<f32>, &mut crate::paint::Painter) + 'static)
    -> Div {
    Div::new("canvas").source(Some(ReplacedSource::Painted(Box::new(move |b, p, _| paint(b, p)))))
}

/// Like [`canvas`], and the paint callback also reads the app: a view over
/// an entity paints straight from it (`entity.read(cx)`) with nothing copied
/// into the element tree. The data tables draw this way.
pub fn canvas_with(paint: impl Fn(crate::geometry::Bounds<f32>, &mut crate::paint::Painter, &App) + 'static)
    -> Div {
    Div::new("canvas").source(Some(ReplacedSource::Painted(Box::new(paint))))
}
