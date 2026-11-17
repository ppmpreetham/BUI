/* Force-included into BUI's C++ when it is built for WebAssembly (platform/wasm/wasi.cmake), where there
 * are no C++ exceptions: Blender's containers (BLI_vector.hh, BLI_map.hh, ...) wrap element moves in
 * `try { ... } catch (...) { undo; throw; }` to stay consistent when a move throws. Nothing throws
 * here, so the `try` body always runs, the handler never does, and a rethrow is nothing.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BUI_WASM_NO_EXCEPTIONS_H
#define BUI_WASM_NO_EXCEPTIONS_H

#if defined(__cplusplus) && !defined(__cpp_exceptions)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wkeyword-macro"
#  define try if (true)
#  define catch(...) if (false)
#  define throw (void)0
#  pragma clang diagnostic pop
#endif

#endif
