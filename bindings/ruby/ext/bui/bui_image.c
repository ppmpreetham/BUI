/* BUI for Ruby: BUI::Image, a decoded image (PNG, JPEG, WebP, GIF, BMP, TGA,
 * QOI, HDR, PNM, SVG) with its frames, premultiplied 0xAARRGGBB.
 *
 * The GC frees an image; a node showing one keeps it alive (Node#image=),
 * so there is no free to call while a node still draws it.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "bui_ext.h"

VALUE bui_cImage;

static size_t image_bytes(const BKImBuf *ib)
{
  int w = 0, h = 0;
  const int frames = ib ? BK_imbuf_size(ib, &w, &h) : 0;
  return (size_t)w * (size_t)h * 4 * (size_t)(frames > 0 ? frames : 1);
}

static void image_free(void *p)
{
  BK_imbuf_free((BKImBuf *)p);
}

static size_t image_size(const void *p)
{
  return image_bytes((const BKImBuf *)p);
}

static const rb_data_type_t image_type = {
    .wrap_struct_name = "BUI::Image",
    .function = {.dfree = image_free, .dsize = image_size},
    .flags = RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED,
};

const BKImBuf *bui_image_ptr(VALUE v)
{
  return (const BKImBuf *)rb_check_typeddata(v, &image_type);
}

static VALUE image_wrap(VALUE klass, BKImBuf *ib)
{
  if (!ib) {
    rb_raise(bui_eError, "not an image BUI reads");
  }
  /* the pixels are outside Ruby's heap: count them, so the GC runs as images pile up
   * (the count starts again at each GC, so nothing is taken back when one is freed) */
  const VALUE self = TypedData_Wrap_Struct(klass, &image_type, ib);
  rb_gc_adjust_memory_usage((ssize_t)image_bytes(ib));
  return self;
}

static void max_size(VALUE max_w, VALUE max_h, int *r_w, int *r_h)
{
  *r_w = NIL_P(max_w) ? 0 : NUM2INT(max_w);
  *r_h = NIL_P(max_h) ? 0 : NUM2INT(max_h);
  if (*r_w < 0 || *r_h < 0) {
    rb_raise(rb_eArgError, "a size limit of %d x %d (0: none)", *r_w, *r_h);
  }
}

/* BUI::Image.decode(bytes, max_w = 0, max_h = 0): fitted within max_w x max_h
 * (never enlarged; 0 is no limit). Raises BUI::Error for data BUI cannot read. */
static VALUE image_s_decode(int argc, VALUE *argv, VALUE klass)
{
  VALUE data, max_w, max_h;
  rb_scan_args(argc, argv, "12", &data, &max_w, &max_h);
  int w, h;
  max_size(max_w, max_h, &w, &h);
  StringValue(data);
  BKImBuf *ib = BK_imbuf_decode(RSTRING_PTR(data), (size_t)RSTRING_LEN(data), w, h);
  RB_GC_GUARD(data);
  return image_wrap(klass, ib);
}

/* BUI::Image.load(path, max_w = 0, max_h = 0) */
static VALUE image_s_load(int argc, VALUE *argv, VALUE klass)
{
  VALUE path, max_w, max_h;
  rb_scan_args(argc, argv, "12", &path, &max_w, &max_h);
  int w, h;
  max_size(max_w, max_h, &w, &h);
  FilePathValue(path);
  return image_wrap(klass, BK_imbuf_load(bui_cstr(path), w, h));
}

/* BUI::Image.from_rgba(bytes, width, height, stride = width * 4): straight
 * RGBA pixels, copied. */
static VALUE image_s_from_rgba(int argc, VALUE *argv, VALUE klass)
{
  VALUE data, width, height, stride;
  rb_scan_args(argc, argv, "31", &data, &width, &height, &stride);
  StringValue(data);
  const long w = NUM2INT(width), h = NUM2INT(height);
  if (w <= 0 || h <= 0 || w > INT_MAX / 4) {
    rb_raise(rb_eArgError, "an image of %ld x %ld", w, h);
  }
  const long s = NIL_P(stride) ? w * 4 : NUM2INT(stride);
  const long len = RSTRING_LEN(data);
  /* every row holds its pixels and the last ends inside the data (divided: no overflow) */
  if (s < w * 4 || len < w * 4 || (len - w * 4) / s < h - 1) {
    rb_raise(rb_eArgError, "%ld bytes hold no %ld x %ld image with rows of %ld bytes", len, w, h, s);
  }
  BKImBuf *ib = BK_imbuf_from_rgba(
      (const unsigned char *)RSTRING_PTR(data), (int)w, (int)h, (int)s);
  RB_GC_GUARD(data);
  return image_wrap(klass, ib);
}

static VALUE image_width(VALUE self)
{
  int w, h;
  BK_imbuf_size(bui_image_ptr(self), &w, &h);
  return INT2NUM(w);
}

static VALUE image_height(VALUE self)
{
  int w, h;
  BK_imbuf_size(bui_image_ptr(self), &w, &h);
  return INT2NUM(h);
}

static VALUE image_frames(VALUE self)
{
  int w, h;
  return INT2NUM(BK_imbuf_size(bui_image_ptr(self), &w, &h));
}

/* The format it was decoded from: "png", "jpeg", "gif"..., nil when made from pixels. */
static VALUE image_format(VALUE self)
{
  return bui_str(BK_image_format_name(BK_imbuf_format(bui_image_ptr(self))));
}

/* The delay of a frame in ms (0 for a still). */
static VALUE image_delay(VALUE self, VALUE frame)
{
  const BKImBuf *ib = bui_image_ptr(self);
  int w, h;
  const int f = NUM2INT(frame), n = BK_imbuf_size(ib, &w, &h);
  if (f < 0 || f >= n) {
    rb_raise(rb_eIndexError, "no frame %d of %d", f, n);
  }
  return INT2NUM(BK_imbuf_delay(ib, f));
}

/* rgba(frame = 0): a frame as straight RGBA bytes (a binary String). */
static VALUE image_rgba(int argc, VALUE *argv, VALUE self)
{
  VALUE frame;
  rb_scan_args(argc, argv, "01", &frame);
  const BKImBuf *ib = bui_image_ptr(self);
  int w, h;
  const int n = BK_imbuf_size(ib, &w, &h), f = NIL_P(frame) ? 0 : NUM2INT(frame);
  if (f < 0 || f >= n) {
    rb_raise(rb_eIndexError, "no frame %d of %d", f, n);
  }
  VALUE out = rb_str_new(NULL, (long)w * h * 4);
  BK_imbuf_rgba(ib, f, (unsigned char *)RSTRING_PTR(out));
  return out;
}

void bui_init_image(void)
{
  bui_cImage = rb_define_class_under(bui_mBUI, "Image", rb_cObject);
  rb_undef_alloc_func(bui_cImage);
  rb_define_singleton_method(bui_cImage, "decode", image_s_decode, -1);
  rb_define_singleton_method(bui_cImage, "load", image_s_load, -1);
  rb_define_singleton_method(bui_cImage, "from_rgba", image_s_from_rgba, -1);
  rb_define_method(bui_cImage, "width", image_width, 0);
  rb_define_method(bui_cImage, "height", image_height, 0);
  rb_define_method(bui_cImage, "frames", image_frames, 0);
  rb_define_method(bui_cImage, "format", image_format, 0);
  rb_define_method(bui_cImage, "delay", image_delay, 1);
  rb_define_method(bui_cImage, "rgba", image_rgba, -1);
}
