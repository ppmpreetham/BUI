/* The activity's entry point. The window host is the kit's (kit/intern/kit_window_android.c); this
 * only attaches it, registers the system fonts and runs the app.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "BUI_kit.h"
#include "android_native_app_glue.h"
#include "bui_app.h"

void android_main(struct android_app *app)
{
  BK_android_attach(app);
  bui_android_fonts();
  bui_app_main();
}
