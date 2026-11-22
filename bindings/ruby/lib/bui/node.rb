# frozen_string_literal: true

module BUI
  # RGBA, each 0..1 (made by the extension).
  class Color
    # Color.hex("#3d7fd9"), "#rgb", "#rrggbbaa".
    def self.hex(text)
      digits = text.delete_prefix("#")
      digits = digits.chars.map { |c| c * 2 }.join if [3, 4].include?(digits.size)
      raise ArgumentError, "not a color: #{text.inspect}" unless digits.match?(/\A(\h{6}|\h{8})\z/)

      values = digits.scan(/../).map { |pair| pair.to_i(16) / 255.0 }
      new(*values, *([1.0] * (4 - values.size)))
    end
  end

  # A box in window pixels (made by the extension).
  class Rect
    def center
      [x + w / 2, y + h / 2]
    end
  end

  # A node of the kit's tree: a layout ("column", "row", "frame"...), a widget
  # ("button", "slider", "text"...) or a view ("list", "tree", "canvas"...).
  # BUI::Node.new(type), then properties by name; see BUI_kit.h for the types.
  #
  # Nodes you make are yours until they join a tree: free a root (and with it
  # everything under it) when done, or let a Window free its own.
  class Node
    # The node types whose "value" is text; the others' is a number.
    TEXT_VALUES = %w[text search textarea].freeze

    # Sets a property by name: a String or Symbol, a number, true/false, a
    # BUI::Color or [r, g, b(, a)], or an Array of Strings (a list: "items").
    # Raises ArgumentError for a key this type of node does not have.
    def []=(key, value)
      known = case value
              when String, Symbol then set_str(key, value.to_s)
              when Numeric then set_num(key, value)
              when true, false then set_num(key, value ? 1 : 0)
              when Color then set_color(key, value)
              when Array
                value.all?(String) ? set_strs(key, value) : set_color(key, value)
              else raise TypeError, "#{key}: a #{value.class} is no property value"
              end
      raise ArgumentError, "a #{type} node has no #{key.inspect} for a #{value.class}" unless known
    end

    # Sets several properties: node.set(text: "OK", enabled: false). Returns self.
    def set(**props)
      props.each { |key, value| self[key] = value }
      self
    end

    # Text of labels, buttons and text fields.
    def text = str("text")
    def text=(text)
      self[:text] = text.to_s
    end

    # The value: text for text fields, a number for the others (sliders, check
    # boxes 0/1, drop-downs' index). Setting it does not report :change.
    def value
      TEXT_VALUES.include?(type) ? str("value") : num("value")
    end

    def value=(value)
      self[:value] = value
    end

    def enabled? = num("enabled") != 0
    def enabled=(on)
      self[:enabled] = on
    end

    def visible? = num("visible") != 0
    def visible=(on)
      self[:visible] = on
    end

    # Events the kit sends only to nodes that ask for them ("events"), and :view
    # ("viewevents").
    RAW_EVENTS = %i[press release double motion enter leave key wheel submit configure key_release].freeze

    # Handles a kind of event (BUI::EVENT_KINDS), replacing the last handler; the
    # block gets a BUI::Event. Without a block, removes it. Returns self.
    def on(kind, &handler)
      raise ArgumentError, "no event #{kind.inspect} (BUI::EVENT_KINDS)" unless EVENT_KINDS.include?(kind)

      # the kit sends these only when asked; a node without the flag ignores it
      set_num("events", 1) if handler && RAW_EVENTS.include?(kind)
      set_num("viewevents", 1) if handler && kind == :view
      (@handlers ||= {})[kind] = handler
      self
    end

    def on_click(&) = on(:click, &)
    def on_change(&) = on(:change, &)
    def on_submit(&) = on(:submit, &)

    # Called by BUI.dispatch.
    def emit(event)
      @handlers&.[](event.kind)&.call(event)
    end

    # pack(side: :top | :bottom | :left | :right, fill: :x | :y | :both,
    # expand: true, anchor: "nw", padx: 4 or [left, right], pady:, ipadx:,
    # ipady:, width:, height:): Tk's pack, in a "frame". Returns self.
    def pack(**opts)
      geometry_options(GEOM_PACK, opts)
    end

    # grid(row:, column:, rowspan:, columnspan:, sticky: "nsew", padx:, pady:...)
    def grid(**opts)
      geometry_options(GEOM_GRID, opts)
    end

    # place(x:, y:, relx:, rely:, width:, height:, relwidth:, relheight:, anchor:)
    def place(**opts)
      geometry_options(GEOM_PLACE, opts)
    end

    def inspect
      freed? ? "#<BUI::Node freed>" : "#<BUI::Node #{type}>"
    end

    SIDES = { top: 0, bottom: 1, left: 2, right: 3 }.freeze
    FILLS = { none: 0, x: 1, y: 2, both: 3 }.freeze
    private_constant :SIDES, :FILLS

    # "nsew", :nw, :center as BUI::ANCHOR_* bits.
    def self.anchor_bits(anchor)
      return anchor if anchor.is_a?(Integer)

      text = anchor.to_s
      return 0 if text == "center"

      bits = { "n" => ANCHOR_N, "s" => ANCHOR_S, "w" => ANCHOR_W, "e" => ANCHOR_E }
      text.chars.sum { |c| bits.fetch(c) { raise ArgumentError, "no anchor #{anchor.inspect}" } }
    end

    private

    def geometry_options(manager, opts)
      opts.each do |key, value|
        case key
        when :side then geometry(manager, "side", SIDES.fetch(value) { raise ArgumentError, "no side #{value.inspect}" })
        when :fill then geometry(manager, "fill", FILLS.fetch(value) { raise ArgumentError, "no fill #{value.inspect}" })
        when :anchor, :sticky then geometry(manager, key.to_s, Node.anchor_bits(value))
        when :expand then geometry(manager, "expand", value ? 1 : 0)
        when :padx, :pady
          first, last = Array(value)
          geometry(manager, "#{key}0", first)
          geometry(manager, "#{key}1", last || first)
        when :width, :height
          geometry(manager, manager == GEOM_PLACE ? key.to_s : "req#{key}", value)
        else geometry(manager, key.to_s, value)
        end
      end
      self
    end
  end
end
