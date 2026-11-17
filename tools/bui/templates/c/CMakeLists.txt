cmake_minimum_required(VERSION 3.25)
project({{ident}} LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJC) # bui::kit has Objective-C on macOS
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clangd and clang-tidy

# the app ends up in build/bin, whatever the generator
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_BINARY_DIR}/bin)
endforeach()

# BUI, fetched at a release (`bui update` moves the tag to the newest release)
include(FetchContent)
FetchContent_Declare(
  bui
  GIT_REPOSITORY https://github.com/ppmpreetham/BUI.git
  GIT_TAG {{tag}}
  GIT_SHALLOW TRUE)
set(BUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUI_BUILD_PYTHON_BINDINGS OFF CACHE BOOL "" FORCE)
set(BUI_BUILD_IMGUI OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(bui)

add_executable(app src/main.c)
target_link_libraries(app PRIVATE bui::kit)
target_compile_options(app PRIVATE
  $<$<C_COMPILER_ID:MSVC>:/W4>
  $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)
