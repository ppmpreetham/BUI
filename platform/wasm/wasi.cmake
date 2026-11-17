# CMake toolchain for WebAssembly (WASI): BUI's C kit compiled to wasm32-wasip1 with clang and the WASI
# sysroot (wasi-libc, and libc++ for the C++ in core/), to be linked into a Rust crate (blenderui-web)
# and run in a browser.
#
#   WASI_SYSROOT=<dir>   wasi-sdk's share/wasi-sysroot (needs include/ and lib/ for wasm32-wasip1,
#                        with libc++ under noeh/)
#   WASI_CLANG=<dir>     (optional) the folder of the clang to use; default: clang from PATH
#
# The flags are compile options, not CMAKE_<LANG>_FLAGS: cargo's cmake crate replaces those.
#
# SPDX-License-Identifier: GPL-2.0-or-later

if(NOT DEFINED ENV{WASI_SYSROOT})
  message(FATAL_ERROR "WASI_SYSROOT must name the WASI sysroot (wasi-sdk's share/wasi-sysroot)")
endif()
set(WASI_SYSROOT "$ENV{WASI_SYSROOT}")

set(CMAKE_SYSTEM_NAME WASI)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# clang from WASI_CLANG, else from PATH (find_program adds .exe on Windows)
find_program(_wasi_cc NAMES clang HINTS "$ENV{WASI_CLANG}")
find_program(_wasi_cxx NAMES clang++ HINTS "$ENV{WASI_CLANG}")
if(NOT _wasi_cc OR NOT _wasi_cxx)
  message(FATAL_ERROR "clang and clang++ are needed: put them on PATH or set WASI_CLANG to LLVM's bin folder")
endif()
set(CMAKE_C_COMPILER "${_wasi_cc}")
set(CMAKE_CXX_COMPILER "${_wasi_cxx}")
# llvm-ar next to clang, else on PATH (Debian and Ubuntu name it llvm-ar-18 and so on)
find_program(CMAKE_AR NAMES llvm-ar llvm-ar-22 llvm-ar-21 llvm-ar-20 llvm-ar-19 llvm-ar-18 llvm-ar-17 HINTS "$ENV{WASI_CLANG}")
find_program(CMAKE_RANLIB NAMES llvm-ranlib llvm-ranlib-22 llvm-ranlib-21 llvm-ranlib-20 llvm-ranlib-19 llvm-ranlib-18 llvm-ranlib-17 HINTS "$ENV{WASI_CLANG}")
if(NOT CMAKE_AR OR NOT CMAKE_RANLIB)
  message(FATAL_ERROR "llvm-ar and llvm-ranlib are needed (they come with LLVM): put them on PATH or set WASI_CLANG")
endif()
set(CMAKE_C_COMPILER_TARGET wasm32-wasip1)
set(CMAKE_CXX_COMPILER_TARGET wasm32-wasip1)
set(CMAKE_SYSROOT "${WASI_SYSROOT}")

# what wasi-libc is: one thread, no signals, no exceptions, no dlopen, no setjmp
add_compile_options(
  "SHELL:-isystem \"${CMAKE_CURRENT_LIST_DIR}/include\"" # <setjmp.h> without exception handling
  -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_MMAN
  -fno-exceptions)
# libc++ (the no-exceptions build) comes with wasi-sdk's sysroot; try/catch in Blender's containers compile away
add_compile_options(
  "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include \"${CMAKE_CURRENT_LIST_DIR}/no_exceptions.h\">"
  "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>"
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-isystem \"${WASI_SYSROOT}/include/wasm32-wasip1/noeh/c++/v1\">")

# static libraries only: the Rust linker links them (nothing here can run on the build machine)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
