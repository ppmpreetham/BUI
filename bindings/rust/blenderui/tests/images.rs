//! Images through the kit's decoders: every format, `<img>` and canvas
//! files decoded off the UI thread (leaks: image_memory.rs).

use blenderui::prelude::*;
use blenderui::{sys, Image};

fn fixture(name: &str) -> String {
    format!("{}/../../../tests/images/{name}", env!("CARGO_MANIFEST_DIR")).replace('\\', "/")
}

#[test]
fn every_format_decodes() {
    for (name, w, h) in [("quad.png", 8, 6), ("quad.jpg", 8, 6), ("quad.webp", 8, 6), ("quad_lossy.webp", 8, 6),
                         ("quad.gif", 8, 6), ("quad.bmp", 8, 6), ("quad.tga", 8, 6), ("quad.qoi", 8, 6),
                         ("quad.hdr", 8, 6), ("quad.ppm", 8, 6), ("halves.svg", 16, 12), ("anim.gif", 8, 8),
                         ("anim.webp", 8, 8)] {
        let img = Image::open(&fixture(name)).unwrap_or_else(|| panic!("{name}"));
        assert_eq!((img.width, img.height), (w, h), "{name}");
        let bytes = std::fs::read(fixture(name)).unwrap();
        let small = Image::decode(&bytes, 4, 4).unwrap();
        assert!(small.width <= 4 && small.height <= 4, "{name} at size");
    }
    let png = Image::open(&fixture("quad.png")).unwrap();
    assert_eq!(&png.to_rgba()[..4], &[255, 0, 0, 255]);
    let copy = Image::from_rgba(png.width, png.height, &png.to_rgba()).unwrap();
    assert_eq!(copy.to_rgba(), png.to_rgba());
    assert!(Image::decode(b"not an image", 0, 0).is_none());
    assert!(Image::from_rgba(4, 4, &[0; 8]).is_none(), "short pixels rejected at the boundary");
}

struct Files;
impl Render for Files {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        let png = fixture("quad.png");
        div()
            .child(img().id("img").src(png.clone()).w(px(16.0)).h(px(12.0)))
            .child(canvas(move |b, p| p.image_file(Bounds { origin: point(b.origin.x, b.origin.y), size: size(16.0, 12.0) }, &png))
                .id("cv")
                .w(px(16.0))
                .h(px(12.0)))
    }
}

#[test]
fn img_and_canvas_files_load_off_thread() {
    Application::headless().run(|cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(64.0), px(64.0)) })),
            ..Default::default()
        };
        let win = cx.open_window(opts, |_, cx| cx.new(|_| Files)).unwrap();
        // draw waits for the decodes it started: the pixels are there
        let (pw, _, px) = cx.draw(win.id);
        let at = |x: usize, y: usize| px[y * pw + x] & 0xffffff;
        let b = cx.element_bounds(win.id, "img").unwrap();
        let (x0, y0) = (b.origin.x as usize, b.origin.y as usize);
        assert_eq!(at(x0 + 1, y0 + 1), 0xff0000, "<img> top left red");
        assert_eq!(at(x0 + 14, y0 + 10), 0xffffff, "<img> bottom right white");
        let c = cx.element_bounds(win.id, "cv").unwrap();
        assert_eq!(at(c.origin.x as usize + 1, c.origin.y as usize + 1), 0xff0000, "canvas image_file top left red");
        let mut st = sys::BKImageStats::default();
        // SAFETY: plain stats read.
        unsafe { sys::BK_image_stats(&raw mut st) };
        assert!(st.decoded >= 1 && st.pending == 0, "{st:?}");
        // a settled redraw decodes nothing new
        let decoded = st.decoded;
        cx.draw(win.id);
        unsafe { sys::BK_image_stats(&raw mut st) };
        assert_eq!(st.decoded, decoded);
    });
}

struct Anim;
impl Render for Anim {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        let gif = fixture("anim.gif");
        div()
            .child(img().id("play").src(gif.clone()).w(px(16.0)).h(px(16.0)))
            .child(img().id("held").src(gif).attr("frame", "2").w(px(16.0)).h(px(16.0)))
    }
}

#[test]
fn img_animations_play_or_hold_a_frame() {
    Application::headless().run(|cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(64.0), px(64.0)) })),
            ..Default::default()
        };
        let win = cx.open_window(opts, |_, cx| cx.new(|_| Anim)).unwrap();
        let at = |cx: &mut App, id: &str| {
            let b = cx.element_bounds(win.id, id).unwrap();
            let (pw, _, px) = cx.draw(win.id);
            px[(b.origin.y as usize + 4) * pw + b.origin.x as usize + 4] & 0xffffff
        };
        // the anim.gif frames are red, green, blue
        let blue = |c: u32| c & 0xff >= 0xf0 && (c >> 16) & 0xff < 0x10;
        let first = at(cx, "play");
        assert!(blue(at(cx, "held")), "frame=\"2\" holds the blue frame");
        let mut seen = vec![first];
        for _ in 0..6 {
            std::thread::sleep(std::time::Duration::from_millis(60));
            let c = at(cx, "play");
            if !seen.contains(&c) {
                seen.push(c);
            }
        }
        assert!(seen.len() >= 2, "it plays: {seen:x?}");
        assert!(blue(at(cx, "held")));
    });
}

#[test]
fn animation_frames_into_a_texture() {
    let bytes = std::fs::read(fixture("anim.webp")).unwrap();
    let mut anim = blenderui::Animation::new(&bytes, 4, 4).expect("an animation");
    assert_eq!((anim.width, anim.height, anim.frames), (8, 8, 3));
    let (first, delay) = anim.next_frame().unwrap();
    assert_eq!((first.width, first.height, delay.as_millis()), (4, 4, 100));
    let tex = first.texture();
    assert!(tex > 0);
    let (second, _) = anim.next_frame().unwrap();
    assert!(second.update_texture(tex));
    assert_ne!(first.to_rgba(), second.to_rgba());
    // SAFETY: frees the texture made above.
    unsafe { sys::BUI_texture_free(tex) };
    assert!(blenderui::Animation::new(b"GIF89a", 0, 0).is_none());
}

struct Ratios;
impl Render for Ratios {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        // quad.png is 8 x 6
        let png = fixture("quad.png");
        div().w(px(400.0)).flex().flex_col().items_start()
            .child(img().id("natural").src(png.clone()))
            .child(img().id("wide").src(png.clone()).w(relative(0.5)))
            .child(img().id("square").src(png.clone()).w(px(120.0)).style("aspect-ratio: 1"))
            .child(img().id("tall").src(png).h(px(60.0)))
    }
}

/// An `<img>` with an auto size follows its other size through a ratio, as in
/// CSS: `aspect-ratio` when set, else the picture's own.
#[test]
fn img_auto_sizes_follow_the_ratio() {
    Application::headless().run(|cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(400.0), px(600.0)) })),
            ..Default::default()
        };
        let win = cx.open_window(opts, |_, cx| cx.new(|_| Ratios)).unwrap();
        cx.draw(win.id);
        let size_of = |cx: &mut App, id: &str| {
            let b = cx.element_bounds(win.id, id).unwrap();
            (b.size.width.round(), b.size.height.round())
        };
        assert_eq!(size_of(cx, "natural"), (8.0, 6.0), "no size: the natural one");
        assert_eq!(size_of(cx, "wide"), (200.0, 150.0), "50% wide: as tall as the picture's ratio makes it");
        assert_eq!(size_of(cx, "square"), (120.0, 120.0), "aspect-ratio: 1 wins over the picture's");
        assert_eq!(size_of(cx, "tall"), (80.0, 60.0), "a height alone: the width follows");
    });
}

struct Counter {
    n: u32,
}
impl Render for Counter {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        div().flex().flex_col()
            .child(img().id("cover").src(fixture("quad.png")).w(px(48.0)).h(px(36.0)))
            // far below the image: damage is tracked in tiles of 32 px
            .child(div().id("count").mt(px(100.0)).child(format!("clicked {} times", self.n)))
    }
}

/// A render that changes something else leaves an unchanged `<img>` alone: it
/// is not redrawn (every image on screen was, at every render).
#[test]
fn an_unchanged_img_is_not_redrawn() {
    Application::headless().run(|cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(200.0), px(200.0)) })),
            ..Default::default()
        };
        let h = cx.open_window(opts, |_, cx| cx.new(|_| Counter { n: 0 })).unwrap();
        cx.draw_partial_and_full(h.id);
        cx.draw_partial_and_full(h.id);
        h.update(cx, |v, _, cx| {
            v.n += 1;
            cx.notify();
        });
        let cover = cx.element_bounds(h.id, "cover").unwrap();
        let (_, _, _, _, damage) = cx.draw_partial_and_full(h.id);
        assert!(!damage.is_empty(), "the changed text is redrawn");
        let (cx0, cy0, cx1, cy1) = (cover.origin.x as i32, cover.origin.y as i32,
                                    (cover.origin.x + cover.size.width) as i32, (cover.origin.y + cover.size.height) as i32);
        let over = damage.iter().filter(|[x, y, w, h]| x < &cx1 && y < &cy1 && x + w > cx0 && y + h > cy0).count();
        assert_eq!(over, 0, "the image's area is not damaged: {damage:?}");
    });
}

/// A 256 x 192 picture, red on its left half and blue on its right, as a PPM
/// in the temp folder (the fixtures are all tiny).
fn wide_picture() -> String {
    let (w, h) = (256usize, 192usize);
    let mut ppm = format!("P6 {w} {h} 255\n").into_bytes();
    ppm.extend((0..w * h).flat_map(|i| if i % w < w / 2 { [255, 0, 0] } else { [0, 0, 255] }));
    let path = std::env::temp_dir().join("bui-wide-picture.ppm");
    std::fs::write(&path, ppm).expect("a temp file");
    path.to_string_lossy().replace('\\', "/")
}

struct Wide {
    src: String,
}
impl Render for Wide {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        div().w_full().child(img().id("wide").src(self.src.clone()).w_full().h(px(100.0)))
    }
}

/// Resizing a window scales its pictures: a size within a scale step
/// (2^(1/4)) draws the decode it has, so no decoder runs and the picture
/// never blinks out.
#[test]
fn resizing_an_img_reuses_its_decode() {
    Application::headless().run(|cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(216.0), px(120.0)) })),
            ..Default::default()
        };
        let src = wide_picture();
        let win = cx.open_window(opts, |_, cx| cx.new(|_| Wide { src })).unwrap();
        cx.draw(win.id);
        let mut st = sys::BKImageStats::default();
        // SAFETY: plain stats read.
        unsafe { sys::BK_image_stats(&raw mut st) };
        let decoded = st.decoded;
        // the picture (the window less the body's 8 px margins) from 200 to
        // 185 px of 256: scales 0.78 to 0.72, one step (0.71 to 0.84)
        for width in [195.0, 190.0, 185.0] {
            cx.resize_window(win.id, size(px(width + 16.0), px(120.0)));
            let (pw, _, pixels) = cx.draw(win.id);
            let b = cx.element_bounds(win.id, "wide").expect("the picture");
            assert!((b.size.width - width).abs() < 0.5, "the picture follows the window: {b:?}");
            let (x0, x1, y) = (b.origin.x as usize, (b.origin.x + b.size.width) as usize, b.origin.y as usize + 50);
            let at = |x: usize| pixels[y * pw + x] & 0xffffff;
            assert_eq!((at(x0 + 4), at(x1 - 4)), (0xff0000, 0x0000ff), "both halves at {width} px");
            // SAFETY: plain stats read.
            unsafe { sys::BK_image_stats(&raw mut st) };
            assert_eq!(st.decoded, decoded, "no decode at {width} px");
        }
        // half the size: a smaller decode
        cx.resize_window(win.id, size(px(116.0), px(120.0)));
        cx.draw(win.id);
        // SAFETY: plain stats read.
        unsafe { sys::BK_image_stats(&raw mut st) };
        assert_eq!(st.decoded, decoded + 1, "a decode for a much smaller box");
    });
}
