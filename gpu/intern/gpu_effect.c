/* BUI GPU effects: the registry and the shader dialect.
 *
 * How Blender does it (gpu/GPU_shader.hh, gpu/opengl/gl_shader.cc): GLSL is the source
 * language; a shader is declared up front by its create-info (name, samplers, uniforms),
 * compiled and linked by the backend, and the compile/link log comes back as text
 * (GLLogParser). Backends that do not take GLSL are fed through compat macros
 * (gpu_shader_compat.hh, GPU_shader_shared_utils.hh: `float2`, `atan2`, `mix`... mapped per
 * backend) instead of a second shader source. This file does the same: ONE dialect (GLSL with
 * the small compat set below), a prelude per backend, and the interface (inputs, floats) the
 * create-info would declare generated from the two numbers the app gave.
 *
 * The dialect, for the app's source (the "GPU effects" docs page lists it as well):
 *   vec4 effect(vec2 p)            required: p is the pixel in the box (centres at .5), the result
 *                                  premultiplied RGBA
 *   vec2 box_size                  the box's size, px
 *   float U[n_floats]              the effect's floats
 *   vec4 input_color(int i, vec2 t) input texture i at texel position t: bilinear, edge-clamped,
 *                                  premultiplied
 *   vec2 input_size(int i)         its size in texels
 *   types float int bool vec2 vec3 vec4 (also float2 float3 float4); no matrices, no arrays of
 *   other than U; the GLSL built-ins mix fract mod clamp step smoothstep abs sign floor ceil min
 *   max pow exp exp2 log sqrt inversesqrt sin cos tan asin acos atan length distance dot
 *   normalize, and atan2(y, x) (as Blender's compat does for GLSL).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_effect.h"

/* -------------------------------------------------------------------- */
/** \name Registry
 * \{ */

typedef struct BuiEffect {
  int id;
  char *name, *source;
  int n_inputs, n_floats;
  char error[BUI_EFFECT_LOG];
} BuiEffect;

static BuiEffect **g_fx; /* slot -> effect (NULL: free) */
static unsigned short *g_fx_serial;
static int g_fx_slots;
static unsigned g_fx_free_gen;

static BuiEffect *fx_get(int id)
{
  const int slot = BUI_EFFECT_SLOT(id);
  if (id <= 0 || slot < 0 || slot >= g_fx_slots) {
    return NULL;
  }
  BuiEffect *e = g_fx[slot];
  return e && e->id == id ? e : NULL;
}

int BUI_effect_create(const char *name, const char *source, int n_inputs, int n_floats)
{
  if (!source || n_inputs < 0 || n_inputs > BUI_EFFECT_MAX_INPUTS || n_floats < 0 ||
      n_floats > BUI_EFFECT_MAX_FLOATS)
  {
    return 0;
  }
  const size_t len = strlen(source);
  if (len == 0 || len > BUI_EFFECT_MAX_SOURCE) {
    return 0;
  }
  int slot = -1;
  for (int i = 0; i < g_fx_slots; i++) {
    if (!g_fx[i]) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    const int n = g_fx_slots ? g_fx_slots * 2 : 16;
    if (n > 0xFFFF) {
      return 0;
    }
    BuiEffect **nf = (BuiEffect **)realloc(g_fx, sizeof(*nf) * (size_t)n);
    if (!nf) {
      return 0;
    }
    g_fx = nf;
    unsigned short *ns = (unsigned short *)realloc(g_fx_serial, sizeof(*ns) * (size_t)n);
    if (!ns) {
      return 0;
    }
    g_fx_serial = ns;
    memset(g_fx + g_fx_slots, 0, sizeof(*nf) * (size_t)(n - g_fx_slots));
    memset(g_fx_serial + g_fx_slots, 0, sizeof(*ns) * (size_t)(n - g_fx_slots));
    slot = g_fx_slots;
    g_fx_slots = n;
  }
  BuiEffect *e = (BuiEffect *)calloc(1, sizeof(BuiEffect));
  const size_t nl = name ? strlen(name) : 0;
  if (!e || !(e->name = (char *)malloc(nl + 1)) || !(e->source = (char *)malloc(len + 1))) {
    if (e) {
      free(e->name);
    }
    free(e);
    return 0;
  }
  memcpy(e->name, name ? name : "", nl + 1);
  memcpy(e->source, source, len + 1);
  e->n_inputs = n_inputs;
  e->n_floats = n_floats;
  /* the serial keeps a stale id from naming a later effect in the same slot; ids stay positive */
  g_fx_serial[slot] = (unsigned short)((g_fx_serial[slot] % 0x7FFE) + 1);
  e->id = (slot + 1) | ((int)g_fx_serial[slot] << 16);
  g_fx[slot] = e;
  return e->id;
}

void BUI_effect_free(int id)
{
  BuiEffect *e = fx_get(id);
  if (!e) {
    return;
  }
  g_fx[BUI_EFFECT_SLOT(id)] = NULL;
  free(e->name);
  free(e->source);
  free(e);
  g_fx_free_gen++;
}

const char *BUI_effect_error(int id)
{
  const BuiEffect *e = fx_get(id);
  return e ? e->error : NULL;
}

int bui_effect_view(int id, BuiEffectView *r_view)
{
  const BuiEffect *e = fx_get(id);
  if (!e) {
    return 0;
  }
  r_view->id = e->id;
  r_view->name = e->name;
  r_view->source = e->source;
  r_view->n_inputs = e->n_inputs;
  r_view->n_floats = e->n_floats;
  return 1;
}

void bui_effect_set_error(int id, const char *log)
{
  BuiEffect *e = fx_get(id);
  if (e) {
    snprintf(e->error, sizeof(e->error), "%s", log ? log : "");
    size_t n = strlen(e->error); /* the compilers end their text with a newline */
    while (n > 0 && (e->error[n - 1] == '\n' || e->error[n - 1] == ' ')) {
      e->error[--n] = 0;
    }
  }
}

unsigned bui_effect_free_gen(void)
{
  return g_fx_free_gen;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Source assembly (one prelude per backend)
 * \{ */

typedef struct StrBuf {
  char *p;
  size_t len, cap;
  int failed;
} StrBuf;

static void sb_range(StrBuf *s, const char *text, size_t n)
{
  if (s->failed) {
    return;
  }
  if (s->len + n + 1 > s->cap) {
    size_t cap = s->cap ? s->cap : 4096;
    while (cap < s->len + n + 1) {
      cap *= 2;
    }
    char *np = (char *)realloc(s->p, cap);
    if (!np) {
      s->failed = 1;
      return;
    }
    s->p = np;
    s->cap = cap;
  }
  memcpy(s->p + s->len, text, n);
  s->len += n;
  s->p[s->len] = 0;
}

static void sb_add(StrBuf *s, const char *text)
{
  sb_range(s, text, strlen(text));
}

static void sb_int(StrBuf *s, int v)
{
  char tmp[16];
  snprintf(tmp, sizeof(tmp), "%d", v);
  sb_add(s, tmp);
}

/* `NAME k` repeated for k in 0 .. n: `head` `k` `mid` `k` `tail` (the per-input declarations and
 * branches of input_color, which GLSL 3.3 cannot index dynamically). */
static void sb_each(StrBuf *s, int n, const char *head, const char *mid, const char *tail)
{
  for (int k = 0; k < n; k++) {
    sb_add(s, head);
    sb_int(s, k);
    sb_add(s, mid);
    sb_int(s, k);
    sb_add(s, tail);
  }
}

/* Direct3D 11 (shader model 4): the dialect's names are macros, as Blender's compat headers do. The
 * floats go in a constant buffer (one register each: HLSL pads array elements), the inputs are
 * t8.. so the renderer's own t0..t5 stay bound. `effect` becomes bui_effect_main (a word FX files
 * reserve). */
static void hlsl_prelude(StrBuf *s, const BuiEffectView *e)
{
  sb_add(s,
         "#define vec2 float2\n#define vec3 float3\n#define vec4 float4\n"
         "#define mix lerp\n#define fract frac\n#define inversesqrt rsqrt\n"
         "#define mod(x, y) ((x) - (y) * floor((x) / (y)))\n"
         "#define effect bui_effect_main\n"
         "cbuffer BuiFx : register(b1) {\n  float4 bui_box;\n  float4 bui_ndc;\n  float U[");
  sb_int(s, e->n_floats > 0 ? e->n_floats : 1);
  sb_add(s,
         "];\n};\n#define box_size (bui_box.zw)\nSamplerState bui_smp : register(s0);\n");
  for (int k = 0; k < e->n_inputs; k++) {
    sb_add(s, "Texture2D bui_t");
    sb_int(s, k);
    sb_add(s, " : register(t");
    sb_int(s, 8 + k);
    sb_add(s, ");\n");
  }
  sb_add(s,
         "float2 bui_dim(Texture2D t) { uint w, h; t.GetDimensions(w, h); return float2(w, h); }\n"
         "float4 bui_fetch(Texture2D t, float2 q) {\n"
         "  float4 c = t.SampleLevel(bui_smp, q / bui_dim(t), 0);\n"
         "  c.rgb *= c.a;\n  return c;\n}\n"
         "float4 input_color(int i, float2 q) {\n");
  sb_each(s, e->n_inputs, "  if (i == ", ") return bui_fetch(bui_t", ", q);\n");
  sb_add(s, "  return float4(0.0, 0.0, 0.0, 0.0);\n}\nfloat2 input_size(int i) {\n");
  sb_each(s, e->n_inputs, "  if (i == ", ") return bui_dim(bui_t", ");\n");
  sb_add(s,
         "  return float2(1.0, 1.0);\n}\n"
         "float4 bui_effect_main(float2 p);\n"
         "float4 bui_ps(float4 pos : SV_Position) : SV_Target {\n"
         "  return bui_effect_main(pos.xy - bui_box.xy);\n}\n"
         "#line 1 \"effect\"\n");
}

/* HLSL has no one-scalar constructors (`vec3(0.5)`): the one GLSL spelling macros cannot map, so
 * the source is rewritten where Blender's shader_tool rewrites GLSL for other backends. Those
 * become casts, `(float3)(0.5)`, which splat. Everything else is copied as it is (so are line
 * numbers, comments and any constructor with a comma). */
static int is_word(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* the vec / float N type at src[i..j), as HLSL spells it ("" when it is no such word) */
static const char *vec_type(const char *src, size_t i, size_t j)
{
  static const char *const names[] = {"vec2", "vec3", "vec4", "float2", "float3", "float4"};
  static const char *const hlsl[] = {"float2", "float3", "float4", "float2", "float3", "float4"};
  for (int k = 0; k < 6; k++) {
    if (j - i == strlen(names[k]) && strncmp(src + i, names[k], j - i) == 0) {
      return hlsl[k];
    }
  }
  return "";
}

/* The matching ')' of the '(' at src[open] and whether the argument list has a top-level comma;
 * `n` when unbalanced. */
static size_t call_end(const char *src, size_t n, size_t open, int *r_comma)
{
  int depth = 0;
  *r_comma = 0;
  for (size_t i = open; i < n; i++) {
    const char c = src[i];
    if (c == '(' || c == '[') {
      depth++;
    }
    else if (c == ')' || c == ']') {
      if (--depth == 0) {
        return i;
      }
    }
    else if (c == ',' && depth == 1) {
      *r_comma = 1;
    }
  }
  return n;
}

static void hlsl_rewrite(StrBuf *s, const char *src, size_t n)
{
  size_t i = 0;
  while (i < n && !s->failed) {
    if (src[i] == '/' && i + 1 < n && (src[i + 1] == '/' || src[i + 1] == '*')) {
      const int line = src[i + 1] == '/';
      size_t j = i + 2;
      while (j < n && (line ? src[j] != '\n' : !(src[j] == '*' && j + 1 < n && src[j + 1] == '/'))) {
        j++;
      }
      j = line || j >= n ? j : j + 2;
      sb_range(s, src + i, j - i);
      i = j;
      continue;
    }
    if (!is_word(src[i]) || (i > 0 && is_word(src[i - 1]))) {
      sb_range(s, src + i, 1);
      i++;
      continue;
    }
    size_t j = i;
    while (j < n && is_word(src[j])) {
      j++;
    }
    const char *type = vec_type(src, i, j);
    size_t open = j;
    while (type[0] && open < n && (src[open] == ' ' || src[open] == '\t')) {
      open++;
    }
    int comma = 0;
    const size_t close = type[0] && open < n && src[open] == '(' ? call_end(src, n, open, &comma) : n;
    if (close < n && !comma && close > open + 1) {
      sb_add(s, "((");
      sb_add(s, type);
      sb_add(s, ")(");
      hlsl_rewrite(s, src + open + 1, close - open - 1);
      sb_add(s, "))");
      i = close + 1;
    }
    else {
      sb_range(s, src + i, j - i);
      i = j;
    }
  }
}

/* OpenGL 3.3: the dialect is GLSL; only the HLSL-flavoured names Blender's shared headers also
 * offer (`float2`, `atan2`) are macros. The frame is stored top row first, so gl_FragCoord is the
 * pixel position in the frame as it is. */
static void glsl_prelude(StrBuf *s, const BuiEffectView *e)
{
  sb_add(s,
         "#version 330 core\n"
         "#define float2 vec2\n#define float3 vec3\n#define float4 vec4\n#define atan2 atan\n"
         "uniform vec4 bui_box;\nuniform float U[");
  sb_int(s, e->n_floats > 0 ? e->n_floats : 1);
  sb_add(s, "];\n#define box_size (bui_box.zw)\n");
  for (int k = 0; k < e->n_inputs; k++) {
    sb_add(s, "uniform sampler2D bui_t");
    sb_int(s, k);
    sb_add(s, ";\n");
  }
  sb_add(s,
         "vec4 bui_fetch(sampler2D t, vec2 q) {\n"
         "  vec4 c = textureLod(t, q / vec2(textureSize(t, 0)), 0.0);\n"
         "  c.rgb *= c.a;\n  return c;\n}\n"
         "vec4 input_color(int i, vec2 q) {\n");
  sb_each(s, e->n_inputs, "  if (i == ", ") return bui_fetch(bui_t", ", q);\n");
  sb_add(s, "  return vec4(0.0);\n}\nvec2 input_size(int i) {\n");
  sb_each(s, e->n_inputs, "  if (i == ", ") return vec2(textureSize(bui_t", ", 0));\n");
  sb_add(s,
         "  return vec2(1.0);\n}\n"
         "vec4 effect(vec2 p);\nout vec4 bui_frag;\n"
         "void main() { bui_frag = effect(gl_FragCoord.xy - bui_box.xy); }\n"
         "#line 1\n");
}

char *bui_effect_assemble(BuiFxDialect dialect, const BuiEffectView *e)
{
  StrBuf s = {0};
  if (dialect == BUI_FX_HLSL) {
    hlsl_prelude(&s, e);
    hlsl_rewrite(&s, e->source, strlen(e->source));
  }
  else {
    glsl_prelude(&s, e);
    sb_add(&s, e->source);
  }
  sb_add(&s, "\n");
  if (s.failed) {
    free(s.p);
    return NULL;
  }
  return s.p;
}

const char bui_fx_hlsl_vs[] =
    "cbuffer BuiFx : register(b1) {\n  float4 bui_box;\n  float4 bui_ndc;\n};\n"
    "float4 bui_vs(uint vid : SV_VertexID) : SV_Position {\n"
    "  const float2 t = float2(vid & 1, vid >> 1);\n"
    "  const float2 xy = bui_box.xy + t * bui_box.zw;\n"
    "  return float4(xy.x * bui_ndc.x - 1.0, 1.0 - xy.y * bui_ndc.y, 0.0, 1.0);\n}\n";

const char bui_fx_glsl_vs[] =
    "#version 330 core\n"
    "uniform vec4 bui_box;\nuniform vec2 bui_ndc;\n"
    "void main() {\n"
    "  vec2 t = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
    "  vec2 xy = bui_box.xy + t * bui_box.zw;\n"
    "  gl_Position = vec4(xy.x * bui_ndc.x - 1.0, xy.y * bui_ndc.y - 1.0, 0.0, 1.0);\n"
    "}\n";

/** \} */
