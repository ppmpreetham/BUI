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
  printf("raster alpha: %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
