# BUI on Android: libbui_app.so, the kit + a NativeActivity host (bui_android.c) + an app.
# Built through the NDK's toolchain file; platform/android/build.py drives it and makes the APK.
#
# BUI_ANDROID_APP picks the app: demo (demo.c, C), lua (lua_app.c: assets/main.lua), imgui (a C++ file
# with Dear ImGui, BUI_ANDROID_IMGUI_SRC) or rust
# (a Rust staticlib that defines bui_app_main; BUI_ANDROID_RUST_LIB is its path).
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(BUI_ANDROID_APP "demo" CACHE STRING "demo, lua, rust or imgui")
set(BUI_ANDROID_IMGUI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/imgui_app.cpp" CACHE FILEPATH "the Dear ImGui app (BUI_ANDROID_APP=imgui): a .cpp defining bui_app_main")
set(BUI_ANDROID_RUST_LIB "" CACHE FILEPATH "libXXX.a of the Rust app (BUI_ANDROID_APP=rust)")
set(_glue "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")

add_library(bui_app SHARED bui_android.c fonts.c ${_glue}/android_native_app_glue.c)
target_include_directories(bui_app PRIVATE ${_glue} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bui_app PRIVATE android log m)

if(BUI_ANDROID_APP STREQUAL "demo")
  target_sources(bui_app PRIVATE demo.c)
  target_link_libraries(bui_app PRIVATE bui::kit)
elseif(BUI_ANDROID_APP STREQUAL "lua")
  if(NOT TARGET bui_lua)
    message(FATAL_ERROR "BUI_ANDROID_APP=lua needs -DBUI_BUILD_LUA_BINDINGS=ON (it builds Lua 5.4)")
  endif()
  set(_lua ${BUI_SOURCE_DIR}/bindings/lua/src)
  target_sources(bui_app PRIVATE lua_app.c ${_lua}/bui_lua.c ${_lua}/bui_node.c ${_lua}/bui_context.c
                 ${_lua}/bui_window.c ${_lua}/bui_image.c ${_lua}/bui_anim.c)
  target_include_directories(bui_app PRIVATE ${_lua})
  target_link_libraries(bui_app PRIVATE bui::kit bui_lua bui::gpu bui::core bui_api)
elseif(BUI_ANDROID_APP STREQUAL "imgui")
  if(NOT TARGET bui_imgui)
    message(FATAL_ERROR "BUI_ANDROID_APP=imgui needs -DBUI_BUILD_IMGUI=ON")
  endif()
  target_sources(bui_app PRIVATE ${BUI_ANDROID_IMGUI_SRC})
  target_link_libraries(bui_app PRIVATE bui::imgui)
elseif(BUI_ANDROID_APP STREQUAL "rust")
  if(NOT BUI_ANDROID_RUST_LIB OR NOT EXISTS "${BUI_ANDROID_RUST_LIB}")
    message(FATAL_ERROR "BUI_ANDROID_APP=rust needs BUI_ANDROID_RUST_LIB (the app's staticlib)")
  endif()
  # the staticlib carries its own copy of the kit (blenderui-sys built it for this target), so the
  # kit is not linked again; only its headers (the shell and fonts.c use them) and libc++ are needed
  target_include_directories(bui_app PRIVATE ${BUI_SOURCE_DIR}/kit/include ${BUI_SOURCE_DIR}/gpu/include)
  set_target_properties(bui_app PROPERTIES LINKER_LANGUAGE CXX)
  target_link_libraries(bui_app PRIVATE "${BUI_ANDROID_RUST_LIB}")
else()
  message(FATAL_ERROR "BUI_ANDROID_APP must be demo, lua, rust or imgui")
endif()

# one exported symbol (ANativeActivity_onCreate, from the glue), no build ids, unused sections
# gone, 16 KB pages (Android 15+)
target_compile_options(bui_app PRIVATE -fvisibility=hidden)
target_link_options(bui_app PRIVATE -Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,--build-id=none
                    -Wl,-z,max-page-size=16384 -s)
# the glue's entry point must stay exported
set_source_files_properties(${_glue}/android_native_app_glue.c PROPERTIES COMPILE_OPTIONS
                            -fvisibility=default)
