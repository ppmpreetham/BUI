# frozen_string_literal: true

require "zlib"

module BUI
  # Shows a tree: lays it out, takes input and renders, into a BUI::Raster
  # (headless: tests, screenshots, your own window) or a BUI::Window.
  #
  #   BUI::Context.open(root) do |ctx|
  #     raster = BUI::Raster.new(320, 200)
  #     ctx.render(raster, full: true)
  #     ctx.click(*button.rect.center)
  #   end
  class Context
    # A context for the block's run, freed after it (not the tree).
    def self.open(root)
      ctx = new(root)
      yield ctx
    ensure
      ctx&.free
    end

    # Sees every event of the context's nodes, after their own handlers.
    def on_event(&handler)
      @on_event = handler
      self
    end

    # Runs when the context wants a render (headless hosts; windows render on their own).
    def on_request(&handler)
      @on_request = handler
      self
    end

    # Frees the context (not its tree; its key bindings go with it). From inside
    # one of its callbacks, it is freed once the callback returns.
    def free
      BUI.later { free_native }
    end

    # Called by BUI.dispatch.
    def emit(event)
      @on_event&.call(event)
    end

    private

    # Called by the extension (BKHost.request).
    def request_render
      @on_request&.call
    end
  end

  # A software frame without a window. The GC frees it; free frees it now.
  class Raster
    def size = [width, height]

    # Writes the frame to a PNG file (8-bit RGBA).
    def save_png(path)
      File.binwrite(path, PNG.encode(width, height, rgba))
    end
  end

  # The smallest PNG writer: one IDAT, no filters. Enough for screenshots and tests.
  module PNG
    def self.encode(width, height, rgba)
      row = width * 4
      raw = String.new(capacity: (row + 1) * height, encoding: Encoding::BINARY)
      height.times { |y| raw << "\0" << rgba.byteslice(y * row, row) }
      header = [width, height, 8, 6, 0, 0, 0].pack("NNCCCCC")
      "\x89PNG\r\n\x1A\n".b + chunk("IHDR", header) + chunk("IDAT", Zlib::Deflate.deflate(raw)) + chunk("IEND", "")
    end

    def self.chunk(type, data)
      [data.bytesize].pack("N") + type + data + [Zlib.crc32(type + data)].pack("N")
    end
    private_class_method :chunk
  end
end
