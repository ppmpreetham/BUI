//! A layer turned on the GPU (the `TURN_SHADER` effect over the box that
//! `turn_box` gives) lands where the CPU turn puts it, with the same pixels.
//! Needs a GPU (Direct3D 11 / OpenGL 3.3); without one the test has nothing
//! to check and passes.

use blenderui::effects::{transform, turn_box, turn_texture, Argb, Pixels, TURN_SHADER};
use blenderui::{GpuTarget, Image, ShaderEffect};

/// A quarter turn of a 64 x 32 image (left half red, right half blue) about
/// its centre, on the GPU and on the CPU: the same colours at the same places.
#[test]
fn a_gpu_turn_matches_the_cpu_one() {
    let (w, h) = (64usize, 32usize);
    let rgba: Vec<u8> = (0..w * h)
        .flat_map(|i| if i % w < w / 2 { [255, 0, 0, 255] } else { [0, 0, 255, 255] })
        .collect();
    let origin = (100.0, 100.0);
    let (cx, cy) = (origin.0 + w as f32 / 2.0, origin.1 + h as f32 / 2.0);
    // turn by 90 degrees about the centre: (x, y) -> (cx - (y - cy), cy + (x - cx))
    let m = [0.0, 1.0, -1.0, 0.0, cx + cy, cy - cx];
    let (at, floats) = turn_box(origin, (w as f32, h as f32), &m, 1.0, 1.0).expect("a turnable matrix");
    assert_eq!((at.size.width.round(), at.size.height.round()), (32.0, 64.0), "a quarter turn stands it up");

    let image = Image::from_rgba(w as u32, h as u32, &rgba).expect("an image");
    let (bw, bh) = (at.size.width.round() as usize, at.size.height.round() as usize);
    let Some(mut gpu) = GpuTarget::new(bw, bh) else {
        eprintln!("no GPU device: nothing to compare");
        return;
    };
    let texture = image.texture();
    assert!(texture > 0, "a texture of the layer");
    let turn = ShaderEffect::new("bui-turn", TURN_SHADER, 1, 7).expect("the turn shader");
    let f = [floats[0], floats[1], floats[2], floats[3], floats[4], floats[5], 1.0];
    assert!(gpu.draw(&turn, &[texture], &f), "the shader ran: {}", turn.error());
    let on_gpu = gpu.pixels().to_vec();

    let src = Pixels::from_rgba8(w, h, &rgba);
    let (cpu, cpu_at) = transform(&src, origin, &m).expect("a CPU turn");
    let cpu_rgba = cpu.to_rgba8();
    // the CPU image starts at `cpu_at`, the GPU box at `at`: compare where both have pixels
    let at_gpu = |x: usize, y: usize| on_gpu[y * bw + x] & 0xffffff;
    let at_cpu = |x: usize, y: usize| {
        let (ox, oy) = ((at.origin.x - cpu_at.0).round() as isize, (at.origin.y - cpu_at.1).round() as isize);
        let (cx, cy) = (x as isize + ox, y as isize + oy);
        let k = (cy as usize * cpu.w + cx as usize) * 4;
        u32::from(cpu_rgba[k]) << 16 | u32::from(cpu_rgba[k + 1]) << 8 | u32::from(cpu_rgba[k + 2])
    };
    // well inside each half: the top half of the box came from the left (red) half
    for (x, y) in [(bw / 2, bh / 4), (bw / 2, 3 * bh / 4), (bw / 4, bh / 4), (3 * bw / 4, 3 * bh / 4)] {
        assert_eq!(at_gpu(x, y), at_cpu(x, y), "at ({x}, {y})");
    }
    assert_eq!(at_gpu(bw / 2, bh / 4), 0xff0000, "the left half turned to the top");
    // SAFETY: ours, freed once.
    unsafe { blenderui::sys::BUI_texture_free(texture) };
}

/// Across an edge from opaque red to transparent inside the layer, a turned
/// layer's pixels are the CPU's: its texels are mixed premultiplied (mixed
/// straight, the edge went dark: half red at half alpha drew a quarter red).
#[test]
fn a_gpu_turn_mixes_an_alpha_edge_like_the_cpu() {
    let (w, h) = (32usize, 32usize);
    let argb = Argb { w, h, px: (0..w * h).map(|i| if i % w < w / 2 { 0xffff_0000 } else { 0 }).collect() };
    let origin = (0.0, 0.0);
    // 30 degrees about the centre: every sample falls between texels
    let (c, s) = (30f32.to_radians().cos(), 30f32.to_radians().sin());
    let (cx, cy) = (w as f32 / 2.0, h as f32 / 2.0);
    let m = [c, s, -s, c, cx - c * cx + s * cy, cy - s * cx - c * cy];
    let (at, f) = turn_box(origin, (w as f32, h as f32), &m, 1.0, 1.0).expect("a turnable matrix");
    let (bw, bh) = (at.size.width.round() as usize, at.size.height.round() as usize);
    let Some(mut gpu) = GpuTarget::new(bw, bh) else {
        eprintln!("no GPU device: nothing to compare");
        return;
    };
    let texture = turn_texture(&argb);
    assert!(texture > 0, "a texture of the layer");
    let turn = ShaderEffect::new("bui-turn", TURN_SHADER, 1, 7).expect("the turn shader");
    assert!(gpu.draw(&turn, &[texture], &[f[0], f[1], f[2], f[3], f[4], f[5], 1.0]), "the shader ran: {}", turn.error());
    let on_gpu = gpu.pixels().to_vec();
    // the CPU's bilinear sample of the same texel position (centres at .5 on both)
    let src = Pixels::from_argb(&argb);
    let mut edge = 0;
    for y in 0..bh {
        for x in 0..bw {
            let (px, py) = (x as f32 + 0.5, y as f32 + 0.5);
            let (qx, qy) = (f[0] * px + f[2] * py + f[4], f[1] * px + f[3] * py + f[5]);
            // well inside the layer: its own edges fade out apart
            if qx < 2.0 || qy < 2.0 || qx > w as f32 - 2.0 || qy > h as f32 - 2.0 {
                continue;
            }
            let p = src.sample(qx, qy);
            let cpu_red = (p[0] * 255.0).round() as i32; // premultiplied: over black
            let gpu_red = (on_gpu[y * bw + x] >> 16 & 0xff) as i32;
            edge += i32::from(p[3] > 0.2 && p[3] < 0.8);
            assert!((gpu_red - cpu_red).abs() <= 8, "at ({x}, {y}): GPU red {gpu_red}, CPU {cpu_red} (alpha {})", p[3]);
        }
    }
    assert!(edge > 0, "the edge was sampled");
    // SAFETY: ours, freed once.
    unsafe { blenderui::sys::BUI_texture_free(texture) };
}
