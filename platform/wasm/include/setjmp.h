/* <setjmp.h> for BUI's WebAssembly build (platform/wasm/wasi.cmake puts this folder before the sysroot).
 *
 * wasi-libc's own header refuses to compile without WebAssembly exception handling, which BUI does not
 * use. What is left that jumps:
 *
 *   - FreeType's smooth rasterizer (a full cell pool: ordinary, a big glyph at a large size) does not
 *     need it: gpu/extern/freetype/src/smooth/ftgrays.c notes the overflow in a flag on WASI and splits
 *     the band exactly as the jump did.
 *   - FreeType's cmap validator (a malformed `cmap` table in a font) and libjpeg's error exit (a corrupt
 *     JPEG; JPEG is not built for the web) jump out of deep calls. `setjmp` returns 0 (the normal path)
 *     and `longjmp` traps: such a font or file stops the module (the page's script reports it and
 *     stops the app) instead of being refused. Fonts and images a page ships itself are not affected.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BUI_WASM_SETJMP_H
#define BUI_WASM_SETJMP_H

#include <stdlib.h>

typedef int jmp_buf[1];

#define setjmp(env) ((void)(env), 0)

static inline void longjmp(jmp_buf env, int value)
{
  (void)env;
  (void)value;
  abort();
}

#endif
