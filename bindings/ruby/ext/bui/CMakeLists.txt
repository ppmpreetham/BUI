# The Ruby extension bui_ext, with BUI's C kit. extconf.rb configures this project
# with Ruby's own settings; nothing here is Ruby-version specific.
#
#   BUI_SOURCE_DIR     BUI's source tree (a checkout, or the gem's vendored csrc/)
#   RUBY_INCLUDE_DIRS  Ruby's header folders
#   RUBY_DEFINITIONS   the -D definitions Ruby's headers expect (from RbConfig CPPFLAGS)
#   RUBY_LINK          what links a Ruby extension (libruby, or nothing)
#   RUBY_EXT_OUT       where the built extension goes (bui_ext.so / .bundle)
#   BUI_RUBY_SANITIZE  "address" builds the kit and the extension with AddressSanitizer
#
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.20)
project(bui_ruby LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJC) # the kit's Cocoa window host; the extension links it
endif()

set(BUI_SOURCE_DIR "" CACHE PATH "BUI's source tree")
set(RUBY_INCLUDE_DIRS "" CACHE STRING "Ruby's header folders")
set(RUBY_DEFINITIONS "" CACHE STRING "definitions Ruby's headers expect")
set(RUBY_LINK "" CACHE STRING "what links a Ruby extension")
set(RUBY_EXT_OUT "" CACHE FILEPATH "where the built extension goes")
set(BUI_RUBY_SANITIZE "" CACHE STRING "a -fsanitize= value for the kit and the extension")

if(NOT EXISTS "${BUI_SOURCE_DIR}/kit/include/BUI_kit.h")
  message(FATAL_ERROR "bui_ruby: no BUI source tree at '${BUI_SOURCE_DIR}'")
endif()

if(BUI_RUBY_SANITIZE)
  add_compile_options(-fsanitize=${BUI_RUBY_SANITIZE} -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=${BUI_RUBY_SANITIZE})
endif()

# The kit and what it needs, nothing else: no tests, examples or other bindings.
foreach(option BUI_BUILD_TESTS BUI_BUILD_EXAMPLES BUI_BUILD_PYTHON_BINDINGS BUI_BUILD_LUA_BINDINGS
               BUI_BUILD_QT_BACKEND BUI_BUILD_IMGUI)
  set(${option} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${BUI_SOURCE_DIR}" bui EXCLUDE_FROM_ALL)

add_library(bui_ext MODULE bui_ext.c bui_node.c bui_context.c bui_window.c bui_image.c)
target_include_directories(bui_ext PRIVATE ${RUBY_INCLUDE_DIRS})
target_compile_definitions(bui_ext PRIVATE ${RUBY_DEFINITIONS})
target_link_libraries(bui_ext PRIVATE bui_kit ${RUBY_LINK})
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  # Ruby's API passes unused self arguments everywhere
  target_compile_options(bui_ext PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
# Ruby loads Init_bui_ext by name: the only symbol the module shows (RUBY_FUNC_EXPORTED)
set_target_properties(bui_ext PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden C_STANDARD 11)
if(RUBY_EXT_OUT)
  get_filename_component(_ext_name "${RUBY_EXT_OUT}" NAME)
  get_filename_component(_ext_suffix "${RUBY_EXT_OUT}" LAST_EXT)
  set_target_properties(bui_ext PROPERTIES SUFFIX "${_ext_suffix}")
  add_custom_command(TARGET bui_ext POST_BUILD
                     COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:bui_ext> "${RUBY_EXT_OUT}"
                     COMMENT "bui_ruby: ${_ext_name}")
endif()
