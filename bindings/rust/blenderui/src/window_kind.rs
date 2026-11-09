//! Window kinds, decorations and layer-shell options (gpui's `WindowKind`,
//! `WindowDecorations` and `layer_shell`), sent to the host as window requests.

use std::ops::BitOr;

use crate::geometry::Pixels;

/// What a window is for (gpui's `WindowKind`).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub enum WindowKind {
    #[default]
    Normal,
    /// No frame, above other windows.
    PopUp,
    /// Above other windows ("topmost"). A Wayland toplevel cannot ask for this:
    /// use [`WindowKind::LayerShell`] there.
    Floating,
    /// A wlr-layer-shell surface (Wayland compositors with layer shell: KDE Plasma,
    /// wlroots). Elsewhere it is a normal window.
    LayerShell(LayerShellOptions),
}

/// Who draws the window frame (gpui's `WindowDecorations`).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum WindowDecorations {
    /// The system's frame.
    #[default]
    Server,
    /// No frame: the app draws everything.
    Client,
}

/// A layer-shell layer, bottom-most first.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Layer {
    Background = 0,
    Bottom = 1,
    Top = 2,
    #[default]
    Overlay = 3,
}

/// Whether a layer surface takes the keyboard.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum KeyboardInteractivity {
    #[default]
    None = 0,
    Exclusive = 1,
    OnDemand = 2,
}

/// Screen edges a layer surface sticks to: `Anchor::TOP | Anchor::LEFT`. Anchored to both
/// edges of an axis, the surface fills that axis.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Anchor(pub u8);

impl Anchor {
    pub const TOP: Anchor = Anchor(1);
    pub const BOTTOM: Anchor = Anchor(2);
    pub const LEFT: Anchor = Anchor(4);
    pub const RIGHT: Anchor = Anchor(8);
}

impl BitOr for Anchor {
    type Output = Anchor;

    fn bitor(self, rhs: Anchor) -> Anchor {
        Anchor(self.0 | rhs.0)
    }
}

/// gpui's `LayerShellOptions`.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct LayerShellOptions {
    pub layer: Layer,
    pub anchor: Anchor,
    /// Space the compositor keeps free for the surface (a panel); `None`: none.
    pub exclusive_zone: Option<Pixels>,
    pub keyboard_interactivity: KeyboardInteractivity,
}

type Request = (&'static str, f64, f64);

impl WindowKind {
    /// The window requests (key, a, b) that make a new window of this kind and decoration.
    pub(crate) fn requests(self, decorations: WindowDecorations) -> impl Iterator<Item = Request> {
        let (first, second) = match self {
            WindowKind::Normal => (None, None),
            WindowKind::PopUp | WindowKind::Floating => (Some(("topmost", 1.0, 0.0)), None),
            WindowKind::LayerShell(o) => (
                Some(("layer", f64::from(o.layer as u8), f64::from(o.keyboard_interactivity as u8))),
                Some(("anchor", f64::from(o.anchor.0), f64::from(o.exclusive_zone.map_or(0.0, |z| z.0)))),
            ),
        };
        let frameless = decorations == WindowDecorations::Client || self == WindowKind::PopUp;
        [first, second, frameless.then_some(("override", 1.0, 0.0))].into_iter().flatten()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::geometry::px;

    fn requests(kind: WindowKind, decorations: WindowDecorations) -> Vec<Request> {
        kind.requests(decorations).collect()
    }

    #[test]
    fn normal_window_sends_nothing() {
        assert!(requests(WindowKind::Normal, WindowDecorations::Server).is_empty());
    }

    #[test]
    fn floating_is_topmost_and_pop_up_has_no_frame() {
        assert_eq!(requests(WindowKind::Floating, WindowDecorations::Server), [("topmost", 1.0, 0.0)]);
        assert_eq!(
            requests(WindowKind::PopUp, WindowDecorations::Server),
            [("topmost", 1.0, 0.0), ("override", 1.0, 0.0)]
        );
        assert_eq!(requests(WindowKind::Normal, WindowDecorations::Client), [("override", 1.0, 0.0)]);
    }

    #[test]
    fn layer_shell_sends_layer_then_anchor() {
        let kind = WindowKind::LayerShell(LayerShellOptions {
            layer: Layer::Top,
            anchor: Anchor::TOP | Anchor::LEFT,
            exclusive_zone: Some(px(24.0)),
            keyboard_interactivity: KeyboardInteractivity::OnDemand,
        });
        assert_eq!(
            requests(kind, WindowDecorations::Server),
            [("layer", 2.0, 2.0), ("anchor", 5.0, 24.0)]
        );
    }
}
