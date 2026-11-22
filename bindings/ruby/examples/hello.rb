# frozen_string_literal: true

# A slider and a button in a native window, with the kit's nodes (no builder).
#
#   ruby -Ilib examples/hello.rb
#
# SPDX-License-Identifier: GPL-2.0-or-later

require "bui"

root = BUI::Node.new("column").set(padding: 8)

size = BUI::Node.new("slider").set(label: "Size", min: 0, max: 10, value: 2.5)
size.on_change { |e| puts "size = #{e.node.value}" }
root << size

apply = BUI::Node.new("button").set(text: "Apply")
apply.on_click do
  puts "clicked"
  BUI.quit
end
root << apply

abort "no native windows here" unless BUI.window_supported?
window = BUI::Window.new("BUI from Ruby", width: 320, height: 120, root: root)
BUI.run
window.free
root.free # the window did not make it, so it is ours
