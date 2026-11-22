/* BUI for Ruby: BUI::Node (the kit's nodes, their properties, layout managers,
 * images, canvas items, text areas, trees and menus) and BUI::TreeItem.
 *
 * A node's wrapper is its borrowed ref: the kit retains it while the node
 * lives, so the wrapper (and the handlers lib/ keeps on it) stays alive with
 * the node, and lets go of it when the kit frees it, whoever frees it.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "bui_ext.h"

VALUE bui_cNode;
VALUE bui_cTreeItem;

static ID id_image;

/* -------------------------------------------------------------------- */
/** \name Wrappers
 * \{ */

static const rb_data_type_t node_type = {
    .wrap_struct_name = "BUI::Node",
    .flags = RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED,
};

static VALUE node_alloc(VALUE klass)
{
  return TypedData_Wrap_Struct(klass, &node_type, NULL);
}

static void node_attach(VALUE self, BKNode *n)
{
  RTYPEDDATA_DATA(self) = n;
  const uintptr_t id = bui_ref_new(self, BUI_REF_NODE);
  BK_node_set_borrowed_ref(n, (void *)id); /* the kit retains it: one reference */
}

VALUE bui_node_wrap(BKNode *n)
{
  if (!n) {
    return Qnil;
  }
  const VALUE known = bui_ref_get((uintptr_t)BK_node_borrowed_ref(n));
  if (known != Qundef) {
    return known;
  }
  VALUE self = node_alloc(bui_cNode);
  node_attach(self, n);
  return self;
}

void bui_node_forget(VALUE wrapper)
{
  if (rb_typeddata_is_kind_of(wrapper, &node_type)) {
    RTYPEDDATA_DATA(wrapper) = NULL;
  }
}

BKNode *bui_node_ptr(VALUE v)
{
  BKNode *n = (BKNode *)rb_check_typeddata(v, &node_type);
  if (!n) {
    rb_raise(bui_eFreed, "the node was freed");
  }
  return n;
}

BKNode *bui_node_ptr_opt(VALUE v)
{
  return NIL_P(v) ? NULL : bui_node_ptr(v);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name The tree
 * \{ */

/* BUI::Node.new("button"): a node of a type the kit knows (BUI_kit.h lists them). */
static VALUE node_initialize(VALUE self, VALUE type)
{
  if (RTYPEDDATA_DATA(self)) {
    rb_raise(rb_eRuntimeError, "the node is made already");
  }
  BKNode *n = BK_node_new(bui_cstr(type));
  if (!n) {
    rb_raise(rb_eArgError, "no node type %+" PRIsVALUE, type);
  }
  node_attach(self, n);
  return self;
}

static VALUE node_type_name(VALUE self)
{
  return bui_str(BK_node_type(bui_node_ptr(self)));
}

/* Frees the node and everything under it (it leaves its parent first); their
 * wrappers become freed. A second free does nothing. */
static VALUE node_free(VALUE self)
{
  BKNode *n = (BKNode *)RTYPEDDATA_DATA(self);
  if (n) {
    BK_node_free(n);
  }
  return Qnil;
}

static VALUE node_freed_p(VALUE self)
{
  return RTYPEDDATA_DATA(self) ? Qfalse : Qtrue;
}

/* add(child, index = -1): moves child here (from any parent), at index (-1:
 * last); returns self. */
static VALUE node_add(int argc, VALUE *argv, VALUE self)
{
  VALUE child, index;
  rb_scan_args(argc, argv, "11", &child, &index);
  BKNode *p = bui_node_ptr(self), *c = bui_node_ptr(child);
  if (!BK_node_add(p, c, NIL_P(index) ? -1 : NUM2INT(index))) {
    rb_raise(rb_eArgError, "a node cannot go under itself");
  }
  return self;
}

static VALUE node_push(VALUE self, VALUE child)
{
  return node_add(1, &child, self);
}

/* Takes child out (not freed: it is yours again); returns child. */
static VALUE node_remove(VALUE self, VALUE child)
{
  BKNode *p = bui_node_ptr(self), *c = bui_node_ptr(child);
  if (BK_node_parent(c) != p) {
    rb_raise(rb_eArgError, "not a child of this node");
  }
  BK_node_remove(p, c);
  return child;
}

static VALUE node_child_count(VALUE self)
{
  return INT2NUM(BK_node_child_count(bui_node_ptr(self)));
}

static VALUE node_child(VALUE self, VALUE i)
{
  return bui_node_wrap(BK_node_child(bui_node_ptr(self), NUM2INT(i)));
}

static VALUE node_children(VALUE self)
{
  BKNode *n = bui_node_ptr(self);
  const int count = BK_node_child_count(n);
  VALUE out = rb_ary_new_capa(count);
  for (int i = 0; i < count; i++) {
    rb_ary_push(out, bui_node_wrap(BK_node_child(n, i)));
  }
  return out;
}

static VALUE node_parent(VALUE self)
{
  return bui_node_wrap(BK_node_parent(bui_node_ptr(self)));
}

/* The box from the last layout, in window pixels: a BUI::Rect. */
static VALUE node_rect(VALUE self)
{
  int r[4];
  BK_node_rect(bui_node_ptr(self), r);
  return bui_rect_new(r);
}

/* The size the node asks for: [width, height]. */
static VALUE node_natural(VALUE self)
{
  int s[2];
  BK_node_natural(bui_node_ptr(self), s);
  return rb_assoc_new(INT2NUM(s[0]), INT2NUM(s[1]));
}

static VALUE node_invalidate(VALUE self)
{
  BK_node_invalidate(bui_node_ptr(self));
  return self;
}

static VALUE node_relayout(VALUE self)
{
  BK_node_relayout(bui_node_ptr(self));
  return self;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Properties
 *
 * The kit names properties ("value", "text", "items") and types them by node:
 * a text field's "value" is a string, a slider's a number. The setters return
 * false for a key the node does not know.
 * \{ */

static VALUE node_set_num(VALUE self, VALUE key, VALUE value)
{
  BKNode *n = bui_node_ptr(self);
  const double v = NUM2DBL(value);
  return BK_set_num(n, bui_cstr(key), v) ? Qtrue : Qfalse;
}

static VALUE node_set_str(VALUE self, VALUE key, VALUE value)
{
  BKNode *n = bui_node_ptr(self);
  const char *k = bui_cstr(key);
  return BK_set_str(n, k, bui_cstr(value)) ? Qtrue : Qfalse;
}

static VALUE node_set_color(VALUE self, VALUE key, VALUE value)
{
  BKNode *n = bui_node_ptr(self);
  float c[4];
  bui_rgba(value, c);
  return BK_set_color(n, bui_cstr(key), c) ? Qtrue : Qfalse;
}

typedef struct SetStrs {
  BKNode *node;
  VALUE key, values;
} SetStrs;

static VALUE set_strs_body(BuiArena *a, void *p)
{
  const SetStrs *s = (const SetStrs *)p;
  const long n = RARRAY_LEN(s->values);
  if (n > INT_MAX) {
    rb_raise(rb_eArgError, "too many strings");
  }
  const char **list = (const char **)bui_arena_alloc(a, sizeof(char *) * (size_t)n);
  for (long i = 0; i < n; i++) {
    list[i] = bui_arena_str(a, rb_ary_entry(s->values, i));
    if (!list[i]) {
      rb_raise(rb_eTypeError, "nil in a list of strings");
    }
  }
  const char *key = bui_arena_str(a, s->key);
  return BK_set_strs(s->node, key, list, (int)n) ? Qtrue : Qfalse;
}

static VALUE node_set_strs(VALUE self, VALUE key, VALUE values)
{
  Check_Type(values, T_ARRAY);
  SetStrs s = {bui_node_ptr(self), key, values};
  return bui_with_arena(set_strs_body, &s);
}

static VALUE node_num(VALUE self, VALUE key)
{
  BKNode *n = bui_node_ptr(self);
  return DBL2NUM(BK_get_num(n, bui_cstr(key)));
}

static VALUE node_str(VALUE self, VALUE key)
{
  BKNode *n = bui_node_ptr(self);
  return bui_str(BK_get_str(n, bui_cstr(key)));
}

static VALUE node_color(VALUE self, VALUE key)
{
  BKNode *n = bui_node_ptr(self);
  float c[4];
  return BK_get_color(n, bui_cstr(key), c) ? bui_color_new(c) : Qnil;
}

static VALUE node_strs(VALUE self, VALUE key)
{
  BKNode *n = bui_node_ptr(self);
  const char *k = bui_cstr(key);
  const int count = BK_strs_count(n, k);
  VALUE out = rb_ary_new_capa(count);
  for (int i = 0; i < count; i++) {
    /* a new String may run the GC: the key is read again, as it may have moved */
    rb_ary_push(out, bui_str(BK_strs_get(n, bui_cstr(key), i)));
  }
  return out;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Layout managers (children of a "frame": Tk's pack, grid, place)
 * \{ */

/* geometry(manager, key, value): manager 1 pack, 2 grid, 3 place; lib/ names them. */
static VALUE node_geometry(VALUE self, VALUE manager, VALUE key, VALUE value)
{
  BKNode *n = bui_node_ptr(self);
  const int m = NUM2INT(manager);
  if (m < BK_GEOM_PACK || m > BK_GEOM_PLACE) {
    rb_raise(rb_eArgError, "no layout manager %d", m);
  }
  if (BK_geom_manager(n) != m) {
    BK_geom_set_manager(n, m);
  }
  const double v = NUM2DBL(value);
  BK_geom_set(n, bui_cstr(key), v);
  return self;
}

static VALUE node_geometry_manager(VALUE self)
{
  return INT2NUM(BK_geom_manager(bui_node_ptr(self)));
}

static VALUE node_geometry_get(VALUE self, VALUE key)
{
  BKNode *n = bui_node_ptr(self);
  return DBL2NUM(BK_geom_get(n, bui_cstr(key)));
}

/* grid_weight(axis, index, weight, minsize = 0): axis :column or :row. */
static VALUE node_grid_weight(int argc, VALUE *argv, VALUE self)
{
  VALUE axis, index, weight, minsize;
  rb_scan_args(argc, argv, "31", &axis, &index, &weight, &minsize);
  int column;
  if (axis == ID2SYM(rb_intern("column"))) {
    column = 1;
  }
  else if (axis == ID2SYM(rb_intern("row"))) {
    column = 0;
  }
  else {
    rb_raise(rb_eArgError, "the axis is :column or :row, not %" PRIsVALUE, axis);
  }
  BK_frame_grid_weight(bui_node_ptr(self),
                       column,
                       NUM2INT(index),
                       NUM2DBL(weight),
                       NIL_P(minsize) ? 0 : NUM2INT(minsize));
  return self;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Images on nodes (labels, buttons, "image" nodes)
 * \{ */

static int fit_of(VALUE fit)
{
  static const char *const names[] = {"fill", "contain", "cover", "none", "scale_down"};
  if (NIL_P(fit)) {
    return BK_FIT_CONTAIN;
  }
  for (int i = 0; i < 5; i++) {
    if (fit == ID2SYM(rb_intern(names[i]))) {
      return i; /* BK_FIT_* in this order */
    }
  }
  rb_raise(rb_eArgError,
           "fit is :fill, :contain, :cover, :none or :scale_down, not %" PRIsVALUE,
           fit);
}

/* set_image_source(src, fit = :contain): an image file, decoded in the
 * background at the size it is drawn. */
static VALUE node_set_image_source(int argc, VALUE *argv, VALUE self)
{
  VALUE src, fit;
  rb_scan_args(argc, argv, "11", &src, &fit);
  BKNode *n = bui_node_ptr(self);
  const int f = fit_of(fit);
  FilePathValue(src);
  BK_node_set_image_source(n, bui_cstr(src), f);
  rb_ivar_set(self, id_image, Qnil);
  return self;
}

/* image = BUI::Image or nil: shown until replaced; the node keeps the image alive. */
static VALUE node_set_image(VALUE self, VALUE image)
{
  BKNode *n = bui_node_ptr(self);
  BK_node_set_imbuf(n, NIL_P(image) ? NULL : bui_image_ptr(image));
  rb_ivar_set(self, id_image, image);
  return image;
}

/* image_frame = n: an animated image plays (0) or shows frame n - 1. */
static VALUE node_set_image_frame(VALUE self, VALUE frame)
{
  BK_node_set_image_frame(bui_node_ptr(self), NUM2INT(frame));
  return frame;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Canvas items ("canvas" nodes: Tk's Canvas)
 * \{ */

static ID id_k_id, id_k_type, id_k_coords, id_k_fill, id_k_outline, id_k_width, id_k_radius,
    id_k_size, id_k_start, id_k_extent, id_k_text, id_k_anchor, id_k_src, id_k_fit, id_k_hidden,
    id_k_bold, id_k_italic, id_k_underline, id_k_family, id_k_rgba, id_k_image_size, id_k_frame,
    id_k_no_hit;

static VALUE item_opt(VALUE hash, ID key)
{
  return rb_hash_lookup(hash, ID2SYM(key));
}

static int item_type_of(VALUE type)
{
  static const char *const names[] = {
      "rect", "oval", "line", "polygon", "text", "arc", "image", "roundrect"};
  for (int i = 0; i < 8; i++) {
    if (type == ID2SYM(rb_intern(names[i]))) {
      return i; /* BK_ITEM_* in this order */
    }
  }
  rb_raise(rb_eArgError,
           "an item type is :rect, :oval, :line, :polygon, :text, :arc, :image or :roundrect, "
           "not %" PRIsVALUE,
           type);
}

/* Fonts named by text items: the kit does not copy the name, so each is kept
 * for the life of the process (a program names a few). */
static st_table *g_families;

static const char *family_of(VALUE v)
{
  if (NIL_P(v)) {
    return NULL;
  }
  const char *s = bui_cstr(v);
  st_data_t kept;
  if (st_lookup(g_families, (st_data_t)s, &kept)) {
    return (const char *)kept;
  }
  char *copy = ruby_strdup(s);
  st_insert(g_families, (st_data_t)copy, (st_data_t)copy);
  return copy;
}

static void item_fill(BuiArena *a, VALUE h, BKCanvasItem *it)
{
  Check_Type(h, T_HASH);
  VALUE v;
  it->id = NIL_P(v = item_opt(h, id_k_id)) ? 0 : NUM2INT(v);
  it->type = item_type_of(item_opt(h, id_k_type));
  if (!NIL_P(v = item_opt(h, id_k_coords))) {
    Check_Type(v, T_ARRAY);
    const long n = RARRAY_LEN(v);
    if (n > INT_MAX) {
      rb_raise(rb_eArgError, "too many coordinates");
    }
    float *coords = (float *)bui_arena_alloc(a, sizeof(float) * (size_t)n);
    for (long i = 0; i < n; i++) {
      coords[i] = (float)NUM2DBL(rb_ary_entry(v, i));
    }
    it->coords = coords;
    it->ncoords = (int)n;
  }
  if (!NIL_P(v = item_opt(h, id_k_fill))) {
    bui_rgba(v, it->fill);
  }
  if (!NIL_P(v = item_opt(h, id_k_outline))) {
    bui_rgba(v, it->outline);
  }
  it->width = NIL_P(v = item_opt(h, id_k_width)) ? 1.0f : (float)NUM2DBL(v);
  it->radius = NIL_P(v = item_opt(h, id_k_radius)) ? 0.0f : (float)NUM2DBL(v);
  it->size = NIL_P(v = item_opt(h, id_k_size)) ? 0.0f : (float)NUM2DBL(v);
  it->start = NIL_P(v = item_opt(h, id_k_start)) ? 0.0f : (float)NUM2DBL(v);
  it->extent = NIL_P(v = item_opt(h, id_k_extent)) ? 0.0f : (float)NUM2DBL(v);
  it->anchor = NIL_P(v = item_opt(h, id_k_anchor)) ? 0 : NUM2INT(v);
  it->fit = fit_of(item_opt(h, id_k_fit));
  it->frame = NIL_P(v = item_opt(h, id_k_frame)) ? 0 : NUM2INT(v);
  it->hidden = RTEST(item_opt(h, id_k_hidden));
  it->no_hit = RTEST(item_opt(h, id_k_no_hit));
  it->text_flags = (RTEST(item_opt(h, id_k_bold)) ? BUI_FONT_BOLD : 0) |
                   (RTEST(item_opt(h, id_k_italic)) ? BUI_FONT_ITALIC : 0) |
                   (RTEST(item_opt(h, id_k_underline)) ? BUI_FONT_UNDERLINE : 0);
  it->family = family_of(item_opt(h, id_k_family));
  it->text = bui_arena_str(a, item_opt(h, id_k_text));
  if (!NIL_P(v = item_opt(h, id_k_src))) {
    FilePathValue(v);
    it->src = bui_arena_str(a, v);
  }
  if (!NIL_P(v = item_opt(h, id_k_rgba))) {
    /* straight RGBA pixels, with image_size: [w, h] */
    StringValue(v);
    VALUE size = item_opt(h, id_k_image_size);
    Check_Type(size, T_ARRAY);
    const int w = NUM2INT(rb_ary_entry(size, 0)), hgt = NUM2INT(rb_ary_entry(size, 1));
    if (w <= 0 || hgt <= 0 || (long)w > RSTRING_LEN(v) / 4 / hgt) {
      rb_raise(rb_eArgError, "rgba holds fewer than %d x %d pixels", w, hgt);
    }
    unsigned char *px = (unsigned char *)bui_arena_alloc(a, (size_t)w * (size_t)hgt * 4);
    memcpy(px, RSTRING_PTR(v), (size_t)w * (size_t)hgt * 4);
    it->rgba = px;
    it->iw = w;
    it->ih = hgt;
  }
}

typedef struct CanvasCall {
  BKNode *node;
  VALUE item;
  int how, index; /* 0 set, 1 add, 2 put at index */
} CanvasCall;

static VALUE canvas_body(BuiArena *a, void *p)
{
  const CanvasCall *c = (const CanvasCall *)p;
  BKCanvasItem it;
  memset(&it, 0, sizeof(it));
  item_fill(a, c->item, &it);
  switch (c->how) {
    case 0:
      BK_canvas_set_item(c->node, &it);
      break;
    case 1:
      BK_canvas_add_item(c->node, &it);
      break;
    default:
      BK_canvas_put_item(c->node, c->index, &it);
      break;
  }
  return Qnil;
}

static void canvas_call(VALUE self, VALUE item, int how, int index)
{
  CanvasCall c = {bui_node_ptr(self), item, how, index};
  bui_with_arena(canvas_body, &c);
}

/* canvas_set(id: 1, type: :rect, coords: [x0, y0, x1, y1], fill: color, ...):
 * inserts or replaces the item with this id. */
static VALUE node_canvas_set(VALUE self, VALUE item)
{
  canvas_call(self, item, 0, 0);
  return self;
}

static VALUE node_canvas_add(VALUE self, VALUE item)
{
  canvas_call(self, item, 1, 0);
  return self;
}

/* canvas_put(index, item): overwrites the item at index (index == count
 * appends), for lists rebuilt every frame; canvas_truncate(count) drops the rest. */
static VALUE node_canvas_put(VALUE self, VALUE index, VALUE item)
{
  BKNode *n = bui_node_ptr(self);
  const int i = NUM2INT(index);
  if (i < 0 || i > BK_canvas_count(n)) {
    rb_raise(rb_eIndexError, "index %d outside 0..%d", i, BK_canvas_count(n));
  }
  canvas_call(self, item, 2, i);
  return self;
}

static VALUE node_canvas_truncate(VALUE self, VALUE count)
{
  BK_canvas_truncate(bui_node_ptr(self), NUM2INT(count));
  return self;
}

/* canvas_delete(id), or canvas_delete(:all). */
static VALUE node_canvas_delete(VALUE self, VALUE id)
{
  BK_canvas_delete(bui_node_ptr(self), id == ID2SYM(rb_intern("all")) ? -1 : NUM2INT(id));
  return self;
}

static VALUE node_canvas_raise(int argc, VALUE *argv, VALUE self)
{
  VALUE id, to_top;
  rb_scan_args(argc, argv, "11", &id, &to_top);
  BK_canvas_raise(bui_node_ptr(self), NUM2INT(id), NIL_P(to_top) || RTEST(to_top));
  return self;
}

static VALUE node_canvas_count(VALUE self)
{
  return INT2NUM(BK_canvas_count(bui_node_ptr(self)));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Text areas: the text is the "value" string; offsets are bytes
 * \{ */

static int text_offset(BKNode *n, VALUE offset)
{
  const int o = NUM2INT(offset);
  const char *text = BK_get_str(n, "value");
  const long len = text ? (long)strlen(text) : 0;
  if (o < 0 || o > len) {
    rb_raise(rb_eIndexError, "byte offset %d outside 0..%ld", o, len);
  }
  return o;
}

static VALUE node_text_insert(VALUE self, VALUE offset, VALUE text)
{
  BKNode *n = bui_node_ptr(self);
  const int o = text_offset(n, offset);
  BK_textarea_insert(n, o, bui_cstr(text));
  return self;
}

static VALUE node_text_delete(VALUE self, VALUE start, VALUE end)
{
  BKNode *n = bui_node_ptr(self);
  const int a = text_offset(n, start), b = text_offset(n, end);
  if (a > b) {
    rb_raise(rb_eArgError, "start %d after end %d", a, b);
  }
  BK_textarea_delete(n, a, b);
  return self;
}

static VALUE node_caret(VALUE self)
{
  return INT2NUM(BK_textarea_caret(bui_node_ptr(self)));
}

static VALUE node_set_caret(VALUE self, VALUE offset)
{
  BKNode *n = bui_node_ptr(self);
  BK_textarea_set_caret(n, text_offset(n, offset));
  return offset;
}

/* The selected byte range, nil when nothing is selected. */
static VALUE node_selection(VALUE self)
{
  int a, b;
  if (!BK_textarea_selection(bui_node_ptr(self), &a, &b)) {
    return Qnil;
  }
  return rb_range_new(INT2NUM(a), INT2NUM(b), 1);
}

static VALUE node_select(VALUE self, VALUE start, VALUE end)
{
  BKNode *n = bui_node_ptr(self);
  BK_textarea_set_selection(n, text_offset(n, start), text_offset(n, end));
  return self;
}

static VALUE node_text_see(VALUE self, VALUE offset)
{
  BKNode *n = bui_node_ptr(self);
  BK_textarea_see(n, text_offset(n, offset));
  return self;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Tree items
 *
 * An item is owned by its wrapper: parents and trees retain it, and the GC
 * frees it once neither Ruby nor the kit holds it.
 * \{ */

typedef struct TreeItem {
  BKTreeItem *item;
  uintptr_t id;
} TreeItem;

static void tree_item_free(void *p)
{
  TreeItem *t = (TreeItem *)p;
  if (t->item && BK_tree_item_parent(t->item)) {
    /* a parent keeps its children alive, so this is the exit, which frees
     * everything in any order: without a ref, the parent frees the item */
    BK_tree_item_set_ref(t->item, NULL);
  }
  else if (t->item) {
    BK_tree_item_free(t->item);
  }
  bui_ref_remove(t->id);
  xfree(t);
}

static size_t tree_item_size(const void *p)
{
  return sizeof(TreeItem);
}

static const rb_data_type_t tree_item_type = {
    .wrap_struct_name = "BUI::TreeItem",
    .function = {.dfree = tree_item_free, .dsize = tree_item_size},
    .flags = RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED,
};

static VALUE tree_item_alloc(VALUE klass)
{
  TreeItem *t;
  VALUE self = TypedData_Make_Struct(klass, TreeItem, &tree_item_type, t);
  return self;
}

BKTreeItem *bui_tree_item_ptr(VALUE v)
{
  TreeItem *t = (TreeItem *)rb_check_typeddata(v, &tree_item_type);
  if (!t->item) {
    rb_raise(bui_eError, "the tree item is not initialized");
  }
  return t->item;
}

VALUE bui_tree_item_wrap(BKTreeItem *item)
{
  if (!item) {
    return Qnil;
  }
  const VALUE v = bui_ref_get((uintptr_t)BK_tree_item_ref(item));
  return v == Qundef ? Qnil : v;
}

/* BUI::TreeItem.new(label, icon = 0) */
static VALUE tree_item_initialize(int argc, VALUE *argv, VALUE self)
{
  VALUE label, icon;
  rb_scan_args(argc, argv, "11", &label, &icon);
  TreeItem *t = (TreeItem *)rb_check_typeddata(self, &tree_item_type);
  if (t->item) {
    rb_raise(rb_eRuntimeError, "the tree item is made already");
  }
  const int ic = NIL_P(icon) ? 0 : NUM2INT(icon);
  t->item = BK_tree_item_new(bui_cstr(label), ic);
  t->id = bui_ref_new(self, BUI_REF_KEEP);
  BK_tree_item_set_ref(t->item, (void *)t->id);
  return self;
}

/* add(child): child must have no parent; returns child. */
static VALUE tree_item_add(VALUE self, VALUE child)
{
  BKTreeItem *p = bui_tree_item_ptr(self), *c = bui_tree_item_ptr(child);
  if (BK_tree_item_parent(c)) {
    rb_raise(rb_eArgError, "the item has a parent already");
  }
  for (const BKTreeItem *a = p; a; a = BK_tree_item_parent(a)) {
    if (a == c) {
      rb_raise(rb_eArgError, "an item cannot go under itself");
    }
  }
  BK_tree_item_add(p, c);
  return child;
}

static VALUE tree_item_clear(VALUE self)
{
  BK_tree_item_clear(bui_tree_item_ptr(self));
  return self;
}

static VALUE tree_item_label(VALUE self)
{
  return bui_str(BK_tree_item_label(bui_tree_item_ptr(self)));
}

static VALUE tree_item_set_label(VALUE self, VALUE label)
{
  BKTreeItem *it = bui_tree_item_ptr(self);
  BK_tree_item_set_label(it, bui_cstr(label));
  return label;
}

/* item["expanded"], item["marked"], item["icon"], item["toggle0"]... */
static VALUE tree_item_get(VALUE self, VALUE key)
{
  BKTreeItem *it = bui_tree_item_ptr(self);
  return DBL2NUM(BK_tree_item_get(it, bui_cstr(key)));
}

static VALUE tree_item_set(VALUE self, VALUE key, VALUE value)
{
  BKTreeItem *it = bui_tree_item_ptr(self);
  const double v = value == Qtrue ? 1.0 : value == Qfalse ? 0.0 : NUM2DBL(value);
  BK_tree_item_set(it, bui_cstr(key), v);
  return value;
}

typedef struct ItemValues {
  BKTreeItem *item;
  VALUE values;
} ItemValues;

static VALUE item_values_body(BuiArena *a, void *p)
{
  const ItemValues *iv = (const ItemValues *)p;
  const long n = RARRAY_LEN(iv->values);
  if (n > INT_MAX) {
    rb_raise(rb_eArgError, "too many values");
  }
  const char **list = (const char **)bui_arena_alloc(a, sizeof(char *) * (size_t)n);
  for (long i = 0; i < n; i++) {
    list[i] = bui_arena_str(a, rb_String(rb_ary_entry(iv->values, i)));
  }
  BK_tree_item_set_values(iv->item, list, (int)n);
  return Qnil;
}

/* values = ["12 KB", "Today"]: the texts of the tree's other columns. */
static VALUE tree_item_set_values(VALUE self, VALUE values)
{
  Check_Type(values, T_ARRAY);
  ItemValues iv = {bui_tree_item_ptr(self), values};
  bui_with_arena(item_values_body, &iv);
  return values;
}

static VALUE tree_item_parent(VALUE self)
{
  return bui_tree_item_wrap(BK_tree_item_parent(bui_tree_item_ptr(self)));
}

static VALUE tree_item_children(VALUE self)
{
  BKTreeItem *it = bui_tree_item_ptr(self);
  const int n = BK_tree_item_child_count(it);
  VALUE out = rb_ary_new_capa(n);
  for (int i = 0; i < n; i++) {
    rb_ary_push(out, bui_tree_item_wrap(BK_tree_item_child(it, i)));
  }
  return out;
}

/* tree_roots = [items]: what a "tree" node shows. */
static VALUE node_set_tree_roots(VALUE self, VALUE roots)
{
  BKNode *n = bui_node_ptr(self);
  Check_Type(roots, T_ARRAY);
  const long count = RARRAY_LEN(roots);
  if (count > INT_MAX) {
    rb_raise(rb_eArgError, "too many roots");
  }
  VALUE buf;
  BKTreeItem **items = ALLOCV_N(BKTreeItem *, buf, count);
  for (long i = 0; i < count; i++) {
    items[i] = bui_tree_item_ptr(rb_ary_entry(roots, i));
  }
  BK_tree_set_roots(n, items, (int)count);
  ALLOCV_END(buf);
  return roots;
}

static VALUE node_tree_selected(VALUE self)
{
  return bui_tree_item_wrap(BK_tree_selected(bui_node_ptr(self)));
}

static VALUE node_tree_select(VALUE self, VALUE item)
{
  BKNode *n = bui_node_ptr(self);
  BK_tree_select(n, NIL_P(item) ? NULL : bui_tree_item_ptr(item));
  return self;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Menus
 *
 * Items are Hashes: {text:, icon:, shortcut:, checked: (true/false; absent: no
 * check box), enabled:, action: a callable, items: [submenu]}; nil or "-" is a
 * separator. Actions are refs the kit retains while it shows them.
 * \{ */

static ID id_m_text, id_m_icon, id_m_shortcut, id_m_checked, id_m_enabled, id_m_action,
    id_m_items;

typedef struct MenuBuild {
  VALUE items;
  void (*fn)(const BKMenuSpec *items, int n, void *data);
  void *data;
  uintptr_t *ids; /* the actions' ids, removed after the call when the kit kept none */
  long nids, capids;
} MenuBuild;

static BKMenuSpec *menu_specs(BuiArena *a, MenuBuild *mb, VALUE items, int depth)
{
  if (depth > 32) {
    rb_raise(rb_eArgError, "menus nest at most 32 deep");
  }
  Check_Type(items, T_ARRAY);
  const long n = RARRAY_LEN(items);
  if (n > INT_MAX) {
    rb_raise(rb_eArgError, "too many menu items");
  }
  BKMenuSpec *specs = (BKMenuSpec *)bui_arena_alloc(a, sizeof(BKMenuSpec) * (size_t)n);
  for (long i = 0; i < n; i++) {
    VALUE h = rb_ary_entry(items, i);
    BKMenuSpec *s = &specs[i];
    s->checked = -1;
    s->enabled = 1;
    if (NIL_P(h)) {
      continue; /* separator: no text */
    }
    if (RB_TYPE_P(h, T_STRING)) {
      s->text = bui_arena_str(a, h); /* "-" is a separator, other text a plain row */
      continue;
    }
    Check_Type(h, T_HASH);
    VALUE v;
    s->text = bui_arena_str(a, rb_hash_lookup(h, ID2SYM(id_m_text)));
    s->icon = NIL_P(v = rb_hash_lookup(h, ID2SYM(id_m_icon))) ? 0 : NUM2INT(v);
    s->shortcut = bui_arena_str(a, rb_hash_lookup(h, ID2SYM(id_m_shortcut)));
    if (!NIL_P(v = rb_hash_lookup(h, ID2SYM(id_m_checked)))) {
      s->checked = RTEST(v) ? 1 : 0;
    }
    if (!NIL_P(v = rb_hash_lookup(h, ID2SYM(id_m_enabled)))) {
      s->enabled = RTEST(v);
    }
    if (!NIL_P(v = rb_hash_lookup(h, ID2SYM(id_m_items)))) {
      s->sub = menu_specs(a, mb, v, depth + 1);
      s->nsub = (int)RARRAY_LEN(v);
    }
    if (!NIL_P(v = rb_hash_lookup(h, ID2SYM(id_m_action)))) {
      if (!rb_respond_to(v, rb_intern("call"))) {
        rb_raise(rb_eTypeError, "a menu action responds to call");
      }
      if (mb->nids == mb->capids) {
        mb->capids = mb->capids ? mb->capids * 2 : 8;
        REALLOC_N(mb->ids, uintptr_t, mb->capids);
      }
      const uintptr_t id = bui_ref_new(v, BUI_REF_PLAIN);
      mb->ids[mb->nids++] = id;
      s->ref = (void *)id;
    }
  }
  return specs;
}

static VALUE menu_body(BuiArena *a, void *p)
{
  MenuBuild *mb = (MenuBuild *)p;
  BKMenuSpec *specs = menu_specs(a, mb, mb->items, 0);
  mb->fn(specs, (int)RARRAY_LEN(mb->items), mb->data);
  return Qnil;
}

static VALUE menu_build(VALUE p)
{
  return bui_with_arena(menu_body, (void *)p);
}

static VALUE menu_cleanup(VALUE p)
{
  MenuBuild *mb = (MenuBuild *)p;
  for (long i = 0; i < mb->nids; i++) {
    bui_ref_remove_unused(mb->ids[i]);
  }
  xfree(mb->ids);
  return Qnil;
}

void bui_with_menu(VALUE items, void (*fn)(const BKMenuSpec *items, int n, void *data), void *data)
{
  MenuBuild mb = {items, fn, data, NULL, 0, 0};
  rb_ensure(menu_build, (VALUE)&mb, menu_cleanup, (VALUE)&mb);
}

static void set_menu(const BKMenuSpec *items, int n, void *data)
{
  BK_menubutton_set_items((BKNode *)data, items, n);
}

/* menu = [items]: the items of a "menubutton" (a "menubar" holds menubuttons). */
static VALUE node_set_menu(VALUE self, VALUE items)
{
  BKNode *n = bui_node_ptr(self);
  if (strcmp(BK_node_type(n), "menubutton") != 0) {
    rb_raise(rb_eArgError, "a %s node has no menu (a menubutton has)", BK_node_type(n));
  }
  bui_with_menu(items, set_menu, n);
  return items;
}

/* Menu popups: the texts of their visible rows, and a row's box. */
static VALUE node_menu_rows(VALUE self)
{
  BKNode *n = bui_node_ptr(self);
  const int count = BK_menu_row_count(n);
  VALUE out = rb_ary_new_capa(count);
  for (int i = 0; i < count; i++) {
    rb_ary_push(out, bui_str(BK_menu_row_text(n, i)));
  }
  return out;
}

static VALUE node_menu_row_rect(VALUE self, VALUE i)
{
  BKNode *n = bui_node_ptr(self);
  const int row = NUM2INT(i);
  if (row < 0 || row >= BK_menu_row_count(n)) {
    rb_raise(rb_eIndexError, "no row %d", row);
  }
  int r[4];
  BK_menu_row_rect(n, row, r);
  return bui_rect_new(r);
}

/** \} */

void bui_init_node(void)
{
  id_image = rb_intern("@image");
  g_families = st_init_strtable();

  /* layout managers and anchor bits (BK_GEOM_*, BK_ANCHOR_*), for lib/bui/node.rb */
  rb_define_const(bui_mBUI, "GEOM_PACK", INT2NUM(BK_GEOM_PACK));
  rb_define_const(bui_mBUI, "GEOM_GRID", INT2NUM(BK_GEOM_GRID));
  rb_define_const(bui_mBUI, "GEOM_PLACE", INT2NUM(BK_GEOM_PLACE));
  rb_define_const(bui_mBUI, "ANCHOR_N", INT2NUM(BK_ANCHOR_N));
  rb_define_const(bui_mBUI, "ANCHOR_S", INT2NUM(BK_ANCHOR_S));
  rb_define_const(bui_mBUI, "ANCHOR_W", INT2NUM(BK_ANCHOR_W));
  rb_define_const(bui_mBUI, "ANCHOR_E", INT2NUM(BK_ANCHOR_E));

  bui_cNode = rb_define_class_under(bui_mBUI, "Node", rb_cObject);
  rb_define_alloc_func(bui_cNode, node_alloc);
  rb_define_method(bui_cNode, "initialize", node_initialize, 1);
  rb_define_method(bui_cNode, "type", node_type_name, 0);
  rb_define_method(bui_cNode, "free", node_free, 0);
  rb_define_method(bui_cNode, "freed?", node_freed_p, 0);
  rb_define_method(bui_cNode, "add", node_add, -1);
  rb_define_method(bui_cNode, "<<", node_push, 1);
  rb_define_method(bui_cNode, "remove", node_remove, 1);
  rb_define_method(bui_cNode, "child_count", node_child_count, 0);
  rb_define_method(bui_cNode, "child", node_child, 1);
  rb_define_method(bui_cNode, "children", node_children, 0);
  rb_define_method(bui_cNode, "parent", node_parent, 0);
  rb_define_method(bui_cNode, "rect", node_rect, 0);
  rb_define_method(bui_cNode, "natural_size", node_natural, 0);
  rb_define_method(bui_cNode, "invalidate", node_invalidate, 0);
  rb_define_method(bui_cNode, "relayout", node_relayout, 0);

  rb_define_method(bui_cNode, "set_num", node_set_num, 2);
  rb_define_method(bui_cNode, "set_str", node_set_str, 2);
  rb_define_method(bui_cNode, "set_color", node_set_color, 2);
  rb_define_method(bui_cNode, "set_strs", node_set_strs, 2);
  rb_define_method(bui_cNode, "num", node_num, 1);
  rb_define_method(bui_cNode, "str", node_str, 1);
  rb_define_method(bui_cNode, "color", node_color, 1);
  rb_define_method(bui_cNode, "strs", node_strs, 1);

  rb_define_method(bui_cNode, "geometry", node_geometry, 3);
  rb_define_method(bui_cNode, "geometry_manager", node_geometry_manager, 0);
  rb_define_method(bui_cNode, "geometry_get", node_geometry_get, 1);
  rb_define_method(bui_cNode, "grid_weight", node_grid_weight, -1);

  rb_define_method(bui_cNode, "set_image_source", node_set_image_source, -1);
  rb_define_method(bui_cNode, "image=", node_set_image, 1);
  rb_define_method(bui_cNode, "image_frame=", node_set_image_frame, 1);

  id_k_id = rb_intern("id");
  id_k_type = rb_intern("type");
  id_k_coords = rb_intern("coords");
  id_k_fill = rb_intern("fill");
  id_k_outline = rb_intern("outline");
  id_k_width = rb_intern("width");
  id_k_radius = rb_intern("radius");
  id_k_size = rb_intern("size");
  id_k_start = rb_intern("start");
  id_k_extent = rb_intern("extent");
  id_k_text = rb_intern("text");
  id_k_anchor = rb_intern("anchor");
  id_k_src = rb_intern("src");
  id_k_fit = rb_intern("fit");
  id_k_hidden = rb_intern("hidden");
  id_k_bold = rb_intern("bold");
  id_k_italic = rb_intern("italic");
  id_k_underline = rb_intern("underline");
  id_k_family = rb_intern("family");
  id_k_rgba = rb_intern("rgba");
  id_k_image_size = rb_intern("image_size");
  id_k_frame = rb_intern("frame");
  id_k_no_hit = rb_intern("no_hit");
  rb_define_method(bui_cNode, "canvas_set", node_canvas_set, 1);
  rb_define_method(bui_cNode, "canvas_add", node_canvas_add, 1);
  rb_define_method(bui_cNode, "canvas_put", node_canvas_put, 2);
  rb_define_method(bui_cNode, "canvas_truncate", node_canvas_truncate, 1);
  rb_define_method(bui_cNode, "canvas_delete", node_canvas_delete, 1);
  rb_define_method(bui_cNode, "canvas_raise", node_canvas_raise, -1);
  rb_define_method(bui_cNode, "canvas_count", node_canvas_count, 0);

  rb_define_method(bui_cNode, "text_insert", node_text_insert, 2);
  rb_define_method(bui_cNode, "text_delete", node_text_delete, 2);
  rb_define_method(bui_cNode, "caret", node_caret, 0);
  rb_define_method(bui_cNode, "caret=", node_set_caret, 1);
  rb_define_method(bui_cNode, "selection", node_selection, 0);
  rb_define_method(bui_cNode, "select", node_select, 2);
  rb_define_method(bui_cNode, "text_see", node_text_see, 1);

  bui_cTreeItem = rb_define_class_under(bui_mBUI, "TreeItem", rb_cObject);
  rb_define_alloc_func(bui_cTreeItem, tree_item_alloc);
  rb_define_method(bui_cTreeItem, "initialize", tree_item_initialize, -1);
  rb_define_method(bui_cTreeItem, "add", tree_item_add, 1);
  rb_define_method(bui_cTreeItem, "<<", tree_item_add, 1);
  rb_define_method(bui_cTreeItem, "clear", tree_item_clear, 0);
  rb_define_method(bui_cTreeItem, "label", tree_item_label, 0);
  rb_define_method(bui_cTreeItem, "label=", tree_item_set_label, 1);
  rb_define_method(bui_cTreeItem, "[]", tree_item_get, 1);
  rb_define_method(bui_cTreeItem, "[]=", tree_item_set, 2);
  rb_define_method(bui_cTreeItem, "values=", tree_item_set_values, 1);
  rb_define_method(bui_cTreeItem, "parent", tree_item_parent, 0);
  rb_define_method(bui_cTreeItem, "children", tree_item_children, 0);
  rb_define_method(bui_cNode, "tree_roots=", node_set_tree_roots, 1);
  rb_define_method(bui_cNode, "tree_selected", node_tree_selected, 0);
  rb_define_method(bui_cNode, "tree_select", node_tree_select, 1);

  id_m_text = rb_intern("text");
  id_m_icon = rb_intern("icon");
  id_m_shortcut = rb_intern("shortcut");
  id_m_checked = rb_intern("checked");
  id_m_enabled = rb_intern("enabled");
  id_m_action = rb_intern("action");
  id_m_items = rb_intern("items");
  rb_define_method(bui_cNode, "menu=", node_set_menu, 1);
  rb_define_method(bui_cNode, "menu_rows", node_menu_rows, 0);
  rb_define_method(bui_cNode, "menu_row_rect", node_menu_row_rect, 1);
}
