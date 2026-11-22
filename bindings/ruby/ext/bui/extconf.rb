# frozen_string_literal: true

# Configures the bui_ext extension. BUI's C kit builds with CMake (it is C, C++20
# and, on macOS, Objective-C), so this script does not use mkmf's Makefile: it
# configures ext/bui/CMakeLists.txt with what this Ruby builds extensions with,
# and writes a small Makefile with the targets RubyGems and rake-compiler call
# (all, install, clean).
#
# Environment:
#   BUI_SOURCE_DIR        another BUI source tree (default: the gem's csrc/, or this checkout)
#   BUI_CMAKE_GENERATOR   a CMake generator (default: Ninja when installed)
#   BUI_SANITIZE=address  builds with AddressSanitizer (Linux, macOS)
#
# SPDX-License-Identifier: GPL-2.0-or-later

require "mkmf"
require "rbconfig"
require "shellwords"

def config(key) = RbConfig::CONFIG.fetch(key, "")

here = __dir__
# A checkout builds the repository's own sources (a csrc/ left by `rake gem` is a copy that goes
# stale as you edit); the published gem has only its csrc/ (Rakefile: vendor).
source = ENV["BUI_SOURCE_DIR"] ||
         [File.expand_path("../../../..", here), File.join(here, "csrc")].find do |dir|
           File.exist?(File.join(dir, "kit", "include", "BUI_kit.h"))
         end
abort "bui: BUI's C sources are not here (set BUI_SOURCE_DIR to a BUI checkout)" unless source

cmake = find_executable("cmake") or
  abort "bui: building BUI needs CMake 3.20 or newer and a C and C++20 compiler"

# The compilers Ruby was built with: the kit's static libraries link into the extension.
compilers = { "C" => config("CC"), "CXX" => config("CXX") }.transform_values do |cmd|
  words = Shellwords.split(cmd)
  # a launcher in front ("ccache gcc") goes to CMake on its own
  launcher = words.shift if words.size > 1 && %w[ccache sccache].include?(File.basename(words.first, ".exe"))
  [launcher, words.join(";")]
end

includes = [config("rubyhdrdir"), config("rubyarchhdrdir")].reject(&:empty?)
definitions = Shellwords.split(config("CPPFLAGS")).grep(/\A-D/).map { |d| d.delete_prefix("-D") }
link = []
link += ["-L#{config('libdir')}", *Shellwords.split(config("LIBRUBYARG_SHARED"))] if config("ENABLE_SHARED") == "yes"
link << "-Wl,-undefined,dynamic_lookup" if RbConfig::CONFIG["host_os"].include?("darwin")
out = File.join(Dir.pwd, "bui_ext.#{config('DLEXT')}")

args = [
  "-S", here, "-B", "build",
  "-DCMAKE_BUILD_TYPE=Release",
  "-DBUI_SOURCE_DIR=#{source}",
  "-DRUBY_INCLUDE_DIRS=#{includes.join(';')}",
  "-DRUBY_DEFINITIONS=#{definitions.join(';')}",
  "-DRUBY_LINK=#{link.join(';')}",
  "-DRUBY_EXT_OUT=#{out}",
  "-DBUI_RUBY_SANITIZE=#{ENV.fetch('BUI_SANITIZE', '')}"
]
compilers.each do |lang, (launcher, compiler)|
  args << "-DCMAKE_#{lang}_COMPILER=#{compiler}" unless compiler.empty?
  args << "-DCMAKE_#{lang}_COMPILER_LAUNCHER=#{launcher}" if launcher
end
generator = ENV["BUI_CMAKE_GENERATOR"] ||
            (find_executable("ninja") && "Ninja") ||
            (RbConfig::CONFIG["host_os"].match?(/mingw|mswin/) && "MinGW Makefiles")
args += ["-G", generator] if generator

system(cmake, *args) or abort "bui: CMake could not configure BUI (see the output above)"

# RubyGems runs `make clean`, `make` and `make install sitearchdir=...`; rake-compiler runs `make`
# and takes bui_ext.<dlext> from here. Paths are in double quotes, which sh and cmd.exe both read.
File.write("Makefile", <<~MAKE)
  CMAKE = "#{cmake}"
  sitearchdir = #{config('sitearchdir')}
  target_prefix = /bui
  DESTDIR =

  all:
  \t$(CMAKE) --build build --config Release --target bui_ext --parallel

  install: all
  \t$(CMAKE) -E make_directory "$(DESTDIR)$(sitearchdir)$(target_prefix)"
  \t$(CMAKE) -E copy "#{out}" "$(DESTDIR)$(sitearchdir)$(target_prefix)"

  clean:
  \t$(CMAKE) -E rm -f "#{out}"

  .PHONY: all install clean
MAKE
