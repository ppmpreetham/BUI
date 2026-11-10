/* The kit's version: the header's macros and the library agree, and they are
 * well formed.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdio.h>
#include <string.h>

#include "BUI_kit.h"
#include "bui_c_api.h"

static int g_fail = 0;
#define CHECK(c) \
  do { \
    if (!(c)) { \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
      g_fail = 1; \
    } \
  } while (0)

int main(void)
{
  char expect[32];
  snprintf(expect, sizeof(expect), "%d.%d.%d", BUI_KIT_VERSION_MAJOR, BUI_KIT_VERSION_MINOR,
           BUI_KIT_VERSION_PATCH);
  CHECK(strcmp(BUI_KIT_VERSION_STRING, expect) == 0); /* the string and the numbers agree */
  CHECK(strcmp(BK_version_string(), BUI_KIT_VERSION_STRING) == 0);
  CHECK(BK_version() == BUI_KIT_VERSION);
  CHECK(BUI_KIT_VERSION_AT_LEAST(BUI_KIT_VERSION_MAJOR, BUI_KIT_VERSION_MINOR, BUI_KIT_VERSION_PATCH));
  CHECK(!BUI_KIT_VERSION_AT_LEAST(BUI_KIT_VERSION_MAJOR + 1, 0, 0));
  CHECK(BUI_KIT_VERSION_MINOR < 100 && BUI_KIT_VERSION_PATCH < 100); /* the number scheme holds */
  /* the stable C API reports the same version, not a copy of it that can drift */
  int major = -1, minor = -1, patch = -1;
  bui_get_version(&major, &minor, &patch);
  CHECK(major == BUI_KIT_VERSION_MAJOR && minor == BUI_KIT_VERSION_MINOR && patch == BUI_KIT_VERSION_PATCH);
  char api_expect[48];
  snprintf(api_expect, sizeof(api_expect), "BUI %s", BUI_KIT_VERSION_STRING);
  CHECK(strcmp(bui_version_string(), api_expect) == 0);
  if (!g_fail) {
    printf("version test passed: %s\n", BK_version_string());
  }
  return g_fail;
}
