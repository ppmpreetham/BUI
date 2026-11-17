/* The system's fonts for the kit: the build leaves Inter out (BUI_NO_INTER, ~0.8 MB), and the
 * phone has Roboto and Noto in /system/fonts. Read once, registered under the generic family
 * names the kit asks for.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>

#include "GPU_raster.h"
#include "bui_app.h"

static int add(const char *family, int flags, const char *const *names)
{
  for (; *names; names++) {
    char path[160];
    snprintf(path, sizeof(path), "/system/fonts/%s", *names);
    FILE *f = fopen(path, "rb");
    if (!f) {
      continue;
    }
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = len > 0 ? (unsigned char *)malloc((size_t)len) : NULL;
    const int ok = data && fread(data, 1, (size_t)len, f) == (size_t)len &&
                   BUI_text_add_font(family, flags, data, (int)len);
    free(data); /* registered fonts are copied */
    fclose(f);
    if (ok) {
      return 1;
    }
  }
  return 0;
}

void bui_android_fonts(void)
{
  static const char *const regular[] = {"Roboto-Regular.ttf", "RobotoStatic-Regular.ttf",
                                        "NotoSans-Regular.ttf", "DroidSans.ttf", NULL};
  static const char *const bold[] = {"Roboto-Bold.ttf", "RobotoStatic-Bold.ttf",
                                     "NotoSans-Bold.ttf", "DroidSans-Bold.ttf", NULL};
  static const char *const mono[] = {"DroidSansMono.ttf", "RobotoMono-Regular.ttf",
                                     "NotoSansMono-Regular.ttf", NULL};
  add("sans", 0, regular);
  add("sans", BUI_FONT_BOLD, bold);
  add("mono", 0, mono);
}
