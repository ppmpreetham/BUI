# frozen_string_literal: true

# SPDX-License-Identifier: GPL-2.0-or-later

require "minitest/autorun"
require "bui"

module Headless
  # Shows root in a headless context, rendered once, for the block; frees both after.
  def show(root, width = 320, height = 200)
    ctx = BUI::Context.new(root)
    raster = BUI::Raster.new(width, height)
    ctx.render(raster, full: true)
    yield ctx, raster
  ensure
    ctx&.free
    root.free
  end

  # Builds a tree under a column root with BUI::Builder.
  def build(&block)
    BUI::Builder.new(BUI::Node.new("column")).build(&block)
  end

  def click(ctx, node)
    ctx.click(*node.rect.center)
  end
end
