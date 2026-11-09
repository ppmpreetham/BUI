/* Frame alpha: an opaque raster keeps the alpha byte at 0 (0x00RRGGBB); a raster with
 * BUI_raster_set_alpha is premultiplied 0xAARRGGBB and BUI_raster_clear makes it transparent.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "GPU_raster.h"

#include <stdio.h>
#include <stdlib.h>

static int near(int v, int want)
{
  return abs(v - want) <= 2;
}

static int check(const char *what, int ok)
{
  if (!ok) {
    printf("raster alpha: %s failed\n", what);
  }
  return ok;
}

/* The same frames on the CPU and the GPU: a transparent clear, translucent and opaque paint,
 * then a second frame that clears a rect over painted pixels (the GPU clear). */
static void frames(BuiRaster *r, int frame)
{
  const float half[4] = {0.2f, 0.4f, 0.6f, 0.5f}, red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  BUI_raster_begin_frame(r, 16, 16);
  if (frame == 0) {
    BUI_raster_clear(r, 0, 0, 16, 16);
    BUI_raster_fill_rect(r, 0, 0, 8, 16, half);
    BUI_raster_fill_rect(r, 12, 0, 4, 16, red);
  }
  else {
    BUI_raster_clear(r, 2, 2, 12, 6);
    BUI_raster_fill_rect(r, 5, 3, 3, 2, half);
  }
  BUI_raster_end_frame(r);
}

static int gpu_matches_cpu(void)
{
  BuiRaster *cpu = BUI_raster_create(16, 16), *gpu = BUI_raster_create(16, 16);
  BUI_raster_set_alpha(cpu, 1);
  BUI_raster_set_alpha(gpu, 1);
  if (!BUI_raster_gpu_attach(gpu, NULL, 1)) {
    printf("raster alpha: no GPU device, GPU part skipped\n");
    BUI_raster_free(cpu);
    BUI_raster_free(gpu);
    return 1;
  }
  int ok = 1;
  for (int frame = 0; frame < 2; frame++) {
    frames(cpu, frame);
    frames(gpu, frame);
    const uint32_t *a = BUI_raster_pixels(cpu), *b = BUI_raster_pixels(gpu);
    for (int i = 0; i < 16 * 16; i++) {
      for (int sh = 0; sh < 32; sh += 8) {
        ok &= near((int)(a[i] >> sh & 0xFF), (int)(b[i] >> sh & 0xFF));
      }
    }
    if (!check(frame ? "GPU clear of painted pixels" : "GPU alpha frame", ok)) {
      break;
    }
  }
  BUI_raster_free(cpu);
  BUI_raster_free(gpu);
  return ok;
}

int main(void)
{
  const float half[4] = {0.2f, 0.4f, 0.6f, 0.5f}, red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  BuiRaster *r = BUI_raster_create(8, 8);
  int ok = 1;

  BUI_raster_begin_frame(r, 8, 8);
  BUI_raster_fill_rect(r, 0, 0, 8, 8, red);
  BUI_raster_fill_rect(r, 0, 0, 4, 8, half);
  BUI_raster_end_frame(r);
  const uint32_t *px = BUI_raster_pixels(r);
  ok &= check("opaque raster has no alpha byte", (px[0] >> 24) == 0 && (px[7] >> 24) == 0);

  BUI_raster_set_alpha(r, 1);
  BUI_raster_begin_frame(r, 8, 8);
  BUI_raster_clear(r, 0, 0, 8, 8);
  BUI_raster_fill_rect(r, 0, 0, 4, 8, half);
  BUI_raster_fill_rect(r, 6, 0, 2, 8, red);
  BUI_raster_end_frame(r);
  px = BUI_raster_pixels(r);
  const uint32_t p = px[0];
  ok &= check("cleared pixel is transparent", px[5] == 0);
  ok &= check("opaque paint is opaque", px[7] == 0xFFFF0000u);
  ok &= check("translucent paint is premultiplied",
              near((int)(p >> 24), 128) && near((int)(p >> 16 & 0xFF), 26) &&
                  near((int)(p >> 8 & 0xFF), 51) && near((int)(p & 0xFF), 77));
  ok &= check("BUI_raster_alpha", BUI_raster_alpha(r) == 1);

  BUI_raster_free(r);
  ok &= gpu_matches_cpu();
  printf("raster alpha: %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
