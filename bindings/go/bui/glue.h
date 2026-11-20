/* C side of package bui: the kit's callbacks forward to exported Go functions,
 * each window, context and callback named by a small integer id rather than a
 * pointer into Go memory (cgo's pointer rules).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stdint.h>
#include <stdlib.h>

#include "BUI_kit.h"

void bui_go_set_host(BKContext *ctx, uintptr_t id);
void bui_go_set_ref_hooks(void);
void bui_go_window_callbacks(BKWindow *win, uintptr_t id);
int bui_go_wake_init(void);
/* An id as the `void *ref` the kit hands back in BK_EV_CALL. */
static inline void *bui_go_token(uintptr_t id)
{
  return (void *)id;
}
