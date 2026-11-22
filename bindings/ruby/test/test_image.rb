# frozen_string_literal: true

# SPDX-License-Identifier: GPL-2.0-or-later

require "tmpdir"
require "test_helper"

class TestImage < Minitest::Test
  include Headless

  def test_from_rgba
    pixels = [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255].pack("C*")
    img = BUI::Image.from_rgba(pixels, 2, 2)
    assert_equal [2, 2, 1], [img.width, img.height, img.frames]
    assert_equal pixels, img.rgba
    assert_raises(ArgumentError) { BUI::Image.from_rgba(pixels, 2, 2, 4) } # rows overlap
    assert_raises(ArgumentError) { BUI::Image.from_rgba(pixels, 3, 2) }
    assert_raises(IndexError) { img.rgba(1) }
  end

  def test_a_frame_saved_and_loaded
    root = BUI::Node.new("column").set(padding: 8)
    root << BUI::Node.new("button").set(text: "Saved")
    Dir.mktmpdir do |dir|
      path = File.join(dir, "frame.png")
      show(root, 120, 40) do |_, raster|
        raster.save_png(path)
        img = BUI::Image.load(path)
        assert_equal [120, 40], [img.width, img.height]
        assert_equal "png", img.format
        assert_equal raster.rgba, img.rgba
        half = BUI::Image.decode(File.binread(path), 60, 0)
        assert_equal [60, 20], [half.width, half.height]
      end
    end
    assert_raises(BUI::Error) { BUI::Image.decode("not an image") }
    assert_raises(BUI::Error) { BUI::Image.load("/no/such/file.png") }
  end

  def test_a_node_keeps_its_image
    node = BUI::Node.new("image")
    node.image = BUI::Image.from_rgba("\x80".b * 16, 2, 2)
    GC.start
    show(node, 20, 20) { |ctx, raster| ctx.render(raster, full: true) }
  end
end
