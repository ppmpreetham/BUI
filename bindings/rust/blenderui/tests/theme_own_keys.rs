//! A theme's own keys: what BUI has no field for is read back by key while that theme is in use.
//! One test (the theme is process-global).

use blenderui::{load_theme, set_theme, theme_color, theme_value};

#[test]
fn a_theme_carries_keys_of_its_own() {
    let n = load_theme(
        r##"{ "themes": [ { "name": "Own", "colors": {
            "muted.foreground": "#8696A0", "list.hover.background": "#2a3942", "bubble.radius": 14 } } ] }"##,
    );
    assert_eq!(n, Ok(1));
    assert_eq!(theme_color("muted.foreground"), None, "not the active theme's yet");
    assert!(set_theme("Own"));
    let c = theme_color("muted.foreground").expect("the theme's own color");
    assert!((c[0] - 0x86 as f32 / 255.0).abs() < 2e-3 && (c[2] - 0xa0 as f32 / 255.0).abs() < 2e-3);
    assert!(theme_color("list.hover.background").is_some());
    assert_eq!(theme_value("bubble.radius"), Some(14.0));
    assert_eq!(theme_color("bubble.radius"), None, "a number is not a color");
    assert!(theme_color("button.background").is_some(), "the keys BUI has are the preset's");
    assert!(set_theme("dark"));
    assert_eq!(theme_color("muted.foreground"), None, "gone with the theme");
}
