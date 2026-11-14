//! GPU effects: a pixel shader of the app's over a box, run by the GPU renderers (Direct3D 11,
//! OpenGL), in paint order. [`ShaderEffect::new`] declares it (source, inputs, floats), then
//! [`Painter::effect`](crate::Painter::effect) draws it every frame with its inputs and floats.
//!
//! The source is GLSL with a small compat set (the same on every backend; see the "GPU effects"
//! docs page). It defines `vec4 effect(vec2 p)`: `p` is the pixel inside the box in device pixels,
//! the result is premultiplied RGBA; `box_size`, `U[i]` (the floats), `input_color(i, texel)` and
//! `input_size(i)` are available.
//!
//! ```ignore
//! let fx = ShaderEffect::new("tint", "vec4 effect(vec2 p) {
//!     vec2 uv = p / box_size;
//!     vec4 t = input_color(0, uv * input_size(0));
//!     return vec4(mix(vec3(uv, U[0]), t.rgb, 0.5), 1.0);
//! }", 1, 1).unwrap();
//! canvas(move |bounds, p| p.effect(bounds, &fx, &[texture_id], &[0.25], Some(&fallback)))
//! ```
//!
//! Nothing compiles until a GPU frame draws the effect. Where the GPU cannot run it (the software
//! renderer, a missing shader compiler, a compile error), the fallback image is drawn instead and
//! [`ShaderEffect::error`] says why.

use std::ffi::{c_int, CStr, CString};
use std::marker::PhantomData;

use crate::sys;

/// A shader declared with the kit (`BUI_effect_create`); freed on drop. Effects belong to the
/// thread that draws.
#[derive(Debug)]
pub struct ShaderEffect {
    id: c_int,
    n_inputs: usize,
    n_floats: usize,
    /// The registry is not thread-safe: like the window, an effect stays on its thread.
    _thread: PhantomData<*const ()>,
}

impl ShaderEffect {
    /// Declare an effect: `source` defines `vec4 effect(vec2 p)`, `n_inputs` BUI textures (at most
    /// [`ShaderEffect::MAX_INPUTS`]) and `n_floats` floats (at most [`ShaderEffect::MAX_FLOATS`]) are
    /// available to it. Fails for an empty or over-long source (more than
    /// [`ShaderEffect::MAX_SOURCE`] bytes), interior NULs or counts over the limits; the source
    /// itself is checked when the GPU first draws it (see [`ShaderEffect::error`]).
    pub fn new(name: &str, source: &str, n_inputs: usize, n_floats: usize) -> Result<Self, String> {
        let bad = |what: &str| format!("effect `{name}`: {what}");
        let cname = CString::new(name).map_err(|_| bad("the name has a NUL byte"))?;
        let csource = CString::new(source).map_err(|_| bad("the source has a NUL byte"))?;
        // SAFETY: both strings are NUL-terminated and outlive the call; the kit copies them.
        let id = unsafe {
            sys::BUI_effect_create(
                cname.as_ptr(),
                csource.as_ptr(),
                c_int::try_from(n_inputs).unwrap_or(-1),
                c_int::try_from(n_floats).unwrap_or(-1),
            )
        };
        if id <= 0 {
            return Err(bad(&format!(
                "needs a non-empty source of at most {} bytes, at most {} inputs and {} floats",
                Self::MAX_SOURCE,
                Self::MAX_INPUTS,
                Self::MAX_FLOATS
            )));
        }
        Ok(Self { id, n_inputs, n_floats, _thread: PhantomData })
    }

    /// The most textures an effect samples.
    pub const MAX_INPUTS: usize = sys::BK_EFFECT_MAX_INPUTS;
    /// The most floats an effect reads.
    pub const MAX_FLOATS: usize = sys::BK_EFFECT_MAX_FLOATS;
    /// The longest source, in bytes.
    pub const MAX_SOURCE: usize = sys::BK_EFFECT_MAX_SOURCE;

    /// The kit's id of the effect (`BK_ITEM_EFFECT`'s `effect`).
    #[must_use]
    pub const fn id(&self) -> i32 {
        self.id
    }

    /// How many input textures it declared.
    #[must_use]
    pub const fn n_inputs(&self) -> usize {
        self.n_inputs
    }

    /// How many floats it declared.
    #[must_use]
    pub const fn n_floats(&self) -> usize {
        self.n_floats
    }

    /// The compile/link log of the last GPU attempt to build it: empty when it went fine or no GPU
    /// renderer has tried yet (a window draws the effect on its first GPU frame).
    #[must_use]
    pub fn error(&self) -> String {
        // SAFETY: the kit returns a NUL-terminated string that lives as long as the effect.
        let p = unsafe { sys::BUI_effect_error(self.id) };
        if p.is_null() {
            return String::new();
        }
        // SAFETY: as above; copied out before anything can free the effect.
        unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
    }
}

impl Drop for ShaderEffect {
    fn drop(&mut self) {
        // SAFETY: the effect is ours; a canvas that still names it draws its fallback.
        unsafe { sys::BUI_effect_free(self.id) };
    }
}

/// A GPU device without a window: effects drawn into an off-screen frame and read back as pixels.
/// For tests, thumbnails and measuring a shader; a window draws effects through its canvas
/// ([`Painter::effect`](crate::Painter::effect)).
pub struct GpuTarget {
    raster: std::ptr::NonNull<sys::BuiRaster>,
    size: (usize, usize),
    _thread: PhantomData<*const ()>,
}

impl GpuTarget {
    /// A `width` x `height` target. `None` when there is no GPU device (no Direct3D 11 / OpenGL 3.3,
    /// or the `gpu` feature is off) or the size is empty or beyond 16384.
    #[must_use]
    pub fn new(width: usize, height: usize) -> Option<Self> {
        if width == 0 || height == 0 || width > 16384 || height > 16384 {
            return None;
        }
        // SAFETY: a fresh raster of that size, ours until Drop.
        let raster = std::ptr::NonNull::new(unsafe { sys::BUI_raster_create(width as c_int, height as c_int) })?;
        let target = Self { raster, size: (width, height), _thread: PhantomData };
        // SAFETY: a null window renders off-screen; the call waits for the device.
        (unsafe { sys::BUI_raster_gpu_attach(raster.as_ptr(), std::ptr::null_mut(), 1) } != 0).then_some(target)
    }

    /// Draw `effect` over the whole target (see [`Painter::effect`](crate::Painter::effect) for
    /// `inputs` and `floats`) without waiting for the GPU. `false` when it cannot run (see
    /// [`ShaderEffect::error`]); the frame is then empty.
    pub fn draw(&mut self, effect: &ShaderEffect, inputs: &[i32], floats: &[f32]) -> bool {
        let mut slots = [0; sys::BK_EFFECT_MAX_INPUTS];
        for (slot, id) in slots.iter_mut().zip(inputs) {
            *slot = *id;
        }
        let (w, h) = self.size;
        let n = floats.len().min(ShaderEffect::MAX_FLOATS);
        // SAFETY: the raster is live; the arrays outlive the call (the kit copies the floats).
        unsafe {
            sys::BUI_raster_begin_frame(self.raster.as_ptr(), w as c_int, h as c_int);
            let ok = sys::BUI_raster_effect(self.raster.as_ptr(), 0.0, 0.0, w as f32, h as f32, effect.id(),
                                            slots.as_ptr(), floats.as_ptr(), n as c_int);
            sys::BUI_raster_end_frame(self.raster.as_ptr());
            ok != 0
        }
    }

    /// The last drawn frame as `0x00RRGGBB` rows (waits for the GPU).
    #[must_use]
    pub fn pixels(&self) -> &[u32] {
        let (w, h) = self.size;
        // SAFETY: the kit returns `w * h` pixels owned by the raster; they stay valid until the next frame.
        unsafe { std::slice::from_raw_parts(sys::BUI_raster_pixels(self.raster.as_ptr()), w * h) }
    }

    /// The size it was made with.
    #[must_use]
    pub const fn size(&self) -> (usize, usize) {
        self.size
    }
}

impl Drop for GpuTarget {
    fn drop(&mut self) {
        // SAFETY: the raster is ours; the device goes with it.
        unsafe { sys::BUI_raster_free(self.raster.as_ptr()) };
    }
}
