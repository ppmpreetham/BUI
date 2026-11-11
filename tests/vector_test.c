/* Ctrl+Alt+C over a vector field copies every component, however many there are (the text used to
 * be built in a 512-byte buffer).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

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

static char *g_clip = NULL;

static void clip_set(void *data, const char *text)
{
  (void)data;
  free(g_clip);
  g_clip = malloc(strlen(text) + 1);
  strcpy(g_clip, text);
}

static void request(void *data)
{
  (void)data;
}

int main(void)
{
  enum { W = 400, H = 400, N = 12 };
  BKNode *col = BK_node_new("column");
  BKNode *fields[N];
  for (int i = 0; i < N; i++) {
    fields[i] = BK_node_new("number");
    BK_set_num(fields[i], "value", 1e300 + i); /* 300 digits: the number's text is cut at 63 characters */
    BK_node_add(col, fields[i], -1);
  }
  BK_link_fields(fields, N);
  BKContext *ctx = BK_context_new(col);
  BKHost host = {0};
  host.request = request;
  host.clipboard_set = clip_set;
  BK_context_set_host(ctx, &host);
  BuiRaster *r = BUI_raster_create(W, H);
  BK_render(ctx, r, W, H, 1);

  int rc[4];
  BK_node_rect(fields[3], rc);
  BK_motion(ctx, rc[0] + rc[2] / 2, rc[1] + rc[3] / 2, 0); /* over one component */
  BK_key(ctx, "", "c", BK_CTRL | BK_ALT);
  CHECK(g_clip != NULL);
  if (g_clip) {
    const size_t n = strlen(g_clip);
    CHECK(n > 512);                      /* all twelve are there, not the first few */
    CHECK(g_clip[0] == '[' && g_clip[n - 1] == ']');
    int commas = 0;
    for (const char *p = g_clip; *p; p++) {
      commas += *p == ',';
    }
    CHECK(commas == N - 1);
  }

  free(g_clip);
  BUI_raster_free(r);
  BK_context_free(ctx);
  BK_node_free(col);
  if (!g_fail) {
    printf("vector test passed\n");
  }
  return g_fail;
}
