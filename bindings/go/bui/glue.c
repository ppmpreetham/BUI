/* Trampolines from the kit's C callbacks to package bui's exported Go functions.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "glue.h"

#include "_cgo_export.h"

static void host_request(void *data)
{
  goHostRequest((uintptr_t)data);
}

static void host_schedule(void *data, int ms, int timer)
{
  goHostSchedule((uintptr_t)data, ms, timer);
}

static void host_emit(void *data, BKNode *node, int kind, void *ref, double a, double b)
{
  goHostEmit((uintptr_t)data, node, kind, ref, a, b);
}

void bui_go_set_host(BKContext *ctx, uintptr_t id)
{
  BKHost host = {0};
  host.data = (void *)id;
  host.request = host_request;
  host.schedule = host_schedule;
  host.emit = host_emit;
  BK_context_set_host(ctx, &host);
}

static void ref_retain(void *ref)
{
  goRefRetain((uintptr_t)ref);
}

static void ref_release(void *ref)
{
  goRefRelease((uintptr_t)ref);
}

void bui_go_set_ref_hooks(void)
{
  BK_set_ref_hooks(ref_retain, ref_release);
}

static void win_timer(void *data, BKWindow *win, int id)
{
  (void)win;
  goWindowTimer((uintptr_t)data, id);
}

static int win_close(void *data, BKWindow *win)
{
  (void)win;
  return goWindowClose((uintptr_t)data);
}

static void win_frame(void *data, BKWindow *win)
{
  (void)win;
  goWindowFrame((uintptr_t)data);
}

static void win_focus(void *data, BKWindow *win, int active)
{
  (void)win;
  goWindowFocus((uintptr_t)data, active);
}

void bui_go_window_callbacks(BKWindow *win, uintptr_t id)
{
  BKWindowCallbacks cb = {0};
  cb.data = (void *)id;
  cb.on_timer = win_timer;
  cb.on_close = win_close;
  cb.on_frame = win_frame;
  cb.on_focus = win_focus;
  BK_window_set_callbacks(win, &cb);
}

static void wake(void)
{
  goWake();
}

int bui_go_wake_init(void)
{
  return BK_window_wake_init(wake);
}
