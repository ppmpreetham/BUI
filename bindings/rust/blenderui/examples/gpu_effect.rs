//! GPU effects: an app's own pixel shader in a canvas. On the left a gradient tinted by a texture
//! (its float animates), on the right a deliberately broken shader whose compile log is printed and
//! whose fallback image (an orange checker) is drawn instead. `cargo run --release --example gpu_effect`
//! opens a GPU window; `-- --software` opens the software renderer, which draws the fallback of both.

use std::rc::Rc;
use std::sync::Arc;
use std::time::Instant;

use blenderui::prelude::*;

const GRADIENT: &str = "
vec4 effect(vec2 p) {
    vec2 uv = p / box_size;
    vec4 t = input_color(0, uv * input_size(0));
    vec3 g = vec3(uv.x, uv.y, 0.5 + 0.5 * sin(U[0]));
    return vec4(mix(g, t.rgb, 0.5 * t.a), 1.0);
}";

const BROKEN: &str = "vec4 effect(vec2 p) { return vec4(no_such_symbol); }";

struct Demo {
    start: Instant,
    gradient: Rc<ShaderEffect>,
    broken: Rc<ShaderEffect>,
    texture: i32,
    fallback: Arc<Image>,
    reported: bool,
}

/// A 2 x 2 colour image as a BUI texture (the texture keeps its own copy of the pixels).
fn tint() -> i32 {
    let rgba = [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255];
    Image::from_rgba(2, 2, &rgba).expect("2x2 image").texture()
}

/// What the software renderer shows in place of a shader: an orange checker.
fn checker() -> Arc<Image> {
    let mut px = Vec::new();
    for y in 0..64 {
        for x in 0..64 {
            let on = (x / 8 + y / 8) % 2 == 0;
            px.extend(if on { [240, 150, 30, 255] } else { [90, 50, 10, 255] });
        }
    }
    Arc::new(Image::from_rgba(64, 64, &px).expect("checker"))
}

impl Render for Demo {
    fn render(&mut self, w: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        w.request_animation_frame(); // the float moves every frame
        let t = self.start.elapsed().as_secs_f32();
        if !self.reported && t > 0.5 {
            self.reported = true;
            eprintln!("gradient: {:?}", self.gradient.error());
            eprintln!("broken:   {:?}", self.broken.error());
        }
        let (gradient, broken, texture) = (self.gradient.clone(), self.broken.clone(), self.texture);
        let (left, right) = (self.fallback.clone(), self.fallback.clone());
        body().styled().m_0().flex().gap(px(8.0)).p(px(8.0)).bg(rgb(0x202020)).size_full()
            .child(canvas(move |b, p| p.effect(b, &gradient, &[texture], &[t * 2.0], Some(&left)))
                .w(px(300.0)).h(px(300.0)))
            .child(canvas(move |b, p| p.effect(b, &broken, &[], &[], Some(&right))).w(px(300.0)).h(px(300.0)))
    }
}

fn main() {
    let software = std::env::args().any(|a| a == "--software");
    let shot = std::env::args().skip_while(|a| a != "--screenshot").nth(1);
    let app = if shot.is_some() { Application::headless() } else { Application::new() };
    app.run(move |cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds {
                origin: point(px(0.0), px(0.0)),
                size: size(px(640.0), px(330.0)),
            })),
            gpu: Some(!software),
            ..Default::default()
        };
        let win = cx
            .open_window(opts, |_, cx| {
                cx.new(|_| Demo {
                    start: Instant::now(),
                    gradient: Rc::new(ShaderEffect::new("gradient", GRADIENT, 1, 1).expect("gradient")),
                    broken: Rc::new(ShaderEffect::new("broken", BROKEN, 0, 0).expect("broken")),
                    texture: tint(),
                    fallback: checker(),
                    reported: false,
                })
            })
            .unwrap();
        if let Some(path) = &shot {
            // off-screen is the software renderer: both canvases show their fallback
            let (w, h, px) = cx.draw(win.id);
            let rgba: Vec<u8> = px.iter().flat_map(|p| [(p >> 16) as u8, (p >> 8) as u8, *p as u8, 255]).collect();
            let file = std::fs::File::create(path).expect("create png");
            let mut enc = png::Encoder::new(std::io::BufWriter::new(file), w as u32, h as u32);
            enc.set_color(png::ColorType::Rgba);
            enc.set_depth(png::BitDepth::Eight);
            enc.write_header().expect("png").write_image_data(&rgba).expect("png");
        }
    });
}
