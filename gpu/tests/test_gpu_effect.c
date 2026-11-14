/* GPU effects (application shaders): an effect drawn by the GPU renderer gives the pixels its
 * shader computes, in paint order, inside the clip; a broken shader returns the compiler's log
 * and draws nothing (the caller draws its fallback); the software raster never draws one.
 * Skips (exit 0) where no GPU device can be created. BUI_FX_PPM=dir writes the frame.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "GPU_raster.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 256
#define H 128

static int g_fail;

#define CHECK(cond, ...) \
  do { \
    if (!(cond)) { \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__); \
      printf("\n"); \
      g_fail = 1; \
    } \
  } while (0)

/* a gradient mixed with an input texture, the floats being the blue level and the mix */
static const char *k_gradient =
    "vec4 effect(vec2 p) {\n"
    "  vec2 uv = p / box_size;\n"
    "  vec4 t = input_color(0, uv * input_size(0));\n"
    "  return vec4(mix(vec3(uv.x, uv.y, U[0]), t.rgb, U[1]), 1.0);\n"
    "}\n";

/* the dialect's constructs: splat constructors, casts, loops, mod, fract, atan2, inversesqrt */
static const char *k_dialect =
    "vec4 effect(vec2 p) {\n"
    "  float s = 0.0;\n"
    "  for (int i = 0; i < 4; i++) { s += U[i]; }\n"           /* 1.0 */
    "  float a = float(int(2.7));\n"                            /* 2 */
    "  vec3 c = vec3(0.25) + vec3(a * 0.125, s * 0.25, mod(7.5, 2.0) / 3.0 - 0.25);\n"
    "  c.z += atan2(1.0, 1.0) * 0.0 + inversesqrt(4.0) * 0.0 + fract(3.25) * 0.0;\n"
    "  return vec4(c, 1.0);\n"                                  /* (.5, .5, .5) */
    "}\n";

static const char *k_broken = "vec4 effect(vec2 p) { return vec4(no_such_symbol); }\n";

static float clampf(float v, float lo, float hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}

static void ppm(const char *dir, const char *name, const uint32_t *px)
{
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
  FILE *f = fopen(path, "wb");
  if (!f) {
    return;
  }
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) {
    const unsigned char c[3] = {(unsigned char)(px[i] >> 16), (unsigned char)(px[i] >> 8),
                                (unsigned char)px[i]};
    fwrite(c, 1, 3, f);
  }
  fclose(f);
}

static void rgb_at(const uint32_t *px, int x, int y, int out[3])
{
  const uint32_t v = px[y * W + x];
  out[0] = (int)((v >> 16) & 255);
  out[1] = (int)((v >> 8) & 255);
  out[2] = (int)(v & 255);
}

static int near3(const int got[3], const float want[3], const char *what, int x, int y)
{
  int ok = 1;
  for (int k = 0; k < 3; k++) {
    if (fabsf((float)got[k] - want[k] * 255.0f) > 3.0f) {
      ok = 0;
    }
  }
  CHECK(ok, "%s at (%d, %d): got %d %d %d, want %.0f %.0f %.0f", what, x, y, got[0], got[1],
        got[2], want[0] * 255, want[1] * 255, want[2] * 255);
  return ok;
}

/* the 2x2 texture (red, green / blue, white) sampled bilinearly and clamped at texel position q */
static void tex_at(float qx, float qy, float out[3])
{
  static const float t[2][2][3] = {{{1, 0, 0}, {0, 1, 0}}, {{0, 0, 1}, {1, 1, 1}}};
  const float x = clampf(qx - 0.5f, 0, 1), y = clampf(qy - 0.5f, 0, 1);
  const int x0 = (int)x, y0 = (int)y, x1 = x0 + 1 > 1 ? 1 : x0 + 1, y1 = y0 + 1 > 1 ? 1 : y0 + 1;
  const float fx = x - (float)x0, fy = y - (float)y0;
  for (int k = 0; k < 3; k++) {
    out[k] = (t[y0][x0][k] * (1 - fx) + t[y0][x1][k] * fx) * (1 - fy) +
             (t[y1][x0][k] * (1 - fx) + t[y1][x1][k] * fx) * fy;
  }
}

static int validation(void)
{
  CHECK(BUI_effect_create("x", NULL, 0, 0) == 0, "no source");
  CHECK(BUI_effect_create("x", "", 0, 0) == 0, "empty source");
  CHECK(BUI_effect_create("x", k_gradient, BUI_EFFECT_MAX_INPUTS + 1, 0) == 0, "9 inputs");
  CHECK(BUI_effect_create("x", k_gradient, -1, 0) == 0, "negative inputs");
  CHECK(BUI_effect_create("x", k_gradient, 0, BUI_EFFECT_MAX_FLOATS + 1) == 0, "257 floats");
  char *big = (char *)malloc(BUI_EFFECT_MAX_SOURCE + 2);
  memset(big, ' ', BUI_EFFECT_MAX_SOURCE + 1);
  big[BUI_EFFECT_MAX_SOURCE + 1] = 0;
  CHECK(BUI_effect_create("x", big, 0, 0) == 0, "a source over the limit");
  free(big);
  const int a = BUI_effect_create("a", k_gradient, 1, 2);
  CHECK(a > 0, "a valid effect");
  CHECK(BUI_effect_error(a) && BUI_effect_error(a)[0] == 0, "not tried yet: no error");
  BUI_effect_free(a);
  CHECK(BUI_effect_error(a) == NULL, "freed: no longer live");
  const int b = BUI_effect_create("b", k_gradient, 1, 2);
  CHECK(b != a, "a stale id is not reused");
  BUI_effect_free(b);
  BUI_effect_free(b); /* twice: harmless */
  return !g_fail;
}

int main(void)
{
  validation();

  static const uint8_t tex_px[2 * 2 * 4] = {255, 0,   0,   255, 0,   255, 0,   255,
                                            0,   0,   255, 255, 255, 255, 255, 255};
  const int tex = BUI_texture_create(2, 2, tex_px, 0);
  const int gradient = BUI_effect_create("gradient", k_gradient, 1, 2);
  const int dialect = BUI_effect_create("dialect", k_dialect, 0, 4);
  const int broken = BUI_effect_create("broken", k_broken, 0, 0);
  CHECK(tex > 0 && gradient > 0 && dialect > 0 && broken > 0, "setup");

  const int inputs[BUI_EFFECT_MAX_INPUTS] = {tex};
  const float floats_g[2] = {0.25f, 0.5f};
  const float floats_d[4] = {0.1f, 0.2f, 0.3f, 0.4f};
  const float bg[4] = {0.2f, 0.2f, 0.2f, 1.0f}, red[4] = {1, 0, 0, 1};

  /* software raster: nothing drawn, the caller's fallback is the answer */
  BuiRaster *cpu = BUI_raster_create(W, H);
  BUI_raster_begin_frame(cpu, W, H);
  CHECK(BUI_raster_effect(cpu, 16, 16, 160, 96, gradient, inputs, floats_g, 2) == 0,
        "the software raster draws no effects");
  BUI_raster_end_frame(cpu);
  BUI_raster_free(cpu);

  BuiRaster *gpu = BUI_raster_create(W, H);
  if (!BUI_raster_gpu_attach(gpu, NULL, 1)) {
    printf("gpu effect: no GPU device, skipped\n");
    BUI_raster_free(gpu);
    return g_fail;
  }
  BUI_raster_begin_frame(gpu, W, H);
  BUI_raster_fill_rect(gpu, 0, 0, W, H, bg);
  const float bx = 16.0f, by = 16.0f, bw = 160.0f, bh = 96.0f;
  const int drawn = BUI_raster_effect(gpu, bx, by, bw, bh, gradient, inputs, floats_g, 2);
  CHECK(drawn == 1, "the gradient effect draws on the GPU: %s", BUI_effect_error(gradient));
  CHECK(BUI_effect_error(gradient)[0] == 0, "a good shader has no error: %s",
        BUI_effect_error(gradient));
  /* painted over (paint order): a red square above the effect's top left */
  BUI_raster_fill_rect(gpu, 16, 16, 30, 20, red);
  /* the dialect effect, narrowed by a clip */
  BUI_raster_set_clip(gpu, 190, 20, 30, 50);
  CHECK(BUI_raster_effect(gpu, 180, 10, 70, 100, dialect, inputs, floats_d, 4) == 1,
        "the dialect effect draws: %s", BUI_effect_error(dialect));
  BUI_raster_set_clip(gpu, 0, 0, 0, 0);
  /* the broken one: nothing drawn, the log says why */
  CHECK(BUI_raster_effect(gpu, 200, 100, 40, 20, broken, inputs, NULL, 0) == 0,
        "a broken shader draws nothing");
  CHECK(BUI_effect_error(broken)[0] != 0, "a broken shader has a log");
  printf("broken shader log: %s\n", BUI_effect_error(broken));
  CHECK(strstr(BUI_effect_error(broken), "no_such_symbol") != NULL, "the log names the symbol");
  CHECK(BUI_raster_effect(gpu, 200, 100, 40, 20, broken, inputs, NULL, 0) == 0, "and stays broken");
  BUI_raster_end_frame(gpu);

  const uint32_t *px = BUI_raster_pixels(gpu);
  if (getenv("BUI_FX_PPM")) {
    ppm(getenv("BUI_FX_PPM"), "effect", px);
  }
  /* inside the effect: the gradient mixed with the texture */
  const int pts[][2] = {{60, 60}, {120, 30}, {170, 100}, {40, 90}, {100, 50}};
  for (int i = 0; i < 5; i++) {
    const int x = pts[i][0], y = pts[i][1];
    const float u = ((float)x + 0.5f - bx) / bw, v = ((float)y + 0.5f - by) / bh;
    float t[3];
    tex_at(u * 2.0f, v * 2.0f, t);
    const float want[3] = {u + (t[0] - u) * 0.5f, v + (t[1] - v) * 0.5f, 0.25f + (t[2] - 0.25f) * 0.5f};
    int got[3];
    rgb_at(px, x, y, got);
    near3(got, want, "gradient", x, y);
  }
  int got[3];
  const float grey[3] = {0.2f, 0.2f, 0.2f}, redf[3] = {1, 0, 0}, half[3] = {0.5f, 0.5f, 0.5f};
  rgb_at(px, 8, 8, got);
  near3(got, grey, "outside the effect", 8, 8);
  rgb_at(px, 30, 25, got);
  near3(got, redf, "painted over the effect", 30, 25);
  rgb_at(px, 200, 40, got);
  near3(got, half, "dialect effect inside its clip", 200, 40);
  rgb_at(px, 185, 40, got);
  near3(got, grey, "dialect effect left of its clip", 185, 40);
  rgb_at(px, 200, 90, got);
  near3(got, grey, "dialect effect below its clip", 200, 90);
  rgb_at(px, 210, 110, got);
  near3(got, grey, "where the broken effect would be", 210, 110);
  BUI_raster_free(gpu);

  BUI_effect_free(gradient);
  BUI_effect_free(dialect);
  BUI_effect_free(broken);
  BUI_texture_free(tex);
  if (!g_fail) {
    printf("gpu effect: ok\n");
  }
  return g_fail;
}
