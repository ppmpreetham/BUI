/* What an Android app gives the shell (bui_android.c): one function.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BUI_APP_H
#define BUI_APP_H

/* The app: build a UI, open a window (BK_window_new) and run it (BK_window_run), exactly as on a
 * desktop. Returns when the window is closed. C, Lua and Rust apps each define it. */
#ifdef __cplusplus
extern "C" {
#endif

void bui_app_main(void);

/* Registers the system's fonts (Roboto, Droid Sans Mono): nothing is embedded in the build. */
void bui_android_fonts(void);

#ifdef __cplusplus
}
#endif

#endif
