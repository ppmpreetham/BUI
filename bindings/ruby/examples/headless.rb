# frozen_string_literal: true

# BUI without a window: render a frame, click a button, save the frame as a PNG.
#
#   ruby -Ilib examples/headless.rb [frame.png]
#
# SPDX-License-Identifier: GPL-2.0-or-later

require "bui"

root = BUI::Node.new("column").set(padding: 12)
root << BUI::Node.new("label").set(text: "BUI from Ruby")
clicks = 0
button = BUI::Node.new("button").set(text: "Click me").on_click { clicks += 1 }
root << button

BUI::Context.open(root) do |ctx|
  raster = BUI::Raster.new(320, 200)
  regions = ctx.render(raster, full: true)
  puts "rendered #{regions} region(s) into #{raster.width}x#{raster.height}"
  ctx.click(*button.rect.center)
  puts "clicks: #{clicks}; needs a render: #{ctx.needs_render?}"
  raster.save_png(ARGV[0]) if ARGV[0]
end
root.free
