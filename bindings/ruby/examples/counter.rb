# frozen_string_literal: true

# A counter built with blocks: the buttons change the count, the arrow keys too.
#
#   ruby -Ilib examples/counter.rb
#   ruby -Ilib examples/counter.rb --screenshot counter.png   # no window (BUI_THEME, BUI_UI_SCALE)
#
# SPDX-License-Identifier: GPL-2.0-or-later

require "bui"

# The count and the label that shows it.
class Counter
  attr_reader :count

  # Builds the counter's widgets with a BUI::Builder. The blocks take the
  # builder as an argument, so self stays this counter (its @shown, its count=).
  def initialize(ui)
    @count = 0
    ui.column padding: 16, spacing: 8 do |col|
      @shown = col.label "0", text_align: "center", bold: true
      col.row spacing: 4 do |row|
        row.button("Decrement") { self.count = count - 1 }
        row.button("Reset") { self.count = 0 }
        row.button("Increment") { self.count = count + 1 }
      end
      col.label "Up and Down count too", text_align: "center"
    end
  end

  def count=(n)
    @count = n
    @shown.text = n.to_s
  end
end

if (at = ARGV.index("--screenshot"))
  scale = ENV.fetch("BUI_UI_SCALE", "1").to_f
  BUI.scale = scale
  root = BUI::Node.new("column")
  Counter.new(BUI::Builder.new(root)).count = 3
  BUI::Context.open(root) do |ctx|
    raster = BUI::Raster.new((300 * scale).round, (100 * scale).round)
    ctx.render(raster, full: true)
    raster.save_png(ARGV.fetch(at + 1))
  end
  root.free
  exit
end

BUI.app do
  win = window("Counter", width: 300, height: 120)
  counter = Counter.new(BUI::Builder.new(win.root))
  win.context.bind("Up") { counter.count += 1 }
  win.context.bind("Down") { counter.count -= 1 }
end
