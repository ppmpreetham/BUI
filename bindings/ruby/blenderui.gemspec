# frozen_string_literal: true

# The blenderui gem: `gem install blenderui`, then `require "bui"`.
# SPDX-License-Identifier: GPL-2.0-or-later

require_relative "lib/bui/version"

Gem::Specification.new do |spec|
  spec.name = "blenderui"
  spec.version = BUI::VERSION
  spec.authors = ["BUI contributors"]
  spec.summary = "Blender's UI as a library: widgets, layout and native windows for Ruby"
  spec.description = <<~TEXT
    BUI is Blender's own interface toolkit, decoupled from Blender. This gem is a C extension
    over BUI's C kit (no FFI): Blender's widgets, layouts, menus, trees and file browser in
    native windows (Win32, X11, Wayland, Cocoa) or rendered without a window, with a block
    builder for Ruby. It builds the kit from source with CMake and a C and C++20 compiler.
  TEXT
  spec.homepage = "https://github.com/ppmpreetham/BUI"
  spec.license = "GPL-2.0-or-later"
  spec.required_ruby_version = ">= 3.1"
  spec.metadata = {
    "homepage_uri" => spec.homepage,
    "source_code_uri" => "https://github.com/ppmpreetham/BUI/tree/main/bindings/ruby",
    "changelog_uri" => "https://github.com/ppmpreetham/BUI/blob/main/CHANGELOG.md",
    "rubygems_mfa_required" => "true"
  }

  # ext/bui/csrc is the kit's C sources (rake vendor), so the gem builds without a checkout
  spec.files = Dir["lib/**/*.rb", "ext/bui/*.{c,h,rb}", "ext/bui/CMakeLists.txt", "ext/bui/csrc/**/*",
                   "README.md"]
  spec.extensions = ["ext/bui/extconf.rb"]
  spec.require_paths = ["lib"]
end
