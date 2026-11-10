/* Custom themes: theme files (JSON), the plain-English and Blender key names, the short palette
 * names, runtime setters, and what the extra widget options do to the pixels.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BUI_kit.h"
#include "GPU_raster.h"

static int g_fail = 0;
#define CHECK(c) \
  do { \
    if (!(c)) { \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
      g_fail = 1; \
    } \
  } while (0)

static int near(const float c[4], float r, float g, float b, float a)
{
  return fabsf(c[0] - r) < 1.5e-3f && fabsf(c[1] - g) < 1.5e-3f && fabsf(c[2] - b) < 1.5e-3f &&
         fabsf(c[3] - a) < 1.5e-3f;
}

static int color_is(const char *key, unsigned rgb)
{
  float c[4];
  return BK_theme_color(key, c) &&
         near(c, (float)((rgb >> 16) & 255) / 255.0f, (float)((rgb >> 8) & 255) / 255.0f,
              (float)(rgb & 255) / 255.0f, 1.0f);
}

static const char *k_file =
    "{ \"$schema\": \"x\", \"name\": \"Test set\", \"author\": null,\n"
    "  \"themes\": [\n"
    "    { \"name\": \"Plum\", \"mode\": \"dark\", \"radius\": 0.9,\n"
    "      \"font.size\": 12, \"shadow\": false,\n"
    "      \"colors\": {\n"
    "        \"primary.background\": \"#112233\",\n"
    "        \"foreground\": \"#fedcba\",\n"
    "        \"button.background.selected\": \"#445566\",\n"
    "        \"user_interface.wcol_num.inner\": \"#0a0b0c\",\n"
    "        \"node_editor.grid\": \"#abc\",\n"
    "        \"button.border.width\": 3,\n"
    "        \"button.corners.top_left\": false,\n"
    "        \"button.emboss.enabled\": false,\n"
    "        \"button.gradient.top\": 7,\n"
    "        \"viewport_3d.vertex.size\": 5,\n"
    "        \"caret\": null,\n"
    "        \"not.a.key\": \"#ffffff\" } },\n"
    "    { \"name\": \"Paper\", \"mode\": \"light\", \"colors\": { \"background\": \"#fafafa\" } }\n"
    "  ],\n"
    "  \"highlight\": { \"syntax\": { \"keyword\": { \"color\": \"#ff00ff\" } } } }";

static int pixel_at(BuiRaster *r, int w, int x, int y)
{
  return (int)(BUI_raster_pixels(r)[y * w + x] & 0xffffffu);
}

int main(void)
{
  float c[4];

  /* ---- a theme file ---- */
  CHECK(BK_theme_load_json(k_file) == 2);
  CHECK(BK_theme_count() == 2 && strcmp(BK_theme_at(0), "Plum") == 0 && !BK_theme_at(2));
  CHECK(BK_theme_ignored() == 2); /* viewport_3d.vertex.size has no field; "not.a.key" is unknown */
  CHECK(BK_set_theme("plum") == 1); /* any case */
  CHECK(strcmp(BK_theme_name(), "Plum") == 0);
  /* the short names fill in widgets; a key the theme sets itself wins over them */
  CHECK(color_is("tool_button.background.selected", 0x112233));
  CHECK(color_is("button.background.selected", 0x445566));
  CHECK(color_is("button.text", 0xfedcba) && color_is("text", 0xfedcba));
  /* Blender's name and the plain one are the same key */
  CHECK(color_is("number_field.background", 0x0a0b0c));
  CHECK(color_is("user_interface.wcol_num.inner", 0x0a0b0c));
  CHECK(color_is("node_editor.grid", 0xaabbcc)); /* #abc */
  /* what the theme does not list stays Blender's (dark) */
  CHECK(color_is("region.background", 0x303030));
  CHECK(BK_theme_color("caret", c)); /* null: untouched */
  /* numbers */
  double v;
  CHECK(BK_theme_num("button.border.width", &v) && v == 3.0);
  CHECK(BK_theme_num("button.corners.top_left", &v) && v == 0.0);
  CHECK(BK_theme_num("button.corners.top_right", &v) && v == 1.0);
  CHECK(BK_theme_num("button.emboss.enabled", &v) && v == 0.0);
  CHECK(BK_theme_num("button.gradient.top", &v) && v == 7.0);
  CHECK(BK_theme_num("slider.roundness", &v) && fabs(v - 0.9) < 1e-6); /* "radius" */
  CHECK(fabsf(BK_font_size_points() - 12.0f) < 1e-6f);
  CHECK(BK_theme_num("panel.roundness", &v) && fabs(v - 0.4) < 1e-6); /* "radius" is for widgets */
  CHECK(BK_theme_num("viewport_3d.vertex.size", &v) == 0);

  /* the second theme starts from the light preset */
  CHECK(BK_set_theme("Paper") == 1);
  CHECK(color_is("region.background", 0xfafafa));
  CHECK(color_is("button.background", 0xdbdbdb)); /* Blender's light button */
  CHECK(fabsf(BK_font_size_points() - 11.0f) < 1e-6f); /* Plum's size is gone */
  CHECK(BK_theme_num("button.border.width", &v) && v == 1.0);

  /* built-in names still work, and leave a custom theme */
  CHECK(BK_set_theme("dark") == 1 && strcmp(BK_theme_name(), "dark") == 0);
  CHECK(color_is("button.background", 0x545454));

  /* ---- bad files add nothing and say where ---- */
  CHECK(BK_theme_load_json("{ \"name\": \"x\", \"themes\": [ { \"name\": \"Ok\" }, { } ] }") == -1);
  CHECK(strstr(BK_theme_error(), "themes[1]") != NULL);
  CHECK(BK_theme_count() == 2); /* "Ok" was not added either */
  CHECK(BK_theme_load_json("{ \"themes\": [ ") == -1);
  CHECK(strstr(BK_theme_error(), "line 1") != NULL);
  CHECK(BK_theme_load_json("[1, 2]") == -1);
  CHECK(BK_theme_load_json("{\"themes\": [{\"name\": \"X\", \"mode\": \"purple\"}]}") == -1);
  CHECK(BK_theme_load_file("no/such/file.json") == -1 && strstr(BK_theme_error(), "cannot open"));
  CHECK(BK_set_theme("Nope") == 0);

  /* the same name replaces an older theme */
  CHECK(BK_theme_load_json("{\"name\":\"s\",\"themes\":[{\"name\":\"PLUM\",\"colors\":{\"text\":\"#010203\"}}]}") == 1);
  CHECK(BK_theme_count() == 2);
  CHECK(BK_set_theme("Plum") == 1 && color_is("text", 0x010203));
  CHECK(BK_set_theme("dark") == 1);

  /* ---- one value at a time ---- */
  const float red[4] = {1, 0, 0, 1};
  CHECK(BK_theme_set_color("button.background", red) && color_is("button.background", 0xff0000));
  CHECK(BK_theme_set_color("user_interface.wcol_regular.inner", red));
  CHECK(!BK_theme_set_color("button.nonsense", red));
  CHECK(!BK_theme_set_color("button.border.width", red)); /* a number, not a color */
  CHECK(BK_theme_set_num("button.roundness", 0.25) && BK_theme_num("button.roundness", &v) && fabs(v - 0.25) < 1e-6);
  CHECK(BK_theme_set_num("button.border.width", 0) && BK_theme_num("button.border.width", &v) && v == 0.0);
  CHECK(!BK_theme_set_num("button.background", 1));
  CHECK(BK_set_theme("dark") == 1 && color_is("button.background", 0x545454)); /* reset */

  /* ---- what the options do to the pixels ---- */
  const int W = 120, H = 60;
  BKNode *col = BK_node_new("column");
  BKNode *btn = BK_node_new("button");
  BK_set_str(btn, "text", "");
  BK_node_add(col, btn, -1);
  BKContext *ctx = BK_context_new(col);
  BuiRaster *r = BUI_raster_create(W, H);
  BK_render(ctx, r, W, H, 1);
  int rect[4];
  BK_node_rect(btn, rect);
  const int x0 = rect[0], y0 = rect[1], w = rect[2];
  CHECK(w > 20);
  const int corner_round = pixel_at(r, W, x0, y0);
  const int edge_mid = pixel_at(r, W, x0 + w / 2, y0); /* the top edge, between the corners */
  CHECK(corner_round != edge_mid); /* a rounded corner leaves the region color at the very corner */

  /* a square top-left corner fills the corner pixel like the edge next to it */
  CHECK(BK_theme_set_num("button.corners.top_left", 0));
  BK_render(ctx, r, W, H, 1);
  CHECK(pixel_at(r, W, x0, y0) == pixel_at(r, W, x0 + 1, y0) || pixel_at(r, W, x0, y0) != corner_round);
  CHECK(pixel_at(r, W, x0, y0) != corner_round);
  CHECK(BK_theme_set_num("button.corners.top_left", 1));

  /* no border: the edge pixel is the fill, not the outline */
  BK_render(ctx, r, W, H, 1);
  const int with_border = pixel_at(r, W, x0 + w / 2, y0);
  CHECK(BK_theme_set_num("button.border.width", 0));
  BK_render(ctx, r, W, H, 1);
  CHECK(pixel_at(r, W, x0 + w / 2, y0) != with_border);
  CHECK(abs((pixel_at(r, W, x0 + w / 2, y0) & 0xff) - 0x54) <= 2); /* the fill itself reaches the edge */

  /* a wide border covers more rows: row 2 is outline with width 3, fill with width 1 */
  BK_set_theme("dark");
  const float ol[4] = {0, 1, 0, 1};
  CHECK(BK_theme_set_color("button.border", ol));
  BK_render(ctx, r, W, H, 1);
  const int row2_thin = pixel_at(r, W, x0 + w / 2, y0 + 2);
  CHECK(BK_theme_set_num("button.border.width", 4));
  BK_render(ctx, r, W, H, 1);
  const int row2_wide = pixel_at(r, W, x0 + w / 2, y0 + 2);
  CHECK(row2_wide != row2_thin && (row2_wide & 0x00ff00) > (row2_thin & 0x00ff00));

  BUI_raster_free(r);
  BK_context_free(ctx);
  BK_node_free(col);
  BK_set_theme("dark");
  if (!g_fail) {
    printf("theme file tests passed\n");
  }
  return g_fail;
}
