# Effects performance: blur, CSS animation and scale

How BUI makes the effects that used to stall a frame cheap: CSS animations
and transitions, animated `scale` (and other transforms), and `filter: blur()`
(still, moving, with an animated radius, and wide backdrops). For each one:
what it cost, why, which algorithm replaced it, and what it costs now.

The page that brought it up was a lyrics view: 300 lines of 34 px text,
the sung word lighting up and rising, lines blurred by their distance from
the sung one, the column gliding up, and a cover blurred by 64 px turning
behind it. Each of those took from a tenth of a second to over a minute
per frame.

## What it costs now

`cargo test --release --test perf_effects -- --ignored --nocapture --test-threads=1`
(`bindings/rust/blenderui/tests/perf_effects.rs`): a 1280×800 headless window,
300 rows of text, 60 frames of 16 ms each.

| Scene | First frame | BUI per frame (mean / worst) |
|---|---|---|
| Transitions (opacity, transform, colour) on 300 rows | 13 ms | **0.62 / 7.3 ms** |
| `scale` keyframes on 40 rows of text | 13 ms | **0.13 / 0.27 ms** |
| `blur(2px)` on 300 rows, still | 77 ms | **0.00 ms** |
| `blur(1.5px)` on 7 rows, the column moving | 31 ms | **0.45 / 0.65 ms** |
| Blur radius animated (3 → 0 px) on 7 rows | 33 ms | **4.7 / 21 ms** |
| `blur(64px)` on a 1680×1200 cover image, still | 24 ms | **0.00 ms** |
| `blur(64px)` on a 1680×1200 cover image, turning | 17 ms | **2.1 / 2.5 ms** |

**BUI per frame** is everything BUI does for the frame: values, layers, the
display list. It is measured apart from the headless window's software
rasterization of the whole window (`App::draw`: 3 to 10 ms here), because a
native window draws on the GPU, and only what changed.

Before these changes, measured the same way per layer: a lyric line's blur
layer took about 10 ms (2.5 to 5 ms to rasterize, 2.6 to 4 ms to blur, 3.6 ms
to make the image), and the 64 px cover took **1.2 s** (600 ms to rasterize,
210 ms to blur, 360 ms to make the image). The animated radius cost **155 ms
a frame** and the turning cover **5 s a frame**. Before the earlier round
(CSS redraws, vector transforms, the moving-layer cache) the lyrics page took
**75 s** for its first frame and 0.7 to 1.6 s for each frame after it (the
app's debug build, headless).

## CSS animations and transitions

**The problem.** While any CSS animation or transition ran, the window
rendered the view again every frame, then cascaded the styles and laid out
the whole page. Motion's values (`<motion.div animate>`) already had a
cheaper path: a *redraw* writes the new values into the last frame's
styles and repaints only the boxes that changed.

**What it does now.** CSS animations take the same redraw. At a full frame,
`Animations::apply` (`anim.rs`) keeps a `Paint` for each animated element:
its box, its cascaded style, and each animation's timing with its keyframes
*resolved*: every keyframe's value worked out against the element's style
once (`resolve`). A frame of time alone (`Animations::redraw_css`) is then,
for each animated element, a lookup of the keyframe segment and a blend
(`eval_track`). It writes the value into the element's private style, and
shifts the box and its subtree for `translate`. There is no cascade, no layout,
and no copy of a style. Before, every frame copied the whole style for every
keyframe declaration. The window repaints only the changed boxes.

A redraw takes the values that only paint: opacity, colours, shadows,
filters, `transform`, `translate`, `rotate` and `scale`. A value that changes
layout (width, margins, font size ...) still renders a full frame, and so does
an inherited value (`color`, `text-shadow`, `visibility`) on an element with
child elements, which took the old value at the cascade. Text inside the
element shares its style and follows. Transitions start at full frames,
when a style changes, and run in redraws after that.

A window that was closed no longer draws frames: its animations used to keep
running.

## Scale and transforms

**The problem.** An element with a `scale`, `rotate` or any non-translate
`transform` was painted into a pixel layer: its subtree was rasterized on the
CPU, then resampled through the matrix, also on the CPU, and drawn back as an
image. Each new scale value meant a new layer: rasterized text, resampled,
uploaded, every frame. Any such layer in a frame also turned off the partial
repaint for the whole frame.

**What it does now.** A transform that only moves and scales (positive,
axis-aligned: `Effects::vector`) needs no pixels. Each box gets a vector
matrix, its own move and scale times its ancestors' (`vector_mats` in
`paint.rs`), and what it paints is mapped through it: points, rectangles,
images, corner radii and line widths, clips made inside the transformed box,
and text, whose anchor moves and whose size scales. Scaled text stays text,
drawn by the renderer at its size (Slug draws any size from outlines on the
GPU). An animated scale costs what an animated colour does, and keeps the
partial repaint.

A turn, a skew or a mirror still takes a layer, because the canvas has no
rotated text or images, and so does a scale that is not the same on both
axes over text (a text item has one size). The layer is turned on the GPU,
though: it is rasterized and filtered once, uploaded as a BUI texture of
its premultiplied texels (`turn_texture`: mixed straight, the faded edges
of a turned layer darkened), and drawn through the matrix by a small shader
(`TURN_SHADER`, a `BK_ITEM_EFFECT` canvas item over the turned box from
`turn_box`, sampled bilinearly). A turning frame moves four corners and
samples once per pixel on the GPU, with no CPU resample and no upload. The
texture is held by the layer cache and by every display list that draws it,
and freed when neither does (another window's paints can drop it from the
cache while an idle window still shows it). Without a GPU (the software renderer, headless) the CPU turn
runs. A layer covers what its content painted, so content scaled inside a
layer is no longer cut at the layer's box (`layer_area`).

## Images while resizing

The kit's image cache kept one decode per exact drawn size, so every step
of a window resize missed for each picture that changed size. It drew
nothing until a new decode arrived, and ran a decoder for every picture on
every step. A decode is now the whole image at the scale it is drawn,
rounded up to a step of 2^(1/4) and never above its own size
(`bk_image_cache_whole`). `object-fit` crops when drawing, not when
decoding. A box resized within a step (up to 19%) draws the decode it has,
downscaled by less than 1.19, which the bilinear draw keeps sharp. While a
new step decodes, the source's last drawn decode is shown
(`BKImageSource.shown`), so a picture never blinks out.

## Blur

Three costs added up: rasterizing the layer, blurring it, and making the
image. Each of them changed.

### Wide blurs: work at the blur's resolution

A blur of radius σ leaves no detail finer than about σ/3. So a layer whose
widest blur is σ device pixels (from 6 up) is rasterized, filtered and
transformed at 1/k of the screen's resolution, with k = ⌊σ / 3⌋. Its small
image is drawn stretched to the layer's area, and the renderer's bilinear
filter does the upscale for free (`low_res`). This is the CPU form of the
GPU "dual filter" blurs (Marius Bjørge, *Bandwidth-Efficient Rendering*,
SIGGRAPH 2015), which blur a pyramid of downsampled copies. For the 64 px
cover k is 21, so there are 441 times fewer pixels to rasterize, blur, turn
and upload. Layers with a clip path, a mask or a blend mode stay at full
resolution, since their edges or backdrop must stay sharp.

`effects::blur` (for floats) does the same inside a single blur: from σ = 6
it box-filters a copy down by k, blurs it, and scales it back up
bilinearly. The variances of the box down, (k² − 1)/12, and of the bilinear
up, k²/6, are taken off the blur, so the result matches a full-size blur to
within 4 % (`a_downsampled_blur_matches_the_full_one`).

### Narrow blurs: three box blurs, in integers, for SIMD

A Gaussian is approximated by three box blurs whose widths are chosen for
σ (Wells 1986; the layout follows Ivan Kutskir's "Fastest Gaussian blur").
A box blur is a running sum, so the work per pixel doesn't depend on the
radius. `blur_argb` runs them on premultiplied ARGB:

- the four channels are split into planes of bytes;
- every pass runs down the columns a whole row at a time,
  `sums[x] += row[y + r][x] − row[y − r − 1][x]`, a straight loop over
  arrays that the compiler vectorizes;
- the division is a multiply and a shift (`sum · ⌊2²⁴ / n⌋ >> 24`, which
  fits in 32 bits);
- the horizontal passes are the same passes on the transposed plane,
  transposed in 32×32 tiles.

It matches the float blur to within 2 % (`the_argb_blur_matches_the_float_one`)
and takes a lyric line's layer in 1.2 ms, against 2.6 ms for a per-pixel
integer version and more for the float one.

Considered and not used: a true Gaussian kernel (the work grows with σ);
stack blur (constant work too, with no better quality than three boxes and
harder to vectorize); recursive IIR Gaussians (Deriche, Young–van Vliet:
constant work, but in floats and sequential along each line). On a GPU the
best choice is the dual Kawase pyramid, but the canvas has no render targets
to run it into (see below).

### No conversions: premultiplied ARGB end to end

The canvas rasterizer used to return straight-alpha RGBA, dividing every
channel by alpha. Layers then converted it to floats, and the image was made
by dividing again and having the kit premultiply. `BK_canvas_rasterize` now
writes premultiplied ARGB words, the kit's own image format. The rasterizer
draws on black and on white to find alpha, and the colour on black is the
premultiplied colour, so there is no division at all. A layer whose filters
are all blurs stays in that format end to end (`Argb`, `blur_argb`,
`Image::from_argb`, which copies with `BK_imbuf_from_argb`). Effects that
need floats (colour filters, masks, turns, blending) convert without
dividing either.

### Caching by stage, relative to the layer

A layer is computed in three stages, and each stage is cached by what it
depends on (`Caches` in `paint.rs`):

| Stage | Key | What reuses it |
|---|---|---|
| raster | the content, relative to its own area | an animated blur radius (the raster doesn't depend on how far the blur spreads: the margin is added at the filter stage) |
| filtered | raster + filters (blur radii to ¼ device px) + clip, mask, opacity | a turning layer: only the turn is redone |
| image | filtered + the transform | a layer that only moves: nothing is redone |

All keys are relative to the layer's area, to 1/64 px. A layer that moved is
the same layer drawn elsewhere: a blurred line in a gliding column is
blurred once. A clip that holds the whole layer is left out of the key, so a
layer moving under a still clip stays the same. Blur radii are rounded to a
quarter of a device pixel, a difference no one sees, so an animated radius
reuses each filtered layer for several frames. A stage that a frame doesn't
use is dropped at the end of that frame, so a static page keeps only images.

### Layers that cost nothing

- **Out of view.** A layer whose boxes, grown by how far its filters spread,
  are off the window or outside its clip is not painted at all
  (`out_of_view`). This is decided before its content is painted, so a
  300-row list of blurred lines pays only for the rows on screen. A layer with
  a turn inside it is the exception: a turn can carry what it paints past its
  boxes.
- **Fading.** A layer's own opacity is not in its pixels: its image is drawn
  at that opacity (`BKCanvasItem::fade`, which the canvas passes to its image
  drawing). A page fading in reuses every layer instead of redrawing each
  one each frame.
- **Under a clip.** The clip a layer sits under is applied when its image is
  drawn, not baked into its raster (`Painter::unclip`), so a line gliding
  past the edge of a list is rasterized once.
- **Painted twice.** A result is kept for three paints without use, not one.
  A frame can be painted more than once, and a paint that finds a layer's
  image never touches its raster, which the next frame of a turn or a fade
  needs.
- **Boxes with no area.** A line break or an empty run, placed at (0, 0), no
  longer stretches its layer to the page's corner.
- **Finding a layer's subtree.** An Euler tour, one walk per paint, makes "is
  this box inside that layer" two comparisons. Walking up from every box was
  quadratic, and took 9 of a 13 ms paint on the lyrics page.

The lyrics page opening (fading and rising in, every line blurred, the cover
drifting behind) went from over a minute per frame to 4 ms of paint per
frame.

## What is left

- **GPU layers.** Layers are rasterized and filtered on the CPU, then
  uploaded. Rendering them into GPU textures and blurring there (dual Kawase)
  would make an animated radius on a large area as cheap as a still one. It
  needs render targets for canvas items, which the canvas doesn't have yet.
- **Rotated text** still takes a pixel layer (turned on the GPU, rasterized
  again only when its content or size changes).
- **A resize renders the whole view.** Each size renders, cascades, builds
  boxes and runs taffy again (about 11 to 17 ms for the 3,700-node lyrics
  page, taffy 8 of it). `vh`, `vw` and media queries resolve in the
  cascade, so a layout-only path for resizes needs the cascade to mark
  what depends on the viewport.
- **Frames with a layer repaint everything.** Their partial repaint is off: it
  maps each box to a fixed number of display items, and a layer's image
  replaces the items of its whole subtree. The full repaint of the 300-row page
  takes about 0.4 ms.
- **CSS gradients are drawn again each frame.** Unlike `border-image`
  gradients, they aren't cached as images, which is why the turning
  gradient backdrop costs 5.5 ms a frame where the turning cover image costs
  2.1 ms.
- **Headless redraws repeat.** `App::draw` flushes twice, and a window still
  animating redraws at the same time each flush. The later ones hit the
  caches.
