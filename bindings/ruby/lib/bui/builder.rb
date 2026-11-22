# frozen_string_literal: true

module BUI
  # Builds a tree of nodes with blocks: containers take their children in a
  # block, widgets take their handler. Every method returns its BUI::Node, so a
  # handler can change another node later. Keyword options are the kit's
  # properties by name (padding:, spacing:, enabled:, tooltip:...).
  #
  #   BUI::Builder.new(root).build do
  #     count = 0
  #     column padding: 8, spacing: 4 do
  #       shown = label "0"
  #       row do
  #         button("-") { shown.text = (count -= 1).to_s }
  #         button("+") { shown.text = (count += 1).to_s }
  #       end
  #     end
  #   end
  #
  # The block runs with the builder as self (instance_exec); one that takes an
  # argument gets the builder instead and keeps your own self:
  #
  #   build { |ui| ui.button("Save") { save } }
  class Builder
    def initialize(parent)
      @parent = parent
    end

    # Runs the block against this builder; returns the parent.
    def build(&block)
      block.arity == 1 ? block.call(self) : instance_exec(&block)
      @parent
    end

    # -- containers: the block builds the children --------------------------------

    def column(**props, &children) = container("column", props, children)
    def row(**props, &children) = container("row", props, children)
    def box(**props, &children) = container("box", props, children)
    def grid(**props, &children) = container("grid", props, children)
    def scroll(**props, &children) = container("scroll", props, children)
    # Children placed with Node#pack, #grid or #place (Tk's layout managers).
    def frame(**props, &children) = container("frame", props, children)

    # A collapsible panel, with Blender's header and animation.
    def panel(title, open: true, **props, &children)
      container("panel", { text: title, collapsed: !open, **props }, children)
    end

    # Any node type: node("tabs", ...) { children }.
    def node(type, **props, &children) = container(type, props, children)

    # -- widgets: the block handles their event ------------------------------------

    def label(text, **props) = add("label", text: text.to_s, **props)
    def separator(**props) = add("separator", **props)
    def progress(value = 0.0, **props) = add("progress", value: value, **props)

    # The block runs on each click with the BUI::Event.
    def button(text, **props, &on_click)
      handle(add("button", text: text.to_s, **props), :click, on_click)
    end

    # The blocks of checkbox and toggle get true/false and the event.
    def checkbox(text, value: false, **props, &on_change)
      bool(add("checkbox", label: text.to_s, value: value, **props), on_change)
    end

    def toggle(text, value: false, **props, &on_change)
      bool(add("toggle", text: text.to_s, value: value, **props), on_change)
    end

    # The blocks of slider and number get the new Float and the event.
    def slider(label = nil, value: 0.0, min: 0.0, max: 1.0, **props, &on_change)
      number_widget("slider", label, value, min, max, props, on_change)
    end

    def number(label = nil, value: 0.0, min: nil, max: nil, **props, &on_change)
      number_widget("number", label, value, min, max, props, on_change)
    end

    # A one-line text field; the block gets the text and the event as it is typed.
    # Node#on_submit handles Return.
    def text_field(value = "", placeholder: nil, **props, &on_change)
      opts = { value: value.to_s, live: true, **props }
      opts[:placeholder] = placeholder if placeholder
      text(add("text", **opts), on_change)
    end

    # Several lines of text; the block gets the text and the event.
    def text_area(value = "", **props, &on_change)
      text(add("textarea", value: value.to_s, **props), on_change)
    end

    # A drop-down of items; the block gets the chosen index, its item and the event.
    def dropdown(items, value: 0, **props, &on_change)
      node = add("dropdown", items: items.map(&:to_s), value: value, **props)
      return node unless on_change

      node.on(:change) do |e|
        index = e.node.num("value").to_i
        on_change.call(index, e.node.strs("items")[index], e)
      end
    end

    # A button opening a menu: items as Node#menu= takes them.
    def menu_button(text, items, **props)
      add("menubutton", text: text.to_s, **props).tap { |n| n.menu = items }
    end

    # An image file, decoded in the background at the size it is drawn.
    def image(src, fit: :contain, **props)
      add("image", **props).tap { |n| n.set_image_source(src, fit) }
    end

    def canvas(**props) = add("canvas", **props)
    def tree(**props) = add("tree", **props)

    private

    def add(type, **props)
      Node.new(type).tap do |n|
        n.set(**props)
        @parent << n
      rescue StandardError
        n.free
        raise
      end
    end

    def container(type, props, children)
      node = add(type, **props)
      return node unless children

      outer = @parent
      @parent = node
      begin
        children.arity == 1 ? children.call(self) : instance_exec(&children)
      ensure
        @parent = outer
      end
      node
    end

    def handle(node, kind, block)
      block ? node.on(kind, &block) : node
    end

    def bool(node, on_change)
      return node unless on_change

      node.on(:change) { |e| on_change.call(e.node.num("value") != 0, e) }
    end

    def text(node, on_change)
      return node unless on_change

      node.on(:change) { |e| on_change.call(e.node.str("value"), e) }
    end

    def number_widget(type, label, value, min, max, props, on_change)
      opts = {}
      opts[:label] = label.to_s if label
      opts[:min] = min if min
      opts[:max] = max if max
      # the value last: the kit clamps it to the range set before
      node = add(type, **opts, **props, value: value.to_f)
      return node unless on_change

      node.on(:change) { |e| on_change.call(e.node.num("value"), e) }
    end
  end
end
