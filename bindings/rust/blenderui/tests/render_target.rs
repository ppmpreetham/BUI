//! Render targets: effects that draw into textures, with feedback. Needs a GPU device (Direct3D 11 or
//! OpenGL 3.3); without one each test says so and passes.

use std::sync::{Mutex, MutexGuard, PoisonError};

use blenderui::{GpuTarget, RenderTarget, ShaderEffect, TargetFormat};

/// The native effect registry is not synchronized and the harness runs tests in parallel: every
/// test takes this first (declared before any GPU object, so it is released after they drop).
static GPU: Mutex<()> = Mutex::new(());

fn gpu_lock() -> MutexGuard<'static, ()> {
    GPU.lock().unwrap_or_else(PoisonError::into_inner) // a failed test must not fail the rest
}

/// state.r += U[0], read back exactly (`input_load`), written to the other target of a pair
const STEP: &str = "vec4 effect(vec2 p) {
    vec4 s = input_load(0, p);
    return vec4(s.r + U[0], s.g, p.x, 1.0);
}";
/// the first texel's red as the frame's red
const SHOW: &str = "vec4 effect(vec2 p) { return vec4(input_load(0, vec2(0.5, 0.5)).r, 0.0, 0.0, 1.0); }";
/// each texel's `b` (its pixel x) as the frame's red, /8
const PIXELS: &str = "vec4 effect(vec2 p) { return vec4(input_load(0, p).b / 8.0, 0.0, 0.0, 1.0); }";
/// a value no byte can hold
const CONSTANT: &str = "vec4 effect(vec2 p) { return vec4(3.5, 0.0, 0.0, 1.0); }";
const SHOW_THIRD: &str = "vec4 effect(vec2 p) { return vec4(input_raw(0, vec2(0.5, 0.5)).r / 4.0, 0.0, 0.0, 1.0); }";

fn red(gpu: &GpuTarget, i: usize) -> u32 {
    (gpu.pixels()[i] >> 16) & 255
}

/// `passes` of +0.001 ping-ponged between two targets of `format`; the first texel's red, /255.
fn accumulate(gpu: &mut GpuTarget, format: TargetFormat, passes: usize) -> u32 {
    let a = RenderTarget::new(4, 4, format).expect("target a");
    let b = RenderTarget::new(4, 4, format).expect("target b");
    let step = ShaderEffect::new("step", STEP, 1, 1).unwrap();
    let show = ShaderEffect::new("show", SHOW, 1, 0).unwrap();
    let (mut src, mut dst) = (&a, &b);
    for _ in 0..passes {
        assert!(gpu.draw_to(dst, &step, &[src.texture()], &[0.001]), "pass failed: {}", step.error());
        std::mem::swap(&mut src, &mut dst);
    }
    assert!(gpu.draw(&show, &[src.texture()], &[]), "{}", show.error());
    red(gpu, 0)
}

#[test]
fn float_targets_keep_what_a_byte_cannot() {
    let _lock = gpu_lock();
    let Some(mut gpu) = GpuTarget::new(8, 8) else {
        eprintln!("no GPU device: skipped");
        return;
    };
    // 500 passes of +0.001 through float targets: 0.5, i.e. ~128 of 255 (half floats round a little)
    assert!((i64::from(accumulate(&mut gpu, TargetFormat::Rgba32F, 500)) - 127).abs() <= 1);
    assert!((i64::from(accumulate(&mut gpu, TargetFormat::Rgba16F, 500)) - 127).abs() <= 6);
    // through bytes every +0.001 rounds away: the feedback never leaves 0
    assert_eq!(accumulate(&mut gpu, TargetFormat::Rgba8, 500), 0);
}

#[test]
fn a_pass_runs_over_the_whole_target_with_its_own_pixels() {
    let _lock = gpu_lock();
    let Some(mut gpu) = GpuTarget::new(8, 8) else {
        return;
    };
    let t = RenderTarget::new(4, 4, TargetFormat::Rgba32F).unwrap();
    let blank = RenderTarget::new(4, 4, TargetFormat::Rgba32F).unwrap(); // transparent black until drawn
    let step = ShaderEffect::new("step", STEP, 1, 1).unwrap();
    let pixels = ShaderEffect::new("pixels", PIXELS, 1, 0).unwrap();
    assert!(gpu.draw_to(&t, &step, &[blank.texture()], &[0.0]));
    assert!(gpu.draw(&pixels, &[t.texture()], &[]));
    // p.x is the pixel centre: 0.5 1.5 2.5 3.5, so red = x / 8 * 255 within a rounding step
    let row: Vec<u32> = (0..4).map(|x| red(&gpu, x)).collect();
    assert_eq!(row, vec![16, 48, 80, 112], "{row:?}");
}

#[test]
fn a_target_can_be_cleared_and_holds_values_over_one() {
    let _lock = gpu_lock();
    let Some(mut gpu) = GpuTarget::new(8, 8) else {
        return;
    };
    let t = RenderTarget::new(2, 2, TargetFormat::Rgba16F).unwrap();
    let constant = ShaderEffect::new("constant", CONSTANT, 0, 0).unwrap();
    let show = ShaderEffect::new("show", SHOW_THIRD, 1, 0).unwrap();
    assert!(gpu.draw_to(&t, &constant, &[], &[]));
    assert!(gpu.draw(&show, &[t.texture()], &[]));
    assert_eq!(red(&gpu, 0), 223, "3.5 / 4 stored beyond 1.0 and read back bilinearly"); // 0.875 * 255
    assert!(gpu.clear_target(&t, [1.0, 0.0, 0.0, 1.0]));
    assert!(gpu.draw(&show, &[t.texture()], &[]));
    assert_eq!(red(&gpu, 0), 64, "cleared to 1.0, shown as 1.0 / 4");
}

#[test]
fn a_pass_cannot_sample_the_target_it_draws_into() {
    let _lock = gpu_lock();
    let Some(mut gpu) = GpuTarget::new(8, 8) else {
        return;
    };
    let t = RenderTarget::new(2, 2, TargetFormat::Rgba16F).unwrap();
    let step = ShaderEffect::new("step", STEP, 1, 1).unwrap();
    assert!(!gpu.draw_to(&t, &step, &[t.texture()], &[0.0]), "a feedback loop swaps two targets");
}
