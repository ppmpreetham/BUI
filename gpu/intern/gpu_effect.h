/* BUI GPU effects (application shaders) — private interface for the GPU backends.
 *
 * The registry (BUI_effect_create / BUI_effect_free, GPU_raster.h) and the one
 * place that turns an app's shader source into what a backend compiles.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "GPU_raster.h"

/* The longest compile/link log kept per effect (BUI_effect_error). */
#define BUI_EFFECT_LOG 1024

typedef enum BuiFxDialect {
  BUI_FX_HLSL = 0, /* Direct3D 11: the pixel shader, entry point bui_ps (vertex: bui_fx_hlsl_vs) */
  BUI_FX_GLSL = 1, /* OpenGL 3.3: the fragment source (vertex: bui_fx_glsl_vs) */
} BuiFxDialect;

typedef struct BuiEffectView {
  int id;
  const char *name;
  const char *source;
  int n_inputs, n_floats;
} BuiEffectView;

/* 0 when `id` is not a live effect. */
int bui_effect_view(int id, BuiEffectView *r_view);
/* Keeps the compile/link log of a backend's attempt ("" clears it). */
void bui_effect_set_error(int id, const char *log);
/* Bumped whenever an effect is freed (device caches sweep then). */
unsigned bui_effect_free_gen(void);

/* The complete source for `dialect`: the backend's prelude (the compat macros and
 * the declarations the effect's interface implies), then the app's source with
 * its own line numbers. malloc'd; NULL when out of memory. */
char *bui_effect_assemble(BuiFxDialect dialect, const BuiEffectView *e);

/* The vertex shaders that cover the box (bui_box, bui_ndc): Direct3D 11 (entry bui_vs, from
 * SV_VertexID) and OpenGL (gl_VertexID). */
extern const char bui_fx_hlsl_vs[];
extern const char bui_fx_glsl_vs[];

/* A device cache slot for an effect id: id = (slot + 1) | serial << 16. */
#define BUI_EFFECT_SLOT(id) (((id) & 0xFFFF) - 1)
