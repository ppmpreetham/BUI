//! Blender's screen: areas the user splits, joins, docks, swaps and resizes, and the regions
//! (header, toolbar, sidebar ...) inside them, from the kit (`kit/intern/screen`).
//!
//! ```ignore
//! let editors = Editors::new()
//!     .editor("3D Viewport", "VIEW3D", |_area: &AreaRef| {
//!         Editor::new(viewport())
//!             .header(div().child("View"))
//!             .sidebar(Region::new(panels()).size(280).hotkey("n"))
//!     })
//!     .editor("Properties", "PROPERTIES", |_| Editor::new(properties()));
//! rsx! {
//!     <Screen editors={editors} layout={AreaSplit::v(0.75, "3D Viewport", "Properties")}
//!             saved={saved.as_deref()} onChange={|e: &ScreenEvent| save(e.layout())} />
//! }
//! // gpui style
//! Screen().editors(editors).layout(AreaSplit::v(0.75, "3D Viewport", "Properties"))
//! ```
//!
//! The kit owns where everything is: a transparent `screen` node over the window's canvas
//! draws the edges, the corner handles, the highlights and previews, and takes the pointer
//! on edges and action zones only. Each area's regions and main view are HTML elements laid
//! out at the rects the kit gives them (one render behind the kit, which reports every
//! move: a drag re-renders as it goes).
//!
//! The same elements carry JavaScript's screens (`@blender-ui/react` and friends): a
//! `<bui-screen>` whose `<bui-area>` children (in the kit's area order) hold `<bui-region
//! region="sidebar" size="280" ...>` and one `<bui-main>`. Their `change` events: the screen's
//! value is the layout (JSON), an area's `"window\t<editor>"` (dragged out of the window), a
//! region's number its size and `checked` whether it is hidden.

use std::borrow::Cow;
use std::collections::{HashMap, HashSet};
use std::ffi::{CStr, CString};
use std::fmt::Write as _;

use crate::color::Rgba;
use crate::element::{AnyElement, Div, IntoElement, ParentElement, SharedString};
use crate::events::ChangeEvent;
use crate::geometry::px;
use crate::layout::{id_key, Frame, Key};
use crate::styled::Styled;
use crate::window::{App, Window};
use blenderui_sys as sys;

// ==========================================================================
// The public API

/// The direction of the edge a split makes: [`Axis::V`] puts the areas side by side.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Axis {
    /// A horizontal edge: one area above the other.
    H,
    /// A vertical edge: the areas side by side.
    V,
}

/// Where [`AreaRef::dock`] puts an area: beside the target, or in its place.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Dock {
    Left,
    Right,
    Top,
    Bottom,
    Center,
}

/// The side of the area a region is on.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Side {
    Left,
    Right,
    Top,
    Bottom,
}

impl Side {
    const fn name(self) -> &'static str {
        match self {
            Side::Left => "left",
            Side::Right => "right",
            Side::Top => "top",
            Side::Bottom => "bottom",
        }
    }
}

/// A screen's first arrangement: an editor's name, or two arrangements split by an edge.
#[derive(Clone, Debug, PartialEq)]
pub enum AreaSplit {
    Editor(SharedString),
    Split { axis: Axis, factor: f32, first: Box<AreaSplit>, second: Box<AreaSplit> },
}

impl AreaSplit {
    /// `first` on the left taking `factor` of the width, `second` on the right.
    pub fn v(factor: f32, first: impl Into<AreaSplit>, second: impl Into<AreaSplit>) -> AreaSplit {
        AreaSplit::Split { axis: Axis::V, factor, first: Box::new(first.into()), second: Box::new(second.into()) }
    }

    /// `first` on top taking `factor` of the height, `second` below.
    pub fn h(factor: f32, first: impl Into<AreaSplit>, second: impl Into<AreaSplit>) -> AreaSplit {
        AreaSplit::Split { axis: Axis::H, factor, first: Box::new(first.into()), second: Box::new(second.into()) }
    }

    /// The text form `<bui-screen layout="...">` takes: `v0.75("3D Viewport",h0.4("A","B"))`.
    pub fn to_text(&self) -> String {
        let mut out = String::new();
        self.write(&mut out);
        out
    }

    fn write(&self, out: &mut String) {
        match self {
            AreaSplit::Editor(name) => {
                out.push('"');
                for c in name.chars() {
                    if c == '"' || c == '\\' {
                        out.push('\\');
                    }
                    out.push(c);
                }
                out.push('"');
            }
            AreaSplit::Split { axis, factor, first, second } => {
                let _ = write!(out, "{}{factor}(", if *axis == Axis::V { 'v' } else { 'h' });
                first.write(out);
                out.push(',');
                second.write(out);
                out.push(')');
            }
        }
    }

    /// Reads [`to_text`](Self::to_text)'s form; `None` when malformed (a factor outside 0..1 too).
    pub fn parse(text: &str) -> Option<AreaSplit> {
        let (split, rest) = Self::parse_at(text.trim_start())?;
        rest.trim().is_empty().then_some(split)
    }

    fn parse_at(s: &str) -> Option<(AreaSplit, &str)> {
        if let Some(body) = s.strip_prefix('"') {
            let mut name = String::new();
            let mut chars = body.char_indices();
            while let Some((i, c)) = chars.next() {
                match c {
                    '\\' => name.push(chars.next()?.1),
                    '"' => return Some((AreaSplit::Editor(name.into()), &body[i + 1..])),
                    _ => name.push(c),
                }
            }
            return None;
        }
        let axis = match s.as_bytes().first()? {
            b'v' => Axis::V,
            b'h' => Axis::H,
            _ => return None,
        };
        let open = s.find('(')?;
        let factor: f32 = s[1..open].trim().parse().ok()?;
        if !(factor > 0.0 && factor < 1.0) {
            return None;
        }
        let (first, rest) = Self::parse_at(s[open + 1..].trim_start())?;
        let rest = rest.trim_start().strip_prefix(',')?;
        let (second, rest) = Self::parse_at(rest.trim_start())?;
        let rest = rest.trim_start().strip_prefix(')')?;
        Some((AreaSplit::Split { axis, factor, first: Box::new(first), second: Box::new(second) }, rest))
    }
}

impl From<&'static str> for AreaSplit {
    fn from(name: &'static str) -> Self {
        AreaSplit::Editor(Cow::Borrowed(name))
    }
}

impl From<String> for AreaSplit {
    fn from(name: String) -> Self {
        AreaSplit::Editor(Cow::Owned(name))
    }
}

/// A region the user resized, hid or showed.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct RegionEvent {
    /// Unscaled pixels across the area (a sidebar's width, an asset shelf's height).
    pub size: u16,
    pub hidden: bool,
}

/// The screen's layout changed (the user moved an edge, split, joined, docked ...).
#[derive(Debug)]
pub struct ScreenEvent {
    layout: String,
}

impl ScreenEvent {
    /// The arrangement as text: give it back as `saved` to restore it.
    pub fn layout(&self) -> &str {
        &self.layout
    }
}

/// The user dragged an area out of the window, or chose Duplicate Area into New Window.
#[derive(Debug)]
pub struct AreaEvent {
    editor: String,
}

impl AreaEvent {
    /// The editor the area showed: open a window showing it.
    pub fn editor(&self) -> &str {
        &self.editor
    }
}

type RegionListener = Box<dyn Fn(&RegionEvent, &mut Window, &mut App)>;

/// An element placed as one of an area's regions, with its size, side and hotkey.
pub struct Region {
    content: AnyElement,
    size: Option<u16>,
    hidden: Option<bool>,
    side: Option<Side>,
    overlap: Option<bool>,
    resizable: Option<bool>,
    maxsize: Option<u16>,
    hotkey: Option<SharedString>,
    on_resize: Option<RegionListener>,
}

impl Region {
    pub fn new(content: impl IntoElement) -> Region {
        Region { content: content.into_any_element(), size: None, hidden: None, side: None, overlap: None,
                 resizable: None, maxsize: None, hotkey: None, on_resize: None }
    }

    /// Unscaled pixels across the area.
    #[must_use]
    pub fn size(mut self, size: u16) -> Self {
        self.size = Some(size);
        self
    }

    #[must_use]
    pub fn hidden(mut self, hidden: bool) -> Self {
        self.hidden = Some(hidden);
        self
    }

    /// The side it is on (the sides its kind allows: left/right or top/bottom).
    #[must_use]
    pub fn side(mut self, side: Side) -> Self {
        self.side = Some(side);
        self
    }

    /// `false`: its own space instead of floating over the main view.
    #[must_use]
    pub fn overlap(mut self, overlap: bool) -> Self {
        self.overlap = Some(overlap);
        self
    }

    #[must_use]
    pub fn resizable(mut self, resizable: bool) -> Self {
        self.resizable = Some(resizable);
        self
    }

    #[must_use]
    pub fn maxsize(mut self, maxsize: u16) -> Self {
        self.maxsize = Some(maxsize);
        self
    }

    /// Toggles it while the pointer is over the area: `"n"`, `"ctrl+]"`.
    #[must_use]
    pub fn hotkey(mut self, hotkey: impl Into<SharedString>) -> Self {
        self.hotkey = Some(hotkey.into());
        self
    }

    /// The user resized, hid or showed it.
    #[must_use]
    pub fn on_resize(mut self, f: impl Fn(&RegionEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_resize = Some(Box::new(f));
        self
    }

    fn into_element(self, kind: &'static str) -> Div {
        let mut d = Div::new("bui-region").attr("region", kind);
        let num = |v: u16| Cow::Owned(v.to_string());
        let flag = |v: bool| Cow::Borrowed(if v { "1" } else { "0" });
        if let Some(v) = self.size {
            d = d.attr("size", num(v));
        }
        if let Some(v) = self.hidden {
            d = d.attr("hidden", flag(v));
        }
        if let Some(v) = self.side {
            d = d.attr("side", v.name());
        }
        if let Some(v) = self.overlap {
            d = d.attr("overlap", flag(v));
        }
        if let Some(v) = self.resizable {
            d = d.attr("resizable", flag(v));
        }
        if let Some(v) = self.maxsize {
            d = d.attr("maxsize", num(v));
        }
        if let Some(v) = self.hotkey {
            d = d.attr("hotkey", v);
        }
        if let Some(f) = self.on_resize {
            d = d.on_change(move |e, w, app| {
                f(&RegionEvent { size: e.number as u16, hidden: e.checked }, w, app);
                app.propagate = false;
            });
        }
        d.child(self.content)
    }
}

impl<T: IntoElement> From<T> for Region {
    fn from(content: T) -> Self {
        Region::new(content)
    }
}

/// The kinds of region, in Blender's names (one of each per area).
const KINDS: [&str; 10] =
    ["header", "tool_header", "footer", "asset_shelf", "channels", "nav_bar", "toolbar", "sidebar", "execute", "hud"];

/// What an editor shows: the main view and the regions around it (one slot per kind).
pub struct Editor {
    main: AnyElement,
    regions: [Option<Region>; KINDS.len()],
}

macro_rules! region_slots {
    ($($(#[$m:meta])* $name:ident = $i:expr;)*) => {$(
        $(#[$m])*
        #[must_use]
        pub fn $name(mut self, region: impl Into<Region>) -> Self {
            self.regions[$i] = Some(region.into());
            self
        }
    )*};
}

impl Editor {
    pub fn new(main: impl IntoElement) -> Editor {
        Editor { main: main.into_any_element(), regions: Default::default() }
    }

    region_slots! {
        /// The header (on top, or flipped to the bottom).
        header = 0;
        tool_header = 1;
        footer = 2;
        asset_shelf = 3;
        channels = 4;
        nav_bar = 5;
        /// The tool shelf (on the left; its width snaps to columns).
        toolbar = 6;
        /// The sidebar with category tabs (on the right).
        sidebar = 7;
        execute = 8;
        /// The floating Adjust Last Operation panel.
        hud = 9;
    }

    fn into_area(self) -> Div {
        let mut d = Div::new("bui-area");
        for (i, r) in self.regions.into_iter().enumerate() {
            if let Some(r) = r {
                d = d.child(r.into_element(KINDS[i]));
            }
        }
        d.child(Div::new("bui-main").child(self.main))
    }
}

type Factory = Box<dyn Fn(&AreaRef) -> Editor>;

/// The editors a screen's areas show: each fills every area showing it, the ones the user makes
/// too (a split, a dock, a restored layout), and they list the editor-type menu.
#[derive(Default)]
pub struct Editors {
    list: Vec<(SharedString, SharedString, Factory)>,
}

impl Editors {
    pub fn new() -> Editors {
        Editors::default()
    }

    /// `make(area)` builds what an area showing `name` shows; `icon` is Blender's icon name.
    #[must_use]
    pub fn editor(mut self, name: impl Into<SharedString>, icon: impl Into<SharedString>,
                  make: impl Fn(&AreaRef) -> Editor + 'static) -> Self {
        self.list.push((name.into(), icon.into(), Box::new(make)));
        self
    }

    /// The editors' names and icons, in order (an editor-type menu).
    pub fn names(&self) -> impl Iterator<Item = (&str, &str)> {
        self.list.iter().map(|(n, i, _)| (&**n, &**i))
    }

    fn make(&self, area: &AreaRef) -> Editor {
        let name = area.editor();
        let entry = self.list.iter().find(|(n, _, _)| n == name).or_else(|| self.list.first());
        entry.map_or_else(|| Editor::new(""), |(_, _, f)| f(area))
    }
}

thread_local! {
    /// The kit areas alive now (an `AreaRef` checks before it acts): kept as the kit makes and
    /// frees them (`kit_event`, from the kit's callback, at once).
    static LIVE: std::cell::RefCell<HashSet<usize>> = std::cell::RefCell::new(HashSet::new());
    /// The editor of an area that asked for a window, read before the kit closed it.
    static WINDOWED: std::cell::RefCell<HashMap<usize, String>> = std::cell::RefCell::new(HashMap::new());
}

/// One of a screen's areas. Acting on an area that is gone does nothing.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AreaRef {
    node: *mut sys::BKNode,
}

impl AreaRef {
    fn live(self) -> bool {
        LIVE.with(|l| l.borrow().contains(&(self.node as usize)))
    }

    /// The editor it shows ("" when the area is gone).
    pub fn editor(&self) -> &str {
        if !self.live() {
            return "";
        }
        // SAFETY: a live area; the string is the node's and stays until its editor changes.
        unsafe { CStr::from_ptr(sys::BK_get_str(self.node, c"editor".as_ptr())) }.to_str().unwrap_or("")
    }

    /// A new area off this one (Blender's `area_split`): `factor` from the left (`V`) or the bottom
    /// (`H`); the new area is the side past 0.5. `None` when the area is too small.
    pub fn split(&self, axis: Axis, factor: f32) -> Option<AreaRef> {
        if !self.live() {
            return None;
        }
        let a = if axis == Axis::V { c_int_of(b'v') } else { c_int_of(b'h') };
        // SAFETY: a live area node.
        let n = unsafe { sys::BK_area_split(self.node, a, factor) };
        (!n.is_null()).then(|| {
            LIVE.with(|l| l.borrow_mut().insert(n as usize));
            AreaRef { node: n }
        })
    }

    /// `other` joins into this area (they share an edge).
    pub fn join(&self, other: &AreaRef) -> bool {
        // SAFETY: live area nodes.
        self.live() && other.live() && unsafe { sys::BK_area_join(self.node, other.node) } != 0
    }

    /// Moves this area beside `target` (taking `factor` of it) or into its place (`Dock::Center`).
    pub fn dock(&self, target: &AreaRef, where_: Dock, factor: f32) -> bool {
        let w = match where_ {
            Dock::Left => 0,
            Dock::Right => 1,
            Dock::Top => 2,
            Dock::Bottom => 3,
            Dock::Center => 4,
        };
        // SAFETY: live area nodes.
        self.live() && target.live() && unsafe { sys::BK_area_dock(self.node, target.node, w, factor) } != 0
    }

    pub fn swap(&self, other: &AreaRef) -> bool {
        // SAFETY: live area nodes.
        self.live() && other.live() && unsafe { sys::BK_area_swap(self.node, other.node) } != 0
    }

    /// A neighbor takes its place.
    pub fn close(&self) -> bool {
        // SAFETY: a live area node.
        self.live() && unsafe { sys::BK_area_close(self.node) } != 0
    }

    fn full(&self, state: &CStr) {
        if self.live() {
            // SAFETY: a live area node.
            unsafe { sys::BK_set_str(self.node, c"full".as_ptr(), state.as_ptr()) };
        }
    }

    /// Ctrl+Space: only this area.
    pub fn maximize(&self) {
        self.full(c"maximized");
    }

    /// Ctrl+Alt+Space: only this area, without its header and side regions.
    pub fn focus(&self) {
        self.full(c"focus");
    }

    pub fn restore(&self) {
        self.full(c"");
    }
}

fn c_int_of(b: u8) -> std::os::raw::c_int {
    std::os::raw::c_int::from(b)
}

type ScreenListener = Box<dyn Fn(&ScreenEvent, &mut Window, &mut App)>;
type AreaListener = Box<dyn Fn(&AreaEvent, &mut Window, &mut App)>;

/// `<Screen ... />` (see the module docs).
pub struct Screen {
    _private: (),
}

impl Screen {
    #[doc(hidden)]
    pub fn __builder() -> ScreenBuilder {
        ScreenBuilder::default()
    }
}

/// Areas the user rearranges as in Blender (see the module docs).
#[allow(non_snake_case)]
pub fn Screen() -> ScreenBuilder {
    ScreenBuilder::default()
}

/// A saved layout: `&str`, `String` or an `Option` of one (`None`: start from `layout`).
pub trait SavedLayout {
    fn into_saved(self) -> Option<String>;
}
impl SavedLayout for &str {
    fn into_saved(self) -> Option<String> {
        Some(self.to_owned())
    }
}
impl SavedLayout for String {
    fn into_saved(self) -> Option<String> {
        Some(self)
    }
}
impl SavedLayout for Option<&str> {
    fn into_saved(self) -> Option<String> {
        self.map(str::to_owned)
    }
}
impl SavedLayout for Option<String> {
    fn into_saved(self) -> Option<String> {
        self
    }
}

/// [`Screen`]'s props.
#[derive(Default)]
pub struct ScreenBuilder {
    id: Option<SharedString>,
    editors: Editors,
    layout: Option<AreaSplit>,
    saved: Option<String>,
    resize_only: bool,
    on_change: Option<ScreenListener>,
    on_window: Option<AreaListener>,
    class: Option<SharedString>,
    style: Option<SharedString>,
    attrs: Vec<(SharedString, SharedString)>,
}

impl ScreenBuilder {
    /// Its identity (two screens in a window need different ones).
    #[must_use]
    pub fn id(mut self, id: impl Into<SharedString>) -> Self {
        self.id = Some(id.into());
        self
    }

    #[must_use]
    pub fn editors(mut self, editors: Editors) -> Self {
        self.editors = editors;
        self
    }

    /// The first arrangement (used when there is no `saved` layout).
    #[must_use]
    pub fn layout(mut self, layout: impl Into<AreaSplit>) -> Self {
        self.layout = Some(layout.into());
        self
    }

    /// A layout from [`ScreenEvent::layout`] to restore.
    #[must_use]
    pub fn saved(mut self, saved: impl SavedLayout) -> Self {
        self.saved = saved.into_saved();
        self
    }

    /// `"resize"`: only the edges move; `"all"` (the default): split, join, dock ... too.
    #[must_use]
    pub fn ops(mut self, ops: &str) -> Self {
        self.resize_only = ops == "resize";
        self
    }

    /// Every change the user makes to the layout.
    #[must_use]
    pub fn on_change(mut self, f: impl Fn(&ScreenEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_change = Some(Box::new(f));
        self
    }

    /// The user dragged an area out of the window (or chose Duplicate Area into New Window).
    #[must_use]
    pub fn on_window(mut self, f: impl Fn(&AreaEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_window = Some(Box::new(f));
        self
    }

    #[must_use]
    pub fn class(mut self, class: impl Into<SharedString>) -> Self {
        self.class = Some(class.into());
        self
    }

    #[must_use]
    pub fn style(mut self, style: impl Into<SharedString>) -> Self {
        self.style = Some(style.into());
        self
    }

    /// An attribute of the screen's element (`rsx!` literals: `ops="resize"`, `data-*` ...).
    #[must_use]
    pub fn attr(mut self, name: impl Into<SharedString>, value: impl Into<SharedString>) -> Self {
        let (name, value) = (name.into(), value.into());
        if name == "ops" {
            self.resize_only = value == "resize";
        } else {
            self.attrs.push((name, value));
        }
        self
    }

    fn into_div(self) -> Div {
        let ScreenBuilder { id, editors, layout, saved, resize_only, on_change, on_window, class, style, attrs } = self;
        let mut d = Div::new("bui-screen").key(id.unwrap_or(Cow::Borrowed("bui-screen")));
        if let Some(c) = class {
            d = d.class(c);
        }
        if let Some(s) = style {
            d = d.style(&*s);
        }
        for (n, v) in attrs {
            d = d.attr(n, v);
        }
        if resize_only {
            d = d.attr("ops", "resize");
        }
        if on_change.is_some() || on_window.is_some() {
            d = d.on_change(move |e, w, app| {
                match (e.value.strip_prefix("window\t"), &on_window, &on_change) {
                    (Some(editor), Some(f), _) => f(&AreaEvent { editor: editor.to_owned() }, w, app),
                    (None, _, Some(f)) if e.value.starts_with('{') => {
                        f(&ScreenEvent { layout: e.value.clone() }, w, app);
                    }
                    _ => {}
                }
            });
        }
        d.defer(move |window, _app, div| {
            let spec = Spec { first: layout.as_ref(), saved: saved.as_deref(), resize_only, editors: Some(&editors) };
            window.sync_screen(div, &spec);
        })
    }
}

impl crate::hooks::Complete for ScreenBuilder {
    type Props = Div;
    fn finish(self) -> Div {
        self.into_div()
    }
}

impl IntoElement for ScreenBuilder {
    fn into_any_element(self) -> AnyElement {
        AnyElement::Element(self.into_div())
    }
}

/// A `<bui-screen>` built as plain elements (JavaScript's screens): its `layout`, `saved` and
/// `ops` attributes say how it starts, its `<bui-area>` children what the areas show.
#[doc(hidden)]
pub fn screen_element(d: Div) -> Div {
    d.defer(|window, _app, div| {
        let first = div.get_attr("layout").and_then(AreaSplit::parse);
        let saved = div.get_attr("saved").map(str::to_owned);
        let spec = Spec { first: first.as_ref(), saved: saved.as_deref(),
                          resize_only: div.get_attr("ops") == Some("resize"), editors: None };
        window.sync_screen(div, &spec);
    })
}

// ==========================================================================
// The kit side: one screen's nodes, synced from its elements each render

/// How a screen starts, and (Rust's screens) what fills its areas.
pub(crate) struct Spec<'a> {
    first: Option<&'a AreaSplit>,
    saved: Option<&'a str>,
    resize_only: bool,
    editors: Option<&'a Editors>,
}

/// A region's kit node and the attributes last given to it (only changes are applied, so the
/// user's drags stay until the app asks for something else).
struct RegionNode {
    node: *mut sys::BKNode,
    applied: [Option<String>; PROPS.len()],
}

const PROPS: [&str; 7] = ["size", "hidden", "side", "overlap", "resizable", "maxsize", "hotkey"];

struct AreaNode {
    node: *mut sys::BKNode,
    serial: u32,
    regions: [Option<RegionNode>; KINDS.len()],
    main: *mut sys::BKNode,
}

struct KitScreen {
    node: *mut sys::BKNode,
    areas: Vec<AreaNode>,
    next_serial: u32,
    seen: bool,
    /// where it was placed (device pixels)
    placed: [i32; 4],
}

/// The screens of one window, by element key.
#[derive(Default)]
pub(crate) struct Screens {
    map: HashMap<Key, (String, KitScreen)>,
}

/// What a kit node of a screen is, for its events.
pub(crate) enum Target {
    Screen(Key),
    Area(Key),
    Region(Key),
}

fn cstring(s: &str) -> CString {
    CString::new(s.replace('\0', "")).unwrap_or_default()
}

fn rect(node: *const sys::BKNode) -> [i32; 4] {
    let mut r = [0i32; 4];
    // SAFETY: a live kit node; four ints.
    unsafe { sys::BK_node_rect(node, r.as_mut_ptr()) };
    r
}

fn theme(key: &CStr) -> Rgba {
    let mut c = [0.0f32; 4];
    // SAFETY: a NUL-terminated key, four floats.
    unsafe { sys::BK_theme_color(key.as_ptr(), c.as_mut_ptr()) };
    Rgba { r: c[0], g: c[1], b: c[2], a: c[3] }
}

/// Builds `split`'s areas off `area` (the kit splits as the user would).
fn build(area: *mut sys::BKNode, split: &AreaSplit) {
    match split {
        AreaSplit::Editor(name) => {
            let c = cstring(name);
            // SAFETY: a live area node.
            unsafe { sys::BK_set_str(area, c"editor".as_ptr(), c.as_ptr()) };
        }
        AreaSplit::Split { axis, factor, first, second } => {
            // kit factors run from the left and the bottom; AreaSplit's from the left and the top
            let kit = if *axis == Axis::V { *factor } else { 1.0 - *factor };
            let a = if *axis == Axis::V { c_int_of(b'v') } else { c_int_of(b'h') };
            // SAFETY: a live area node.
            let new = unsafe { sys::BK_area_split(area, a, kit) };
            if new.is_null() {
                build(area, first);
                return;
            }
            LIVE.with(|l| l.borrow_mut().insert(new as usize));
            // the new area is the side past 0.5
            let new_is_first = if *axis == Axis::V { kit <= 0.5 } else { kit > 0.5 };
            let (f, s) = if new_is_first { (new, area) } else { (area, new) };
            build(f, first);
            build(s, second);
        }
    }
}

impl KitScreen {
    fn new(canvas: *mut sys::BKNode, spec: &Spec) -> KitScreen {
        // SAFETY: fresh nodes added to our canvas; plain property calls.
        let node = unsafe {
            let node = sys::BK_node_new(c"screen".as_ptr());
            sys::BK_set_num(node, c"transparent".as_ptr(), 1.0);
            if spec.resize_only {
                sys::BK_set_str(node, c"ops".as_ptr(), c"resize".as_ptr());
            }
            sys::BK_node_add(canvas, node, -1);
            sys::BK_geom_set_manager(node, sys::BK_GEOM_PLACE);
            let first = sys::BK_node_new(c"area".as_ptr());
            sys::BK_node_add(node, first, -1);
            LIVE.with(|l| l.borrow_mut().insert(first as usize));
            match spec.first {
                Some(split) => build(first, split),
                None => {
                    let name = spec.editors.and_then(|e| e.list.first()).map_or("", |(n, _, _)| &**n);
                    build(first, &AreaSplit::Editor(Cow::Owned(name.to_owned())));
                }
            }
            if let Some(saved) = spec.saved {
                let c = cstring(saved);
                sys::BK_set_str(node, c"layout".as_ptr(), c.as_ptr());
            }
            node
        };
        KitScreen { node, areas: Vec::new(), next_serial: 0, seen: true, placed: [0; 4] }
    }

    /// The area records follow the kit's areas (made and closed by the user too).
    fn sync_areas(&mut self) {
        // SAFETY: our live screen node.
        let n = unsafe { sys::BK_node_child_count(self.node) };
        let kit: Vec<*mut sys::BKNode> = (0..n).map(|i| unsafe { sys::BK_node_child(self.node, i) }).collect();
        self.areas.retain(|a| kit.contains(&a.node));
        LIVE.with(|l| {
            let mut l = l.borrow_mut();
            for &k in &kit {
                l.insert(k as usize);
            }
        });
        for (i, &k) in kit.iter().enumerate() {
            match self.areas.iter().position(|a| a.node == k) {
                Some(p) if p == i => {}
                Some(p) => {
                    let a = self.areas.remove(p);
                    self.areas.insert(i, a);
                }
                None => {
                    self.next_serial += 1;
                    // SAFETY: a fresh main-region node in a live area.
                    let main = unsafe {
                        let m = sys::BK_node_new(c"column".as_ptr());
                        sys::BK_node_add(k, m, -1);
                        m
                    };
                    self.areas.insert(i, AreaNode { node: k, serial: self.next_serial, regions: Default::default(),
                                                    main });
                }
            }
        }
    }

    fn owns(&self, node: *mut sys::BKNode) -> Option<(u32, Option<usize>)> {
        for a in &self.areas {
            if a.node == node {
                return Some((a.serial, None));
            }
            for (k, r) in a.regions.iter().enumerate() {
                if r.as_ref().is_some_and(|r| r.node == node) {
                    return Some((a.serial, Some(k)));
                }
            }
        }
        None
    }
}

/// The key the element of area `serial` (and its region `kind`) gets.
fn area_key(screen: &str, serial: u32, kind: Option<&str>) -> String {
    let mut s = String::with_capacity(screen.len() + 16);
    let _ = write!(s, "{screen}\u{1f}a{serial}");
    if let Some(k) = kind {
        s.push('\u{1f}');
        s.push_str(k);
    }
    s
}

/// Positions an element absolutely at `r` (device pixels) inside `origin`.
fn place_at(d: &mut Div, r: [i32; 4], origin: [i32; 4], scale: f32, bg: Option<Rgba>) {
    let el = std::mem::replace(d, Div::new("div"));
    let mut el = el.block().absolute()
        .left(px((r[0] - origin[0]) as f32 / scale))
        .top(px((r[1] - origin[1]) as f32 / scale))
        .w(px(r[2] as f32 / scale))
        .h(px(r[3] as f32 / scale))
        .overflow_hidden();
    if r[2] <= 0 || r[3] <= 0 {
        el = el.hidden();
    }
    if let Some(c) = bg {
        el = el.bg(c);
    }
    *d = el;
}

impl Screens {
    /// Syncs a `<bui-screen>` with its kit nodes; the new screen's node when it was just made.
    pub(crate) fn sync(&mut self, canvas: *mut sys::BKNode, scale: f32, div: &mut Div, spec: &Spec)
                       -> Option<*mut sys::BKNode> {
        let name: String = div.id.as_deref().unwrap_or("bui-screen").to_owned();
        let key = id_key(&name);
        let mut made = None;
        let (_, scr) = self.map.entry(key).or_insert_with(|| {
            let scr = KitScreen::new(canvas, spec);
            made = Some(scr.node);
            (name.clone(), scr)
        });
        scr.seen = true;
        scr.sync_areas();
        if let Some(editors) = spec.editors {
            div.children.clear();
            for a in &scr.areas {
                div.children.push(editors.make(&AreaRef { node: a.node }).into_area().into_any_element());
            }
        }
        let origin = rect(scr.node);
        let header = theme(c"area_header.background");
        let back = theme(c"region.background");
        let mut areas = scr.areas.iter_mut();
        for child in &mut div.children {
            let AnyElement::Element(ad) = child else { continue };
            if ad.tag != "bui-area" {
                continue;
            }
            let Some(area) = areas.next() else {
                *ad = Div::new("bui-area").hidden(); // more areas than the screen has
                continue;
            };
            let area_rect = rect(area.node);
            let old = std::mem::replace(ad, Div::new("div"));
            *ad = old.key(area_key(&name, area.serial, None));
            place_at(ad, area_rect, origin, scale, None);
            let mut present = [false; KINDS.len()];
            for rc in &mut ad.children {
                let AnyElement::Element(rd) = rc else { continue };
                match rd.tag {
                    "bui-region" => {
                        let Some(k) = rd.get_attr("region").and_then(|r| KINDS.iter().position(|&n| n == r)) else {
                            continue;
                        };
                        present[k] = true;
                        let reg = area.regions[k].get_or_insert_with(|| {
                            let kind = cstring(KINDS[k]);
                            // SAFETY: a fresh node made a region of a live area.
                            let node = unsafe {
                                let n = sys::BK_node_new(c"column".as_ptr());
                                sys::BK_set_str(n, c"region".as_ptr(), kind.as_ptr());
                                sys::BK_node_add(area.node, n, -1);
                                n
                            };
                            RegionNode { node, applied: Default::default() }
                        });
                        for (p, prop) in PROPS.iter().enumerate() {
                            let want = rd.get_attr(prop);
                            if want != reg.applied[p].as_deref() {
                                if let Some(v) = want {
                                    let k = cstring(prop);
                                    // SAFETY: our live region node; refused values are ignored.
                                    unsafe {
                                        if matches!(*prop, "side" | "hotkey") {
                                            let c = cstring(v);
                                            sys::BK_set_str(reg.node, k.as_ptr(), c.as_ptr());
                                        } else {
                                            sys::BK_set_num(reg.node, k.as_ptr(), v.parse().unwrap_or(0.0));
                                        }
                                    }
                                }
                                reg.applied[p] = want.map(str::to_owned);
                            }
                        }
                        // SAFETY: our live region node.
                        let overlap = unsafe { sys::BK_get_num(reg.node, c"overlap".as_ptr()) } != 0.0;
                        let headerish = k <= 2; // header, tool header, footer
                        let bg = (!overlap).then_some(if headerish { header } else { back });
                        let old = std::mem::replace(rd, Div::new("div"));
                        *rd = old.key(area_key(&name, area.serial, Some(KINDS[k])));
                        place_at(rd, rect(reg.node), area_rect, scale, bg);
                    }
                    "bui-main" => place_at(rd, rect(area.main), area_rect, scale, Some(back)),
                    _ => {}
                }
            }
            for (k, slot) in area.regions.iter_mut().enumerate() {
                if let Some(r) = slot.take_if(|_| !present[k]) {
                    // SAFETY: our region node, detached and freed.
                    unsafe { sys::BK_node_free(r.node) };
                }
            }
        }
        // the screen's own element: a block filling its place (under the author's styles), its
        // children at the kit's rects
        let defaults = Div::new("div").block().w_full().h_full();
        div.decls.splice(0..0, defaults.decls);
        let old = std::mem::replace(div, Div::new("div"));
        *div = old.relative();
        made
    }

    /// After layout: each screen's kit node goes where its element is; screens not rendered go.
    pub(crate) fn after_layout(&mut self, frame: &Frame, scale: f32) {
        self.map.retain(|key, (_, scr)| {
            if !std::mem::take(&mut scr.seen) {
                LIVE.with(|l| {
                    let mut l = l.borrow_mut();
                    for a in &scr.areas {
                        l.remove(&(a.node as usize));
                    }
                });
                // SAFETY: our screen node (and its areas and regions), detached and freed.
                unsafe { sys::BK_node_free(scr.node) };
                return false;
            }
            let Some(&i) = frame.by_key().get(key) else { return true };
            let b = frame.nodes[i].bounds;
            let want = [(b.origin.x * scale).round() as i32, (b.origin.y * scale).round() as i32,
                        (b.size.width * scale).round() as i32, (b.size.height * scale).round() as i32];
            if want != scr.placed {
                scr.placed = want;
                // SAFETY: our live screen node, placed on the canvas.
                unsafe {
                    for (k, v) in [c"x", c"y", c"width", c"height"].iter().zip(want) {
                        sys::BK_geom_set(scr.node, k.as_ptr(), f64::from(v));
                    }
                }
            }
            true
        });
    }

    /// What `node` is, when it belongs to one of these screens.
    pub(crate) fn target(&self, node: *mut sys::BKNode) -> Option<Target> {
        for (key, (name, scr)) in &self.map {
            if scr.node == node {
                return Some(Target::Screen(*key));
            }
            if let Some((serial, region)) = scr.owns(node) {
                let k = id_key(&area_key(name, serial, region.map(|r| KINDS[r])));
                return Some(match region {
                    Some(_) => Target::Region(k),
                    None => Target::Area(k),
                });
            }
        }
        None
    }

    /// The layout of the screen with element key `key`.
    pub(crate) fn layout(&self, key: Key) -> String {
        self.map.get(&key).map_or_else(String::new, |(_, scr)| {
            // SAFETY: our live screen node; the string is copied at once.
            unsafe { CStr::from_ptr(sys::BK_get_str(scr.node, c"layout".as_ptr())) }.to_string_lossy().into_owned()
        })
    }
}

/// The kit's events, as they happen (from its callback): areas made and freed, the editor of
/// an area leaving for a window.
pub(crate) fn kit_event(node: *mut sys::BKNode, kind: std::os::raw::c_int, a: f64, b: f64) {
    if kind != sys::BK_EV_AREA || node.is_null() {
        return;
    }
    match b as std::os::raw::c_int {
        sys::BK_AREA_NEW => {
            // SAFETY: the screen node sending it; its child `a` is the new area.
            let n = unsafe { sys::BK_node_child(node, a as std::os::raw::c_int) };
            if !n.is_null() {
                LIVE.with(|l| l.borrow_mut().insert(n as usize));
            }
        }
        sys::BK_AREA_CLOSE => {
            LIVE.with(|l| l.borrow_mut().remove(&(node as usize)));
        }
        sys::BK_AREA_WINDOW => {
            let editor = AreaRef { node }.editor().to_owned();
            WINDOWED.with(|w| w.borrow_mut().insert(node as usize, editor));
        }
        _ => {}
    }
}

/// The change event an event of the kit's makes for the element `target` names.
pub(crate) fn change_event(target: &Target, screens: &Screens, node: *mut sys::BKNode, kind: std::os::raw::c_int,
                           a: f64, b: f64) -> Option<(Key, ChangeEvent)> {
    match (target, kind) {
        (Target::Screen(k), sys::BK_EV_CHANGE) => Some((*k, ChangeEvent { value: screens.layout(*k), ..Default::default() })),
        (Target::Area(k), sys::BK_EV_AREA) if b as std::os::raw::c_int == sys::BK_AREA_WINDOW => {
            let editor = WINDOWED.with(|w| w.borrow_mut().remove(&(node as usize))).unwrap_or_default();
            Some((*k, ChangeEvent { value: format!("window\t{editor}"), ..Default::default() }))
        }
        (Target::Region(k), sys::BK_EV_RESIZE) => {
            Some((*k, ChangeEvent { value: String::new(), checked: b != 0.0, number: a }))
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn area_split_text_round_trip() {
        let s = AreaSplit::v(0.75, "3D \"Viewport\"", AreaSplit::h(0.4, "Outliner", "Properties"));
        let t = s.to_text();
        assert_eq!(AreaSplit::parse(&t), Some(s));
        assert_eq!(AreaSplit::parse("v1.5(\"a\",\"b\")"), None);
        assert_eq!(AreaSplit::parse("v0.5(\"a\""), None);
        assert_eq!(AreaSplit::parse("\"one\""), Some(AreaSplit::from("one")));
    }
}
