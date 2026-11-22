# frozen_string_literal: true

module BUI
  # A native window (Win32, X11, Wayland, Cocoa) showing a BUI::Context.
  #
  #   win = BUI::Window.new("Notes", width: 400, height: 300) do
  #     label "Built with BUI::Builder"
  #   end
  #   BUI.run
  #
  # With no root: or context: it makes a "column" root and a context, and frees
  # them with the window. The block builds into the root (BUI::Builder).
  class Window
    attr_reader :context

    def initialize(title = "BUI", width: 640, height: 480, root: nil, context: nil, show: true,
                   gpu: false, min_size: nil, &build)
      raise ArgumentError, "a window shows a root or a context, not both" if root && context

      @timers = {}
      made_root = root || context ? nil : Node.new("column")
      @owns = context.nil?
      context ||= Context.new(root || made_root)
      begin
        open_native(context, title.to_s, width, height)
      rescue StandardError
        if @owns
          context.free
          made_root&.free
        end
        raise
      end
      use_gpu if gpu
      wm("minsize", *min_size) if min_size
      Builder.new(context.root).build(&build) if build
      show if show
    end

    def root = @context.root

    # Runs the block once after ms milliseconds; returns the timer's id.
    def timer(ms, &block)
      raise ArgumentError, "Window#timer needs a block" unless block

      id = start_timer(ms) or raise Error, "the window holds no more timers"
      @timers[id] = block
      id
    end

    def cancel_timer(id)
      stop_timer(id) unless freed?
      @timers.delete(id)
      nil
    end

    # The block decides whether the window may close (truthy closes it).
    def on_close(&handler)
      @on_close = handler
      self
    end

    # Runs right before each frame request_frame asked for: update the tree for
    # exactly the frame that shows it.
    def on_frame(&handler)
      @on_frame = handler
      self
    end

    # Gets true when the window gains the keyboard focus, false when it loses it.
    def on_focus(&handler)
      @on_focus = handler
      self
    end

    # Runs the block on the kit's thread, from any thread, then draws a frame:
    # the way other threads change what a window shows.
    def update(&block)
      BUI.on_main do
        block.call
        request_frame if open?
      end
    end

    # Frees the window, and the context and tree it made. From inside a callback
    # it closes now and is freed once the callback returns. A second free does nothing.
    def free
      return if freed? || @freeing

      if BUI.in_callback?
        @freeing = true
        close if open?
        BUI.later do
          @freeing = false
          free
        end
        return
      end
      root = @context.root
      free_native
      return unless @owns

      @context.free
      root&.free
    end

    def inspect
      freed? ? "#<BUI::Window freed>" : "#<BUI::Window #{size.join('x')}#{open? ? '' : ' closed'}>"
    end

    private

    # The extension calls these from the window's callbacks.

    def fire_timer(id)
      @timers.delete(id)&.call
    end

    def close_requested
      @on_close ? @on_close.call : true
    end

    def fire_frame
      @on_frame&.call
    end

    def fire_focus(active)
      @on_focus&.call(active)
    end
  end
end
