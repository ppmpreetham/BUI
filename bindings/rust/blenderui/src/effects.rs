//! CSS effects that work on an element's pixels: `filter`,
//! `backdrop-filter`, `mix-blend-mode`, `clip-path`, `mask`, and transforms
//! beyond `translate` (`rotate`, `skew`, `scale`, `matrix`).
//!
//! The element's subtree is painted into its own display list, rasterized
//! offscreen with the canvas' own item code (`BK_canvas_rasterize`), then
//! filtered, clipped, masked, transformed and composited here, and drawn back
//! as one image item. That works the same under the CPU and GPU renderers;
//! results are cached by content relative to where the layer is, so a page
//! that doesn't change costs nothing, nor does a layer that only moves.
//!
//! A transform that only moves and scales (no turn, skew or mirror) needs no
//! pixels: what the element and its subtree paint is mapped through the
//! matrix instead ([`Effects::vector`]), so scaled text stays vector text,
//! drawn by the renderer at its size, and an animated scale costs what an
//! animated colour does.
//!
//! The formulas are the W3C ones (Filter Effects 1, Compositing and Blending
//! 1), which is what browsers implement. Blur is a three-pass box blur, the
//! Gaussian approximation browsers use; a wide one runs on a smaller copy
//! ([`blur`]). Resampling is bilinear over premultiplied pixels, like
//! Blender's `IMB_transform`.

use crate::color::Rgba;
use crate::geometry::{Bounds, Length, Units};
use crate::style::{BoxEdge, Gradient, Pos};

// ==========================================================================
// Style values

/// One `filter` / `backdrop-filter` function. Amounts are fractions
/// (`50%` = 0.5); `hue-rotate` in degrees; lengths in px.
#[derive(Clone, Debug, PartialEq)]
pub enum Filter {
    Blur(f32),
    Brightness(f32),
    Contrast(f32),
    Grayscale(f32),
    HueRotate(f32),
    Invert(f32),
    Opacity(f32),
    Saturate(f32),
    Sepia(f32),
    DropShadow { x: f32, y: f32, blur: f32, color: Rgba },
}

/// `mix-blend-mode` (and `background-blend-mode`).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum Blend {
    #[default]
    Normal,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
    Hue,
    Saturation,
    Color,
    Luminosity,
    PlusLighter,
}

impl Blend {
    pub fn parse(s: &str) -> Option<Blend> {
        Some(match s {
            "normal" => Blend::Normal,
            "multiply" => Blend::Multiply,
            "screen" => Blend::Screen,
            "overlay" => Blend::Overlay,
            "darken" => Blend::Darken,
            "lighten" => Blend::Lighten,
            "color-dodge" => Blend::ColorDodge,
            "color-burn" => Blend::ColorBurn,
            "hard-light" => Blend::HardLight,
            "soft-light" => Blend::SoftLight,
            "difference" => Blend::Difference,
            "exclusion" => Blend::Exclusion,
            "hue" => Blend::Hue,
            "saturation" => Blend::Saturation,
            "color" => Blend::Color,
            "luminosity" => Blend::Luminosity,
            "plus-lighter" => Blend::PlusLighter,
            _ => return None,
        })
    }
}

/// One `transform` function.
#[derive(Clone, Debug, PartialEq)]
pub enum TransformFn {
    Translate(Length, Length),
    Scale(f32, f32),
    /// radians
    Rotate(f32),
    /// radians
    Skew(f32, f32),
    Matrix([f32; 6]),
}

/// A `clip-path` shape radius: a length, or `closest-side` / `farthest-side`.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum ShapeRadius {
    Len(Length),
    Closest,
    Farthest,
}

/// A `clip-path` basic shape, in its reference box.
#[derive(Clone, Debug, PartialEq)]
pub enum ClipShape {
    /// top, right, bottom, left insets and a corner radius
    Inset([Length; 4], Length),
    Circle(ShapeRadius, [Pos; 2]),
    Ellipse(ShapeRadius, ShapeRadius, [Pos; 2]),
    Polygon(Vec<(Length, Length)>),
    /// just the reference box (`clip-path: padding-box`)
    Box,
}

/// A mask or border-image source: a gradient or a PNG file.
#[derive(Clone, Debug, PartialEq)]
pub enum ImageSource {
    Gradient(Gradient),
    Url(std::sync::Arc<str>),
}

/// `mask` / `mask-image` and its placement.
#[derive(Clone, Debug, PartialEq)]
pub struct Mask {
    pub image: Option<ImageSource>,
    /// `mask-mode: luminance` (else alpha)
    pub luminance: bool,
    pub size: crate::style::BgSize,
    pub position: [Pos; 2],
    pub repeat: (bool, bool),
}

impl Default for Mask {
    fn default() -> Mask {
        Mask { image: None, luminance: false, size: crate::style::BgSize::Auto, position: [Pos::START; 2],
               repeat: (true, true) }
    }
}

/// A `border-image-slice` / `-width` / `-outset` component.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Side {
    /// a plain number: image pixels (slice) or multiples of the border width
    Num(f32),
    Len(Length),
    Auto,
}

/// `border-image-repeat` along one axis.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum Tile {
    #[default]
    Stretch,
    Repeat,
    Round,
    Space,
}

/// `border-image` and its longhands.
#[derive(Clone, Debug, PartialEq)]
pub struct BorderImage {
    pub source: Option<ImageSource>,
    pub slice: [Side; 4],
    pub fill: bool,
    pub width: [Side; 4],
    pub outset: [Side; 4],
    pub repeat: (Tile, Tile),
}

impl Default for BorderImage {
    fn default() -> BorderImage {
        BorderImage { source: None, slice: [Side::Len(Length::Percent(100.0)); 4], fill: false,
                      width: [Side::Num(1.0); 4], outset: [Side::Num(0.0); 4], repeat: (Tile::Stretch, Tile::Stretch) }
    }
}

/// The effects of one element (boxed in `Computed`: most elements have none).
#[derive(Clone, Debug, PartialEq, Default)]
pub struct Effects {
    pub filter: Vec<Filter>,
    pub backdrop: Vec<Filter>,
    pub blend: Blend,
    pub isolate: bool,
    pub clip_path: Option<(ClipShape, BoxEdge)>,
    pub mask: Option<Mask>,
    /// `transform` functions that need pixels (anything but translate)
    pub transform: Vec<TransformFn>,
    /// the individual `rotate` property, radians
    pub rotate: f32,
    /// the individual `scale` property when it isn't uniform
    pub scale2: Option<(f32, f32)>,
    /// `transform-origin` (default: the centre)
    pub origin: Option<[Pos; 2]>,
    pub border_image: Option<BorderImage>,
}

impl Effects {
    /// Whether the element must be painted into its own layer (`b`: its box):
    /// effects on pixels, or a transform that is more than a move and a scale.
    pub fn needs_layer(&self, b: Bounds<f32>, u: &Units) -> bool {
        self.needs_pixels() || (self.has_transform() && self.vector(b, u).is_none())
    }

    /// Filters, blending, a clip path or a mask: they work on pixels.
    pub fn needs_pixels(&self) -> bool {
        !self.filter.is_empty() || self.blend != Blend::Normal || self.clip_path.is_some()
            || self.mask.as_ref().is_some_and(|m| m.image.is_some())
    }

    /// `transform` (beyond translate), `rotate` or a non-uniform `scale`.
    pub fn has_transform(&self) -> bool {
        !self.transform.is_empty() || self.rotate != 0.0 || self.scale2.is_some()
    }

    /// The element's transform for box `b`, in logical pixels of the page,
    /// about its `transform-origin` (identity: none).
    pub fn matrix(&self, b: Bounds<f32>, u: &Units) -> Affine {
        if !self.has_transform() {
            return IDENTITY;
        }
        let mut m = IDENTITY;
        if self.rotate != 0.0 {
            m = mul(&m, &[self.rotate.cos(), self.rotate.sin(), -self.rotate.sin(), self.rotate.cos(), 0.0, 0.0]);
        }
        if let Some((sx, sy)) = self.scale2 {
            m = mul(&m, &[sx, 0.0, 0.0, sy, 0.0, 0.0]);
        }
        if !self.transform.is_empty() {
            m = mul(&m, &matrix_of(&self.transform, b.size.width, b.size.height, u));
        }
        let origin = self.origin.unwrap_or([Pos::CENTER; 2]);
        let (ox, oy) = (b.origin.x + origin[0].at(b.size.width), b.origin.y + origin[1].at(b.size.height));
        mul(&mul(&[1.0, 0.0, 0.0, 1.0, ox, oy], &m), &[1.0, 0.0, 0.0, 1.0, -ox, -oy])
    }

    /// The transform as a vector one: its matrix when it only moves and
    /// scales (positive, axis-aligned) and nothing needs pixels; `None` for a
    /// turn, a skew or a mirror, which take a layer.
    pub fn vector(&self, b: Bounds<f32>, u: &Units) -> Option<Affine> {
        if self.needs_pixels() || !self.has_transform() {
            return None;
        }
        let m = self.matrix(b, u);
        (m[1].abs() < AXIS_EPSILON && m[2].abs() < AXIS_EPSILON && m[0] > 0.0 && m[3] > 0.0).then_some(m)
    }
}

/// How far off the axes a matrix may be and still move and scale only
/// (rounding of a turn by 0 or 360 degrees).
pub(crate) const AXIS_EPSILON: f32 = 1e-5;

/// The shader that turns (rotates, skews, mirrors) a layer on the GPU: each
/// pixel of the box goes back through the inverse matrix (`U[0..6]`, box
/// pixels to texels, see [`turn_box`]) to the layer's texture, sampled
/// bilinearly, the last half texel at its edges faded out (antialiasing),
/// times the layer's opacity (`U[6]`). The texture holds premultiplied texels
/// ([`turn_texture`]), so the bilinear mix is the CPU's; `input_color` takes
/// texels as straight and multiplies by alpha again, which is undone here (a
/// straight texture mixed bilinearly darkens the edges it fades out).
#[doc(hidden)]
pub const TURN_SHADER: &str = "vec4 effect(vec2 p) {
    vec2 q = vec2(U[0] * p.x + U[2] * p.y + U[4], U[1] * p.x + U[3] * p.y + U[5]);
    vec2 inside = clamp(min(q, input_size(0) - q) + 0.5, 0.0, 1.0);
    vec4 c = input_color(0, q);
    c.rgb = c.rgb / max(c.a, 0.000001);
    return c * (inside.x * inside.y * U[6]);
}";

/// A BUI texture of a layer for [`TURN_SHADER`]: its premultiplied texels as
/// they are (RGBA bytes), not made straight. 0 on failure.
#[doc(hidden)]
#[must_use]
pub fn turn_texture(a: &Argb) -> i32 {
    if a.w == 0 || a.h == 0 || a.px.len() < a.w * a.h {
        return 0;
    }
    let rgba: Vec<u8> = a.px[..a.w * a.h].iter().flat_map(|&p| [(p >> 16) as u8, (p >> 8) as u8, p as u8, (p >> 24) as u8]).collect();
    // SAFETY: `rgba` holds `w * h * 4` bytes, read during the call.
    unsafe { crate::sys::BUI_texture_create(a.w as i32, a.h as i32, rgba.as_ptr().cast(), 0) }
}

/// Where a turned layer lands, and how a pixel of that box finds its texel:
/// for a layer image of `size` pixels whose top left is `origin` (layer device
/// pixels, `layer_scale` of them per logical pixel) turned by `m` (in layer
/// device pixels), in a window of `scale` device pixels per logical one. The
/// box in logical pixels, and the matrix of [`TURN_SHADER`] (box device pixels,
/// from its corner, to texels). `None` for a matrix that flattens it.
#[doc(hidden)]
#[must_use]
pub fn turn_box(origin: (f32, f32), size: (f32, f32), m: &Affine, layer_scale: f32, scale: f32)
                -> Option<(Bounds<f32>, Affine)> {
    let inverse = invert(m)?;
    let (x0, y0, x1, y1) = (origin.0, origin.1, origin.0 + size.0, origin.1 + size.1);
    let corners = [(x0, y0), (x1, y0), (x0, y1), (x1, y1)].map(|(x, y)| apply(m, x, y));
    let (lx0, ly0) = corners.iter().fold((f32::MAX, f32::MAX), |(a, b), c| (a.min(c.0), b.min(c.1)));
    let (lx1, ly1) = corners.iter().fold((f32::MIN, f32::MIN), |(a, b), c| (a.max(c.0), b.max(c.1)));
    let at = crate::geometry::rect(lx0 / layer_scale, ly0 / layer_scale, (lx1 - lx0) / layer_scale, (ly1 - ly0) / layer_scale);
    // a box pixel to layer device pixels, back through the matrix, then from
    // the texture's corner
    let to_layer = [layer_scale / scale, 0.0, 0.0, layer_scale / scale, lx0, ly0];
    Some((at, mul(&mul(&[1.0, 0.0, 0.0, 1.0, -origin.0, -origin.1], &inverse), &to_layer)))
}

thread_local! {
    /// Layers this thread rasterized (cache misses).
    static DRAWN: std::cell::Cell<u64> = const { std::cell::Cell::new(0) };
}

/// A layer was rasterized (not found in the cache).
pub(crate) fn count_layer() {
    DRAWN.set(DRAWN.get() + 1);
}

/// How many layers this thread has rasterized and filtered so far (a layer
/// found in the cache costs nothing and is not counted): for tests and
/// benchmarks.
#[doc(hidden)]
#[must_use]
pub fn layers_drawn() -> u64 {
    DRAWN.get()
}

// ==========================================================================
// Affine transforms: [a, b, c, d, e, f] maps (x, y) to (a x + c y + e, b x + d y + f)

pub type Affine = [f32; 6];

pub const IDENTITY: Affine = [1.0, 0.0, 0.0, 1.0, 0.0, 0.0];

/// `m * n`: n applied first.
pub fn mul(m: &Affine, n: &Affine) -> Affine {
    [
        m[0] * n[0] + m[2] * n[1],
        m[1] * n[0] + m[3] * n[1],
        m[0] * n[2] + m[2] * n[3],
        m[1] * n[2] + m[3] * n[3],
        m[0] * n[4] + m[2] * n[5] + m[4],
        m[1] * n[4] + m[3] * n[5] + m[5],
    ]
}

pub fn apply(m: &Affine, x: f32, y: f32) -> (f32, f32) {
    (m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5])
}

pub fn invert(m: &Affine) -> Option<Affine> {
    let det = m[0] * m[3] - m[1] * m[2];
    if det.abs() < 1e-9 {
        return None;
    }
    let (a, b, c, d) = (m[3] / det, -m[1] / det, -m[2] / det, m[0] / det);
    Some([a, b, c, d, -(a * m[4] + c * m[5]), -(b * m[4] + d * m[5])])
}

/// The matrix of a function list for a box of size `w` x `h` (percentages
/// of translate resolve against it), before the origin is applied.
pub fn matrix_of(fns: &[TransformFn], w: f32, h: f32, u: &Units) -> Affine {
    let mut m = IDENTITY;
    for f in fns {
        let n = match *f {
            TransformFn::Translate(x, y) => [1.0, 0.0, 0.0, 1.0, x.resolve(u, w), y.resolve(u, h)],
            TransformFn::Scale(x, y) => [x, 0.0, 0.0, y, 0.0, 0.0],
            TransformFn::Rotate(a) => [a.cos(), a.sin(), -a.sin(), a.cos(), 0.0, 0.0],
            TransformFn::Skew(ax, ay) => [1.0, ay.tan(), ax.tan(), 1.0, 0.0, 0.0],
            TransformFn::Matrix(v) => v,
        };
        m = mul(&m, &n);
    }
    m
}

// ==========================================================================
// Pixels: premultiplied linear-in-sRGB f32 RGBA, row-major

pub struct Pixels {
    pub w: usize,
    pub h: usize,
    pub px: Vec<[f32; 4]>,
}

impl Pixels {
    pub fn new(w: usize, h: usize) -> Pixels {
        Pixels { w, h, px: vec![[0.0; 4]; w * h] }
    }

    /// From straight-alpha RGBA8.
    pub fn from_rgba8(w: usize, h: usize, rgba: &[u8]) -> Pixels {
        let px = rgba.chunks_exact(4).map(|c| {
            let a = c[3] as f32 / 255.0;
            [c[0] as f32 / 255.0 * a, c[1] as f32 / 255.0 * a, c[2] as f32 / 255.0 * a, a]
        }).collect();
        Pixels { w, h, px }
    }

    /// To straight-alpha RGBA8.
    pub fn to_rgba8(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(self.px.len() * 4);
        for p in &self.px {
            let a = p[3].clamp(0.0, 1.0);
            let un = |v: f32| if a > 0.0 { (v / a).clamp(0.0, 1.0) } else { 0.0 };
            out.extend([(un(p[0]) * 255.0).round() as u8, (un(p[1]) * 255.0).round() as u8,
                        (un(p[2]) * 255.0).round() as u8, (a * 255.0).round() as u8]);
        }
        out
    }

    fn get(&self, x: isize, y: isize) -> [f32; 4] {
        if x < 0 || y < 0 || x >= self.w as isize || y >= self.h as isize {
            [0.0; 4]
        } else {
            self.px[y as usize * self.w + x as usize]
        }
    }

    /// Bilinear sample at a pixel-space position (pixel centres at +0.5).
    pub fn sample(&self, x: f32, y: f32) -> [f32; 4] {
        let (fx, fy) = (x - 0.5, y - 0.5);
        let (x0, y0) = (fx.floor(), fy.floor());
        let (tx, ty) = (fx - x0, fy - y0);
        let (x0, y0) = (x0 as isize, y0 as isize);
        let (a, b, c, d) = (self.get(x0, y0), self.get(x0 + 1, y0), self.get(x0, y0 + 1), self.get(x0 + 1, y0 + 1));
        let mut o = [0.0; 4];
        for k in 0..4 {
            let top = a[k] + (b[k] - a[k]) * tx;
            let bot = c[k] + (d[k] - c[k]) * tx;
            o[k] = top + (bot - top) * ty;
        }
        o
    }

    /// From premultiplied ARGB words (no division: both are premultiplied).
    pub fn from_argb(a: &Argb) -> Pixels {
        const K: f32 = 1.0 / 255.0;
        let px = a.px.iter().map(|&p| {
            [((p >> 16) & 255) as f32 * K, ((p >> 8) & 255) as f32 * K, (p & 255) as f32 * K, (p >> 24) as f32 * K]
        }).collect();
        Pixels { w: a.w, h: a.h, px }
    }

    /// To premultiplied ARGB words (a channel never above alpha).
    pub fn to_argb(&self) -> Argb {
        let q = |v: f32| (v.clamp(0.0, 1.0) * 255.0 + 0.5) as u32;
        let px = self.px.iter().map(|p| {
            let a = q(p[3]);
            a << 24 | q(p[0]).min(a) << 16 | q(p[1]).min(a) << 8 | q(p[2]).min(a)
        }).collect();
        Argb { w: self.w, h: self.h, px }
    }
}

/// Premultiplied ARGB words, row-major: the kit's image format, which a
/// layer is rasterized to and drawn from (`BK_canvas_rasterize`,
/// `BK_imbuf_from_argb`), with no conversion on either side. A layer whose
/// filters are blurs stays in it end to end ([`blur_argb`]); other effects
/// work on [`Pixels`].
#[derive(Clone, Debug, Default)]
pub struct Argb {
    pub w: usize,
    pub h: usize,
    pub px: Vec<u32>,
}

impl Argb {
    /// This raster at (`x`, `y`) in a transparent `w` x `h` one (the margin a
    /// filter spreads into); clipped to it.
    pub fn padded(&self, w: usize, h: usize, (x, y): (usize, usize)) -> Argb {
        let mut out = Argb { w, h, px: vec![0; w * h] };
        let cols = self.w.min(w.saturating_sub(x));
        if cols == 0 || y >= h {
            return out; // nothing of it lands inside
        }
        for (row, line) in self.px.chunks_exact(self.w).enumerate().take(h.saturating_sub(y)) {
            out.px[(y + row) * w + x..][..cols].copy_from_slice(&line[..cols]);
        }
        out
    }

    /// Every channel times `o` (0..1): a layer's opacity.
    pub fn fade(&mut self, o: f32) {
        let k = (o.clamp(0.0, 1.0) * 256.0) as u32;
        for p in &mut self.px {
            // each byte times k / 256, all four at once in two lanes
            let rb = (((*p & 0x00ff_00ff) * k) >> 8) & 0x00ff_00ff;
            let ag = ((*p >> 8 & 0x00ff_00ff) * k) & 0xff00_ff00;
            *p = ag | rb;
        }
    }
}

/// Gaussian blur of premultiplied ARGB with standard deviation `sigma`
/// (device pixels): the three box blurs of [`blur`], in integers, as Ivan
/// Kutskir's "fastest Gaussian blur" lays them out for SIMD. The four
/// channels are split into planes of bytes; every pass runs down the
/// columns, a whole row at a time (`sums[x] += row[y + r][x] - row[y - r -
/// 1][x]`: straight loops over arrays, which the compiler vectorizes), with
/// the division a multiply and a shift; the horizontal passes are the same
/// passes on the transposed plane. The work per pixel does not grow with the
/// radius. A wide blur runs on a smaller copy, as [`blur`] does.
pub fn blur_argb(p: &mut Argb, sigma: f32) {
    if sigma < 0.3 || p.w == 0 || p.h == 0 {
        return;
    }
    if let Some(k) = downsample_factor(sigma, p.w, p.h) {
        // the same path as `blur`: down, blur, up
        let mut f = Pixels::from_argb(p);
        blur_small(&mut f, sigma, k);
        *p = f.to_argb();
        return;
    }
    let (w, h) = (p.w, p.h);
    let radii = boxes(sigma).map(|b| (b - 1) / 2);
    let mut planes: [Vec<u8>; 4] = std::array::from_fn(|c| {
        let shift = 24 - 8 * c;
        p.px.iter().map(|&q| (q >> shift) as u8).collect()
    });
    let (mut tmp, mut sums) = (vec![0u8; w * h], Vec::new());
    for plane in &mut planes {
        for &r in &radii {
            box_down(plane, &mut tmp, w, h, r, &mut sums);
            std::mem::swap(plane, &mut tmp);
        }
        transpose(plane, &mut tmp, w, h);
        for &r in &radii {
            box_down(&tmp, plane, h, w, r, &mut sums);
            std::mem::swap(plane, &mut tmp);
        }
        transpose(&tmp, plane, h, w);
    }
    for (i, q) in p.px.iter_mut().enumerate() {
        *q = u32::from(planes[0][i]) << 24 | u32::from(planes[1][i]) << 16 | u32::from(planes[2][i]) << 8
            | u32::from(planes[3][i]);
    }
}

/// A box blur of radius `r` down the columns of a `w` x `h` plane
/// (transparent beyond its ends), a row at a time over a row of sums.
fn box_down(src: &[u8], dst: &mut [u8], w: usize, h: usize, r: usize, sums: &mut Vec<u32>) {
    // sum * mul >> 24 divides by the window: 255 * 2^24 still fits in u32
    let mul = (1u32 << 24) / (2 * r as u32 + 1);
    sums.clear();
    sums.resize(w, 0);
    let row = |y: usize| &src[y * w..(y + 1) * w];
    for y in 0..=r.min(h - 1) {
        sums.iter_mut().zip(row(y)).for_each(|(s, &v)| *s += u32::from(v));
    }
    for y in 0..h {
        dst[y * w..(y + 1) * w].iter_mut().zip(sums.iter()).for_each(|(o, &s)| {
            *o = ((s * mul + (1 << 23)) >> 24) as u8;
        });
        if y + r + 1 < h {
            sums.iter_mut().zip(row(y + r + 1)).for_each(|(s, &v)| *s += u32::from(v));
        }
        if y >= r {
            sums.iter_mut().zip(row(y - r)).for_each(|(s, &v)| *s -= u32::from(v));
        }
    }
}

/// `dst` (`h` x `w`) is `src` (`w` x `h`) transposed, in cache-sized tiles.
fn transpose(src: &[u8], dst: &mut [u8], w: usize, h: usize) {
    const TILE: usize = 32;
    for y0 in (0..h).step_by(TILE) {
        for x0 in (0..w).step_by(TILE) {
            for y in y0..(y0 + TILE).min(h) {
                for x in x0..(x0 + TILE).min(w) {
                    dst[x * h + y] = src[y * w + x];
                }
            }
        }
    }
}

// -- filters ---------------------------------------------------------------------

/// Box sizes for three box blurs approximating a Gaussian of `sigma`.
fn boxes(sigma: f32) -> [usize; 3] {
    let n = 3.0;
    let w_ideal = (12.0 * sigma * sigma / n + 1.0).sqrt();
    let mut wl = w_ideal.floor() as i32;
    if wl % 2 == 0 {
        wl -= 1;
    }
    let wu = wl + 2;
    let m_ideal = (12.0 * sigma * sigma - n * (wl * wl) as f32 - 4.0 * n * wl as f32 - 3.0 * n)
        / (-4.0 * wl as f32 - 4.0);
    let m = m_ideal.round() as i32;
    let mut out = [0usize; 3];
    for (i, o) in out.iter_mut().enumerate() {
        *o = (if (i as i32) < m { wl } else { wu }).max(1) as usize;
    }
    out
}

fn box_pass(src: &[[f32; 4]], dst: &mut [[f32; 4]], w: usize, h: usize, r: usize, horizontal: bool) {
    let (len, lines) = if horizontal { (w, h) } else { (h, w) };
    let idx = |line: usize, i: usize| if horizontal { line * w + i } else { i * w + line };
    let norm = 1.0 / (2 * r + 1) as f32;
    for line in 0..lines {
        let mut acc = [0.0f32; 4];
        // window over [i - r, i + r], transparent outside
        for i in 0..=r.min(len.saturating_sub(1)) {
            let p = src[idx(line, i)];
            for k in 0..4 {
                acc[k] += p[k];
            }
        }
        for i in 0..len {
            let o = &mut dst[idx(line, i)];
            for k in 0..4 {
                o[k] = acc[k] * norm;
            }
            if i + r + 1 < len {
                let p = src[idx(line, i + r + 1)];
                for k in 0..4 {
                    acc[k] += p[k];
                }
            }
            if i >= r {
                let p = src[idx(line, i - r)];
                for k in 0..4 {
                    acc[k] -= p[k];
                }
            }
        }
    }
}

/// From this sigma (device pixels) on, a blur runs on a smaller copy: the
/// result is smooth enough that a box-filtered copy, blurred and scaled back
/// up bilinearly, looks the same (what GPU "dual filter" blurs do). The work
/// falls with the square of the factor: `blur(64px)` on a cover costs
/// hundreds of times less.
pub(crate) const DOWNSAMPLE_FROM: f32 = 6.0;
/// The sigma the smaller copy is blurred at; the factor follows from it.
pub(crate) const SMALL_SIGMA: f32 = 3.0;

/// How many times smaller a blur of `sigma` runs on a `w` x `h` image (for
/// [`blur`] and [`blur_argb`] alike); `None`: at full size.
fn downsample_factor(sigma: f32, w: usize, h: usize) -> Option<usize> {
    let k = (sigma / SMALL_SIGMA).floor() as usize;
    (sigma >= DOWNSAMPLE_FROM && k >= 2 && w >= 2 * k && h >= 2 * k).then_some(k)
}

/// Gaussian blur with standard deviation `sigma` (device pixels).
pub fn blur(p: &mut Pixels, sigma: f32) {
    if sigma < 0.3 || p.w == 0 || p.h == 0 {
        return;
    }
    match downsample_factor(sigma, p.w, p.h) {
        Some(k) => blur_small(p, sigma, k),
        None => box_blur(p, sigma),
    }
}

/// The blur of `sigma` on a copy `k` times smaller, scaled back up into `p`.
fn blur_small(p: &mut Pixels, sigma: f32, k: usize) {
    let mut small = downsample(p, k);
    // the box filter down and the bilinear filter up blur too (variances
    // (k^2 - 1) / 12 and k^2 / 6): the small copy takes what is left
    let kf = k as f32;
    let rest = (sigma * sigma - (kf * kf - 1.0) / 12.0 - kf * kf / 6.0).max(0.25).sqrt();
    box_blur(&mut small, rest / kf);
    upsample(&small, p, k);
}

/// `p` averaged over `k` x `k` blocks (transparent beyond its edges).
fn downsample(p: &Pixels, k: usize) -> Pixels {
    let (w, h) = (p.w.div_ceil(k), p.h.div_ceil(k));
    let mut out = Pixels::new(w, h);
    let norm = 1.0 / (k * k) as f32;
    for (y, row) in p.px.chunks_exact(p.w).enumerate() {
        let line = &mut out.px[(y / k) * w..(y / k + 1) * w];
        for (x, px) in row.iter().enumerate() {
            let o = &mut line[x / k];
            for c in 0..4 {
                o[c] += px[c] * norm;
            }
        }
    }
    out
}

/// `small` scaled up `k` times into `p`, bilinearly (block centres line up).
fn upsample(small: &Pixels, p: &mut Pixels, k: usize) {
    let inv = 1.0 / k as f32;
    for y in 0..p.h {
        let sy = (y as f32 + 0.5) * inv;
        for x in 0..p.w {
            p.px[y * p.w + x] = small.sample((x as f32 + 0.5) * inv, sy);
        }
    }
}

/// Gaussian blur as three box blurs, at full size.
fn box_blur(p: &mut Pixels, sigma: f32) {
    if sigma < 0.3 {
        return;
    }
    let mut tmp = vec![[0.0f32; 4]; p.px.len()];
    for b in boxes(sigma) {
        let r = (b - 1) / 2;
        box_pass(&p.px, &mut tmp, p.w, p.h, r, true);
        box_pass(&tmp, &mut p.px, p.w, p.h, r, false);
    }
}

/// A 4x5 colour matrix (rows r, g, b, a; columns r g b a offset) over
/// unpremultiplied colour, as Filter Effects' feColorMatrix.
fn color_matrix(p: &mut Pixels, m: &[[f32; 5]; 4]) {
    for px in &mut p.px {
        let a = px[3];
        if a <= 0.0 {
            continue;
        }
        let c = [px[0] / a, px[1] / a, px[2] / a, a];
        let mut o = [0.0f32; 4];
        for (r, row) in m.iter().enumerate() {
            o[r] = (row[0] * c[0] + row[1] * c[1] + row[2] * c[2] + row[3] * c[3] + row[4]).clamp(0.0, 1.0);
        }
        *px = [o[0] * o[3], o[1] * o[3], o[2] * o[3], o[3]];
    }
}

fn diag(r: f32, g: f32, b: f32, a: f32, off: f32) -> [[f32; 5]; 4] {
    [[r, 0.0, 0.0, 0.0, off], [0.0, g, 0.0, 0.0, off], [0.0, 0.0, b, 0.0, off], [0.0, 0.0, 0.0, a, 0.0]]
}

fn rgb3(m: [[f32; 3]; 3]) -> [[f32; 5]; 4] {
    [[m[0][0], m[0][1], m[0][2], 0.0, 0.0], [m[1][0], m[1][1], m[1][2], 0.0, 0.0],
     [m[2][0], m[2][1], m[2][2], 0.0, 0.0], [0.0, 0.0, 0.0, 1.0, 0.0]]
}

/// Apply one filter function; `scale` = device pixels per CSS px.
pub fn apply_filter(p: &mut Pixels, f: &Filter, scale: f32) {
    match *f {
        Filter::Blur(r) => blur(p, r * scale),
        Filter::Brightness(b) => color_matrix(p, &diag(b, b, b, 1.0, 0.0)),
        Filter::Contrast(c) => color_matrix(p, &diag(c, c, c, 1.0, 0.5 - 0.5 * c)),
        Filter::Opacity(o) => p.px.iter_mut().for_each(|px| px.iter_mut().for_each(|v| *v *= o.clamp(0.0, 1.0))),
        Filter::Invert(a) => {
            let a = a.clamp(0.0, 1.0);
            color_matrix(p, &diag(1.0 - 2.0 * a, 1.0 - 2.0 * a, 1.0 - 2.0 * a, 1.0, a))
        }
        Filter::Grayscale(a) => {
            let s = 1.0 - a.clamp(0.0, 1.0);
            color_matrix(p, &rgb3([
                [0.2126 + 0.7874 * s, 0.7152 - 0.7152 * s, 0.0722 - 0.0722 * s],
                [0.2126 - 0.2126 * s, 0.7152 + 0.2848 * s, 0.0722 - 0.0722 * s],
                [0.2126 - 0.2126 * s, 0.7152 - 0.7152 * s, 0.0722 + 0.9278 * s],
            ]))
        }
        Filter::Sepia(a) => {
            let s = 1.0 - a.clamp(0.0, 1.0);
            color_matrix(p, &rgb3([
                [0.393 + 0.607 * s, 0.769 - 0.769 * s, 0.189 - 0.189 * s],
                [0.349 - 0.349 * s, 0.686 + 0.314 * s, 0.168 - 0.168 * s],
                [0.272 - 0.272 * s, 0.534 - 0.534 * s, 0.131 + 0.869 * s],
            ]))
        }
        Filter::Saturate(s) => color_matrix(p, &rgb3([
            [0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s],
            [0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s],
            [0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s],
        ])),
        Filter::HueRotate(deg) => {
            let (c, s) = (deg.to_radians().cos(), deg.to_radians().sin());
            color_matrix(p, &rgb3([
                [0.213 + c * 0.787 - s * 0.213, 0.715 - c * 0.715 - s * 0.715, 0.072 - c * 0.072 + s * 0.928],
                [0.213 - c * 0.213 + s * 0.143, 0.715 + c * 0.285 + s * 0.140, 0.072 - c * 0.072 - s * 0.283],
                [0.213 - c * 0.213 - s * 0.787, 0.715 - c * 0.715 + s * 0.715, 0.072 + c * 0.928 + s * 0.072],
            ]))
        }
        Filter::DropShadow { x, y, blur: b, color } => {
            // the source's alpha, coloured, blurred and offset, under the source
            let (dx, dy) = ((x * scale).round() as isize, (y * scale).round() as isize);
            let mut sh = Pixels::new(p.w, p.h);
            for yy in 0..p.h as isize {
                for xx in 0..p.w as isize {
                    let a = p.get(xx - dx, yy - dy)[3] * color.a;
                    sh.px[yy as usize * p.w + xx as usize] = [color.r * a, color.g * a, color.b * a, a];
                }
            }
            blur(&mut sh, b * scale / 2.0);
            for (s, d) in p.px.iter_mut().zip(&sh.px) {
                let ia = 1.0 - s[3];
                for k in 0..4 {
                    s[k] += d[k] * ia;
                }
            }
        }
    }
}

/// How far a filter list can spread pixels past the element (CSS px).
pub fn filter_outset(filters: &[Filter]) -> f32 {
    let mut o = 0.0f32;
    for f in filters {
        o += match *f {
            Filter::Blur(r) => 3.0 * r,
            Filter::DropShadow { x, y, blur, .. } => x.abs().max(y.abs()) + 1.5 * blur,
            _ => 0.0,
        };
    }
    o.ceil()
}

// -- compositing (W3C Compositing and Blending 1) ------------------------------------

fn lum(c: [f32; 3]) -> f32 {
    0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]
}

fn clip_color(c: [f32; 3]) -> [f32; 3] {
    let l = lum(c);
    let n = c[0].min(c[1]).min(c[2]);
    let x = c[0].max(c[1]).max(c[2]);
    let mut o = c;
    if n < 0.0 {
        o = o.map(|v| l + (v - l) * l / (l - n).max(1e-6));
    }
    if x > 1.0 {
        o = o.map(|v| l + (v - l) * (1.0 - l) / (x - l).max(1e-6));
    }
    o
}

fn set_lum(c: [f32; 3], l: f32) -> [f32; 3] {
    let d = l - lum(c);
    clip_color([c[0] + d, c[1] + d, c[2] + d])
}

fn sat(c: [f32; 3]) -> f32 {
    c[0].max(c[1]).max(c[2]) - c[0].min(c[1]).min(c[2])
}

fn set_sat(c: [f32; 3], s: f32) -> [f32; 3] {
    let (mx, mn) = (c[0].max(c[1]).max(c[2]), c[0].min(c[1]).min(c[2]));
    if mx - mn <= 1e-6 {
        return [0.0; 3];
    }
    c.map(|v| (v - mn) * s / (mx - mn))
}

fn separable(mode: Blend, cb: f32, cs: f32) -> f32 {
    match mode {
        Blend::Multiply => cb * cs,
        Blend::Screen => cb + cs - cb * cs,
        Blend::Overlay => separable(Blend::HardLight, cs, cb),
        Blend::Darken => cb.min(cs),
        Blend::Lighten => cb.max(cs),
        Blend::ColorDodge => {
            if cb <= 0.0 { 0.0 } else if cs >= 1.0 { 1.0 } else { (cb / (1.0 - cs)).min(1.0) }
        }
        Blend::ColorBurn => {
            if cb >= 1.0 { 1.0 } else if cs <= 0.0 { 0.0 } else { 1.0 - ((1.0 - cb) / cs).min(1.0) }
        }
        Blend::HardLight => {
            if cs <= 0.5 { cb * 2.0 * cs } else { separable(Blend::Screen, cb, 2.0 * cs - 1.0) }
        }
        Blend::SoftLight => {
            if cs <= 0.5 {
                cb - (1.0 - 2.0 * cs) * cb * (1.0 - cb)
            } else {
                let d = if cb <= 0.25 { ((16.0 * cb - 12.0) * cb + 4.0) * cb } else { cb.sqrt() };
                cb + (2.0 * cs - 1.0) * (d - cb)
            }
        }
        Blend::Difference => (cb - cs).abs(),
        Blend::Exclusion => cb + cs - 2.0 * cb * cs,
        _ => cs,
    }
}

/// B(Cb, Cs) for unpremultiplied colours.
fn blend_fn(mode: Blend, cb: [f32; 3], cs: [f32; 3]) -> [f32; 3] {
    match mode {
        Blend::Hue => set_lum(set_sat(cs, sat(cb)), lum(cb)),
        Blend::Saturation => set_lum(set_sat(cb, sat(cs)), lum(cb)),
        Blend::Color => set_lum(cs, lum(cb)),
        Blend::Luminosity => set_lum(cb, lum(cs)),
        _ => [separable(mode, cb[0], cs[0]), separable(mode, cb[1], cs[1]), separable(mode, cb[2], cs[2])],
    }
}

/// Composite `src` over `backdrop` (same size, premultiplied) with `mode`.
pub fn composite(backdrop: &Pixels, src: &Pixels, mode: Blend) -> Pixels {
    let mut out = Pixels::new(src.w, src.h);
    for ((o, b), s) in out.px.iter_mut().zip(&backdrop.px).zip(&src.px) {
        let (ab, as_) = (b[3], s[3]);
        if mode == Blend::PlusLighter {
            *o = [(b[0] + s[0]).min(1.0), (b[1] + s[1]).min(1.0), (b[2] + s[2]).min(1.0), (ab + as_).min(1.0)];
            continue;
        }
        let un = |p: &[f32; 4], a: f32| if a > 0.0 { [p[0] / a, p[1] / a, p[2] / a] } else { [0.0; 3] };
        let (cb, cs) = (un(b, ab), un(s, as_));
        let mixed = blend_fn(mode, cb, cs);
        for k in 0..3 {
            // co = cs (1 - ab) + cb (1 - as) + as ab B(Cb, Cs), premultiplied
            o[k] = s[k] * (1.0 - ab) + b[k] * (1.0 - as_) + as_ * ab * mixed[k];
        }
        o[3] = as_ + ab * (1.0 - as_);
    }
    out
}

/// Multiply every pixel by the coverage in `mask` (alpha, or luminance).
pub fn apply_mask(p: &mut Pixels, mask: &Pixels, luminance: bool) {
    for (d, m) in p.px.iter_mut().zip(&mask.px) {
        let f = if luminance {
            // premultiplied luminance = luminance x alpha
            0.2126 * m[0] + 0.7152 * m[1] + 0.0722 * m[2]
        } else {
            m[3]
        };
        for v in d.iter_mut() {
            *v *= f;
        }
    }
}

/// Resample `src` (whose top-left is at `origin` in device pixels) through
/// the affine `m` (device pixels). Returns the result and its top-left.
pub fn transform(src: &Pixels, origin: (f32, f32), m: &Affine) -> Option<(Pixels, (f32, f32))> {
    let inv = invert(m)?;
    let (x0, y0) = origin;
    let (x1, y1) = (x0 + src.w as f32, y0 + src.h as f32);
    let pts = [apply(m, x0, y0), apply(m, x1, y0), apply(m, x0, y1), apply(m, x1, y1)];
    let minx = pts.iter().map(|p| p.0).fold(f32::INFINITY, f32::min).floor();
    let miny = pts.iter().map(|p| p.1).fold(f32::INFINITY, f32::min).floor();
    let maxx = pts.iter().map(|p| p.0).fold(f32::NEG_INFINITY, f32::max).ceil();
    let maxy = pts.iter().map(|p| p.1).fold(f32::NEG_INFINITY, f32::max).ceil();
    let (w, h) = ((maxx - minx) as usize, (maxy - miny) as usize);
    if w == 0 || h == 0 || w > 8192 || h > 8192 {
        return None;
    }
    let mut out = Pixels::new(w, h);
    for y in 0..h {
        for x in 0..w {
            let (sx, sy) = apply(&inv, minx + x as f32 + 0.5, miny + y as f32 + 0.5);
            out.px[y * w + x] = src.sample(sx - x0, sy - y0);
        }
    }
    Some((out, (minx, miny)))
}

#[cfg(test)]
mod tests {
    use super::{Pixels, blur, blur_argb, box_blur};

    /// A wide blur on a smaller copy matches the full-size one, and takes a
    /// fraction of the time.
    #[test]
    fn a_downsampled_blur_matches_the_full_one() {
        let (w, h) = (240, 160);
        let mut src = Pixels::new(w, h);
        for (i, px) in src.px.iter_mut().enumerate() {
            let (x, y) = (i % w, i / w);
            // squares of colour, with sharp edges
            if (x / 40 + y / 40) % 2 == 0 && (40..200).contains(&x) && (40..120).contains(&y) {
                *px = [0.9, 0.3, 0.1, 1.0];
            }
        }
        for sigma in [6.0, 12.0, 24.0] {
            let (mut fast, mut exact) = (Pixels { w, h, px: src.px.clone() }, Pixels { w, h, px: src.px.clone() });
            blur(&mut fast, sigma);
            box_blur(&mut exact, sigma);
            // away from the edges (both fade to transparent there, a little differently)
            let m = (sigma * 2.0) as usize;
            let worst = (m..h - m)
                .flat_map(|y| (m..w - m).map(move |x| y * w + x))
                .map(|i| (0..4).map(|c| (fast.px[i][c] - exact.px[i][c]).abs()).fold(0.0, f32::max))
                .fold(0.0, f32::max);
            assert!(worst < 0.04, "sigma {sigma}: off by {worst}");
        }
    }

    /// The integer ARGB blur matches the float one (to rounding), and a
    /// layer's opacity scales every channel.
    #[test]
    fn the_argb_blur_matches_the_float_one() {
        let (w, h) = (90, 60);
        let mut src = Pixels::new(w, h);
        for (i, px) in src.px.iter_mut().enumerate() {
            if (20..70).contains(&(i % w)) && (15..45).contains(&(i / w)) {
                *px = [0.6, 0.2, 0.1, 0.8];
            }
        }
        for sigma in [0.6, 1.5, 3.0, 5.0] {
            let mut exact = Pixels { w, h, px: src.px.clone() };
            box_blur(&mut exact, sigma);
            let mut fast = src.to_argb();
            blur_argb(&mut fast, sigma);
            let fast = Pixels::from_argb(&fast);
            let worst = fast.px.iter().zip(&exact.px)
                .map(|(a, b)| (0..4).map(|c| (a[c] - b[c]).abs()).fold(0.0, f32::max))
                .fold(0.0, f32::max);
            assert!(worst < 0.02, "sigma {sigma}: off by {worst}");
        }
        let mut a = src.to_argb();
        a.fade(0.5);
        let p = Pixels::from_argb(&a).px[15 * w + 20];
        assert!((p[3] - 0.4).abs() < 0.01 && (p[0] - 0.3).abs() < 0.01, "half of (0.6, 0.8): {p:?}");
    }
}
