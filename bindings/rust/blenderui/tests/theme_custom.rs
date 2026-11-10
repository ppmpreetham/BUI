//! Custom themes: a theme file, the plain-English and Blender key names, single values at run time,
//! and the `styled` HTML sheet following them. One test (the theme is process-global).

use blenderui::prelude::*;

struct Page;

impl Render for Page {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        body()
            .m_0()
            .child(div().id("themed").styled().h(px(40.0)).child(pre().id("pre").child("x")))
            .child(div().id("plain").h(px(20.0)))
    }
}

fn draw(cx: &mut App) -> (usize, usize, Vec<u32>) {
    let opts = WindowOptions {
        window_bounds: Some(WindowBounds::Windowed(Bounds {
            origin: point(px(0.0), px(0.0)),
            size: size(px(200.0), px(80.0)),
        })),
        ..Default::default()
    };
    let win = cx.open_window(opts, |_, cx| cx.new(|_| Page)).unwrap();
    let img = cx.draw(win.id);
    cx.close_window(win.id);
    img
}

const FILE: &str = r##"{
  "name": "Test set",
  "themes": [
    { "name": "Plum", "mode": "dark", "radius": 0.8,
      "colors": {
        "background": "#102030",
        "primary.background": "#336699",
        "button.background": "#445566",
        "user_interface.wcol_num.inner": "#0a0b0c",
        "button.border.width": 3,
        "button.corners.top_left": false,
        "view_3d.vertex_size": 5,
        "no.such.key": "#ffffff" } },
    { "name": "Paper", "mode": "light", "colors": { "background": "#fafafa" } }
  ] }"##;

fn near(c: [f32; 4], rgb: u32) -> bool {
    let want = [(rgb >> 16 & 255) as f32, (rgb >> 8 & 255) as f32, (rgb & 255) as f32];
    (0..3).all(|i| (c[i] * 255.0 - want[i]).abs() < 0.6)
}

#[test]
fn custom_themes() {
    // errors say where, and add nothing
    assert!(blenderui::load_theme("{ \"themes\": [ ").unwrap_err().contains("line 1"));
    assert!(blenderui::load_theme(r#"{"themes": [ {"name": "A"}, {} ]}"#).unwrap_err().contains("themes[1]"));
    assert!(blenderui::load_theme_file("no/such/theme.json").unwrap_err().contains("cannot open"));
    assert!(blenderui::themes().is_empty());

    assert_eq!(blenderui::load_theme(FILE), Ok(2));
    assert_eq!(blenderui::themes(), ["Plum", "Paper"]);
    assert!(blenderui::set_theme("plum"));
    assert_eq!(blenderui::current_theme(), "Plum");
    assert_eq!(blenderui::theme_name(), "dark"); // its mode

    // plain-English and Blender's names are one key; short names fill in, exact ones win
    assert!(near(blenderui::theme_color("button.background").unwrap(), 0x445566));
    assert!(near(blenderui::theme_color("user_interface.wcol_regular.inner").unwrap(), 0x445566));
    assert!(near(blenderui::theme_color("tool_button.background.selected").unwrap(), 0x336699));
    assert!(near(blenderui::theme_color("number_field.background").unwrap(), 0x0a0b0c));
    assert_eq!(blenderui::theme_value("button.border.width"), Some(3.0));
    assert_eq!(blenderui::theme_value("button.corners.top_left"), Some(0.0));
    assert_eq!(blenderui::theme_value("button.corners.top_right"), Some(1.0));
    assert!((blenderui::theme_value("slider.roundness").unwrap() - 0.8).abs() < 1e-6);

    let at = |img: &(usize, usize, Vec<u32>), x: f32, y: f32| {
        let s = img.0 as f32 / 200.0;
        img.2[(y * s) as usize * img.0 + (x * s) as usize] & 0xffffff
    };
    Application::headless().run(|cx| {
        // the `styled` sheet takes the theme's background; plain HTML stays white
        let img = draw(cx);
        assert_eq!(at(&img, 190.0, 38.0), 0x102030);
        assert_eq!(at(&img, 190.0, 70.0), 0xffffff);

        // one color at a time, until the next set_theme
        assert!(blenderui::set_theme_color("region.background", [0.0, 1.0, 0.0, 1.0]));
        assert!(!blenderui::set_theme_color("no.such.key", [0.0; 4]));
        assert!(!blenderui::set_theme_color("button.border.width", [0.0; 4])); // a number
        let img = draw(cx);
        assert_eq!(at(&img, 190.0, 38.0), 0x00ff00);
        assert!(blenderui::set_theme_value("button.roundness", 0.25));
        assert_eq!(blenderui::theme_value("button.roundness"), Some(0.25));

        // the other theme starts from the light preset
        assert!(blenderui::set_theme("Paper"));
        assert_eq!(blenderui::theme_name(), "light");
        let img = draw(cx);
        assert_eq!(at(&img, 190.0, 38.0), 0xfafafa);
        assert!(near(blenderui::theme_color("button.background").unwrap(), 0xdbdbdb));
    });

    // the built-in themes are exactly what they were
    assert!(blenderui::set_theme("dark"));
    assert_eq!(blenderui::current_theme(), "dark");
    assert_eq!(blenderui::html::themed_styled_css(), blenderui::html::UA_STYLED_CSS);
    assert!(blenderui::set_theme("light"));
    assert_eq!(blenderui::html::themed_styled_css(), blenderui::html::ua_styled_css("light"));
    assert!(blenderui::set_theme("dark"));
}
