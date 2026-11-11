/* Custom themes: a theme file (JSON) or single colors set at run time, over Blender's Dark and Light
 * presets (kit_theme_presets.c).
 *
 * A theme names its colors in plain English ("button.background") or with Blender's own names
 * ("user_interface.wcol_regular.inner"), both from the key table that tools/gen_theme_keys.py
 * writes from Blender's theme XML. Short palette names ("primary.background") fill in several keys
 * at once; the keys a theme sets itself always win over them. What BKTheme has no field for is
 * ignored, so a file may carry Blender keys BUI has no use for yet.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kit_intern.h"
#include "kit_json.h"

/* -------------------------------------------------------------------- */
/** \name The key table
 * \{ */

typedef enum {
  TK_K_COLOR,  /* uchar[4] */
  TK_K_FLOAT,  /* float */
  TK_K_INT,    /* int */
  TK_K_SHORT,  /* short */
  TK_K_FLAG,   /* uchar 0/1 */
  TK_K_BORDER, /* BKWCol.border_extra from a width in pixels */
  TK_K_EMBOSS, /* BKWCol.no_emboss from "emboss.enabled" */
  TK_K_CORNER, /* one CNR_* bit of BKWCol.square_corners, from "rounded" */
} TKKind;

typedef struct TKey {
  const char *name;    /* plain English */
  const char *blender; /* Blender's key, or "" */
  TKKind kind;
  size_t off;
  unsigned flag;
} TKey;

#define TK_ENTRY(n, b, kind, f, fl) {n, b, kind, offsetof(BKTheme, f), fl},
#define TK_COLOR(n, b, f) TK_ENTRY(n, b, TK_K_COLOR, f, 0)
#define TK_FLOAT(n, b, f) TK_ENTRY(n, b, TK_K_FLOAT, f, 0)
#define TK_INT(n, b, f) TK_ENTRY(n, b, TK_K_INT, f, 0)
#define TK_SHORT(n, b, f) TK_ENTRY(n, b, TK_K_SHORT, f, 0)
#define TK_FLAG(n, b, f) TK_ENTRY(n, b, TK_K_FLAG, f, 0)
#define TK_BORDER_WIDTH(n, b, f) TK_ENTRY(n, b, TK_K_BORDER, f, 0)
#define TK_EMBOSS(n, b, f) TK_ENTRY(n, b, TK_K_EMBOSS, f, 0)
#define TK_CORNER(n, b, f, bit) TK_ENTRY(n, b, TK_K_CORNER, f, bit)

static const TKey k_keys[] = {
#include "kit_theme_keys.inc"
};

#include "kit_theme_aliases.inc"

/* The set of widget colors a roundness applies to: every widget set has "<name>.roundness". */
#define N_KEYS (sizeof(k_keys) / sizeof(k_keys[0]))

static const TKey *find_key(const char *key)
{
  if (!key || !key[0]) {
    return NULL;
  }
  for (size_t i = 0; i < N_KEYS; i++) {
    if (!strcmp(key, k_keys[i].name) || (k_keys[i].blender[0] && !strcmp(key, k_keys[i].blender))) {
      return &k_keys[i];
    }
  }
  return NULL;
}

static const char *const *find_alias(const char *key)
{
  for (size_t i = 0; i < sizeof(k_aliases) / sizeof(k_aliases[0]); i++) {
    if (!strcmp(key, k_aliases[i].name)) {
      return k_aliases[i].targets;
    }
  }
  return NULL;
}

/* "#rgb", "#rgba", "#rrggbb" or "#rrggbbaa". */
static bool parse_color(const char *s, uchar out[4])
{
  if (!s || s[0] != '#') {
    return false;
  }
  const size_t n = strlen(s + 1);
  if (n != 3 && n != 4 && n != 6 && n != 8) {
    return false;
  }
  unsigned v[8];
  for (size_t i = 0; i < n; i++) {
    if (!isxdigit((unsigned char)s[1 + i])) {
      return false;
    }
    const char c = s[1 + i];
    v[i] = (unsigned)(isdigit((unsigned char)c) ? c - '0' : tolower((unsigned char)c) - 'a' + 10);
  }
  if (n <= 4) {
    for (size_t i = 0; i < n; i++) {
      out[i] = (uchar)(v[i] * 17);
    }
    out[3] = n == 4 ? out[3] : 255;
  }
  else {
    for (size_t i = 0; i < n / 2; i++) {
      out[i] = (uchar)(v[2 * i] * 16 + v[2 * i + 1]);
    }
    out[3] = n == 8 ? out[3] : 255;
  }
  return true;
}

static bool key_set_color(const TKey *k, const uchar c[4])
{
  if (k->kind != TK_K_COLOR) {
    return false;
  }
  memcpy((uchar *)&bk_theme + k->off, c, 4);
  return true;
}

/** Floats Blender keeps in 0..1 (the roundness of a widget, the strength of the menu shadow). */
static bool key_is_fraction(const TKey *k)
{
  const size_t n = strlen(k->name);
  return strcmp(k->name, "menu.shadow.strength") == 0 ||
         (n >= 10 && strcmp(k->name + n - 10, ".roundness") == 0);
}

/** The widest a menu's shadow can be, in pixels: it grows the popup's redraw rectangle. */
#define TK_MAX_SHADOW_SIZE 200.0

static bool key_set_num(const TKey *k, double v)
{
  uchar *base = (uchar *)&bk_theme + k->off;
  if (!isfinite(v)) { /* a number from JSON can overflow to infinity */
    return false;
  }
  switch (k->kind) {
    case TK_K_FLOAT:
      *(float *)base = key_is_fraction(k) ? (float)BK_CLAMP(v, 0.0, 1.0) : (float)BK_CLAMP(v, -1e9, 1e9);
      return true;
    case TK_K_INT:
      if (strcmp(k->name, "menu.shadow.size") == 0) {
        v = BK_CLAMP(v, 0.0, TK_MAX_SHADOW_SIZE);
      }
      *(int *)base = (int)lround(BK_CLAMP(v, -1e9, 1e9)); /* bounded first: lround of a huge value is undefined */
      return true;
    case TK_K_SHORT:
      *(short *)base = (short)lround(BK_CLAMP(v, -32768.0, 32767.0));
      return true;
    case TK_K_FLAG:
      *base = v != 0.0;
      return true;
    case TK_K_BORDER:
      *(float *)base = (float)(BK_CLAMP(v, 0.0, 16.0) - 1.0);
      return true;
    case TK_K_EMBOSS:
      *base = v == 0.0;
      return true;
    case TK_K_CORNER:
      if (v != 0.0) {
        *base &= (uchar)~k->flag;
      }
      else {
        *base |= (uchar)k->flag;
      }
      return true;
    case TK_K_COLOR:
      break;
  }
  return false;
}

static bool key_get_num(const TKey *k, double *out)
{
  const uchar *base = (const uchar *)&bk_theme + k->off;
  switch (k->kind) {
    case TK_K_FLOAT: *out = *(const float *)base; return true;
    case TK_K_INT: *out = *(const int *)base; return true;
    case TK_K_SHORT: *out = *(const short *)base; return true;
    case TK_K_FLAG: *out = *base != 0; return true;
    case TK_K_BORDER: *out = *(const float *)base + 1.0; return true;
    case TK_K_EMBOSS: *out = *base == 0; return true;
    case TK_K_CORNER: *out = (*base & k->flag) == 0; return true;
    case TK_K_COLOR: break;
  }
  return false;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Reading and setting single keys
 * \{ */

int bk_theme_key_color(const char *key, float r_rgba[4])
{
  const TKey *k = find_key(key);
  if (!k || k->kind != TK_K_COLOR) {
    return 0;
  }
  if (r_rgba) {
    bk_rgba((const uchar *)&bk_theme + k->off, r_rgba);
  }
  return 1;
}

int BK_theme_num(const char *key, double *r_value)
{
  bk_theme_env_init();
  const TKey *k = find_key(key);
  double v;
  if (!k || !key_get_num(k, &v)) {
    return 0;
  }
  if (r_value) {
    *r_value = v;
  }
  return 1;
}

static uchar to_byte(float v)
{
  return (uchar)(BK_CLAMP(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

int BK_theme_set_color(const char *key, const float rgba[4])
{
  bk_theme_mark_set();
  const TKey *k = find_key(key);
  if (!k || !rgba) {
    return 0;
  }
  const uchar c[4] = {to_byte(rgba[0]), to_byte(rgba[1]), to_byte(rgba[2]), to_byte(rgba[3])};
  if (!key_set_color(k, c)) {
    return 0;
  }
  bk_theme_changed();
  return 1;
}

int BK_theme_set_num(const char *key, double value)
{
  bk_theme_mark_set();
  const TKey *k = find_key(key);
  if (!k || !isfinite(value) || !key_set_num(k, value)) {
    return 0;
  }
  bk_theme_changed();
  return 1;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Theme files
 * \{ */

#define MAX_THEMES 64
#define MAX_ENTRIES 4096

typedef struct Entry {
  const TKey *key;       /* NULL for an alias */
  const char *const *alias_targets;
  char *name;            /* the key as written (aliases) */
  bool is_color;
  uchar color[4];
  double num;
} Entry;

typedef struct ThemeDef {
  char *name;
  int light;
  char *font_family; /* NULL: not set */
  float font_size;   /* 0: not set */
  int shadow;        /* -1: not set */
  float radius;      /* < 0: not set */
  Entry *entries;
  int n;
} ThemeDef;

static ThemeDef g_defs[MAX_THEMES];
static int g_ndefs = 0;
static char g_error[256] = "";
static int g_ignored = 0;
static char g_active[64] = ""; /* the registered theme in use; "" for a built-in one */
/** Which text properties the active theme set (and so which to undo when it is left). */
static struct {
  bool font, size, shadow;
} g_applied;

const char *BK_theme_error(void)
{
  return g_error;
}

int BK_theme_ignored(void)
{
  return g_ignored;
}

int BK_theme_count(void)
{
  return g_ndefs;
}

const char *BK_theme_at(int i)
{
  return i >= 0 && i < g_ndefs ? g_defs[i].name : NULL;
}

static void def_free(ThemeDef *d)
{
  for (int i = 0; i < d->n; i++) {
    free(d->entries[i].name);
  }
  free(d->entries);
  free(d->name);
  free(d->font_family);
  memset(d, 0, sizeof(*d));
}

static char *dup(const char *s)
{
  const size_t n = strlen(s) + 1;
  char *r = (char *)malloc(n);
  if (r) {
    memcpy(r, s, n);
  }
  return r;
}

static bool names_equal(const char *a, const char *b)
{
  for (; *a && *b; a++, b++) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
      return false;
    }
  }
  return *a == *b;
}

static void set_error(const char *fmt, const char *a, const char *b)
{
  snprintf(g_error, sizeof(g_error), fmt, a ? a : "", b ? b : "");
}

/* One member of "colors" into a theme definition. */
static bool add_entry(ThemeDef *d, const BKJson *m)
{
  if (!m->key || m->type == BK_JSON_NULL) {
    return true;
  }
  const TKey *k = find_key(m->key);
  const char *const *alias = k ? NULL : find_alias(m->key);
  if (!k && !alias) {
    g_ignored++;
    return true;
  }
  Entry e;
  memset(&e, 0, sizeof(e));
  e.key = k;
  e.alias_targets = alias;
  if (m->type == BK_JSON_STRING) {
    if (!parse_color(m->string, e.color) || (k && k->kind != TK_K_COLOR)) {
      g_ignored++;
      return true;
    }
    e.is_color = true;
  }
  else if (m->type == BK_JSON_NUMBER || m->type == BK_JSON_BOOL) {
    if (alias || k->kind == TK_K_COLOR) {
      g_ignored++;
      return true;
    }
    e.num = m->type == BK_JSON_BOOL ? (m->boolean ? 1.0 : 0.0) : m->number;
  }
  else {
    g_ignored++;
    return true;
  }
  if (d->n >= MAX_ENTRIES) {
    set_error("theme \"%s\" has too many keys", d->name, NULL);
    return false;
  }
  Entry *grown = (Entry *)realloc(d->entries, sizeof(Entry) * (size_t)(d->n + 1));
  if (!grown) {
    set_error("out of memory", NULL, NULL);
    return false;
  }
  d->entries = grown;
  e.name = dup(m->key);
  d->entries[d->n++] = e;
  return true;
}

static bool def_from_json(ThemeDef *d, const BKJson *t, int index)
{
  char where[48];
  snprintf(where, sizeof(where), "themes[%d]", index);
  const BKJson *name = bk_json_get(t, "name");
  if (t->type != BK_JSON_OBJECT || !name || name->type != BK_JSON_STRING || !name->string[0]) {
    set_error("%s: a theme needs a \"name\"", where, NULL);
    return false;
  }
  memset(d, 0, sizeof(*d));
  d->name = dup(name->string);
  d->shadow = -1;
  d->radius = -1.0f;
  const BKJson *mode = bk_json_get(t, "mode");
  if (mode && mode->type == BK_JSON_STRING) {
    if (names_equal(mode->string, "light")) {
      d->light = 1;
    }
    else if (!names_equal(mode->string, "dark")) {
      set_error("theme \"%s\": mode must be \"light\" or \"dark\"", name->string, NULL);
      def_free(d);
      return false;
    }
  }
  const BKJson *fam = bk_json_get(t, "font.family");
  if (fam && fam->type == BK_JSON_STRING) {
    d->font_family = dup(fam->string);
  }
  const BKJson *size = bk_json_get(t, "font.size");
  if (size && size->type == BK_JSON_NUMBER && size->number > 0.0) {
    d->font_size = (float)size->number;
  }
  const BKJson *shadow = bk_json_get(t, "shadow");
  if (shadow && shadow->type == BK_JSON_BOOL) {
    d->shadow = shadow->boolean;
  }
  const BKJson *radius = bk_json_get(t, "radius");
  if (radius && radius->type == BK_JSON_NUMBER) {
    d->radius = (float)BK_CLAMP(radius->number, 0.0, 1.0);
  }
  const BKJson *colors = bk_json_get(t, "colors");
  if (colors && colors->type == BK_JSON_OBJECT) {
    for (const BKJson *m = colors->first; m; m = m->next) {
      if (!add_entry(d, m)) {
        def_free(d);
        return false;
      }
    }
  }
  return true;
}

int BK_theme_load_json(const char *json)
{
  g_error[0] = '\0';
  g_ignored = 0;
  if (!json) {
    set_error("no theme text", NULL, NULL);
    return -1;
  }
  char err[160];
  BKJson *root = bk_json_parse(json, strlen(json), err, sizeof(err));
  if (!root) {
    set_error("theme file: %s", err, NULL);
    return -1;
  }
  const BKJson *themes = bk_json_get(root, "themes");
  if (root->type != BK_JSON_OBJECT || !themes || themes->type != BK_JSON_ARRAY) {
    bk_json_free(root);
    set_error("theme file: expected an object with a \"themes\" array", NULL, NULL);
    return -1;
  }
  /* parse them all first: a file with one bad theme adds none of them */
  ThemeDef parsed[MAX_THEMES];
  int np = 0, index = 0;
  for (const BKJson *t = themes->first; t; t = t->next, index++) {
    if (np >= MAX_THEMES || !def_from_json(&parsed[np], t, index)) {
      while (np > 0) {
        def_free(&parsed[--np]);
      }
      if (!g_error[0]) {
        set_error("theme file: too many themes", NULL, NULL);
      }
      bk_json_free(root);
      return -1;
    }
    np++;
  }
  bk_json_free(root);
  int added = 0;
  for (int i = 0; i < np; i++) {
    int slot = -1;
    for (int j = 0; j < g_ndefs; j++) {
      if (names_equal(g_defs[j].name, parsed[i].name)) {
        slot = j; /* the same name replaces the older theme */
        break;
      }
    }
    if (slot < 0 && g_ndefs < MAX_THEMES) {
      slot = g_ndefs++;
    }
    if (slot < 0) {
      def_free(&parsed[i]);
      continue;
    }
    def_free(&g_defs[slot]);
    g_defs[slot] = parsed[i];
    added++;
    if (g_active[0] && names_equal(g_active, g_defs[slot].name)) {
      BK_set_theme(g_active); /* the theme in use changed: show the new one */
    }
  }
  return added;
}

int BK_theme_load_file(const char *path)
{
  g_error[0] = '\0';
  FILE *f = path ? fopen(path, "rb") : NULL;
  if (!f) {
    set_error("cannot open \"%s\"", path, NULL);
    return -1;
  }
  fseek(f, 0, SEEK_END);
  const long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size < 0 || size > 16 * 1024 * 1024) {
    fclose(f);
    set_error("\"%s\" is not a theme file (too large)", path, NULL);
    return -1;
  }
  char *text = (char *)malloc((size_t)size + 1);
  if (!text) {
    fclose(f);
    set_error("out of memory", NULL, NULL);
    return -1;
  }
  const size_t got = fread(text, 1, (size_t)size, f);
  fclose(f);
  text[got] = '\0';
  const int n = BK_theme_load_json(text);
  free(text);
  return n;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Applying a theme
 * \{ */

static void apply_entry(const Entry *e)
{
  if (e->key) {
    if (e->is_color) {
      key_set_color(e->key, e->color);
    }
    else {
      key_set_num(e->key, e->num);
    }
    return;
  }
  if (!e->is_color) {
    return;
  }
  for (const char *const *t = e->alias_targets; *t; t++) {
    const TKey *k = find_key(*t);
    if (k) {
      key_set_color(k, e->color);
    }
  }
}

/** What the previous theme gave the text goes back, property by property; what an application set
 * itself (a font it chose, a size, the shadow) stays. */
static void reset_applied_text(void)
{
  if (g_applied.font) {
    BK_set_font("");
  }
  if (g_applied.size) {
    BK_set_font_size(11.0f);
  }
  if (g_applied.shadow) {
    BK_set_text_shadow(1);
  }
  memset(&g_applied, 0, sizeof(g_applied));
}

static void apply_def(const ThemeDef *d)
{
  bk_theme_load_preset(d->light);
  reset_applied_text();
  g_applied.font = d->font_family != NULL;
  g_applied.size = d->font_size > 0.0f;
  g_applied.shadow = d->shadow >= 0;
  if (d->font_family) {
    BK_set_font(d->font_family);
  }
  if (d->font_size > 0.0f) {
    BK_set_font_size(d->font_size);
  }
  if (d->shadow >= 0) {
    BK_set_text_shadow(d->shadow);
  }
  if (d->radius >= 0.0f) {
    for (size_t i = 0; i < N_KEYS; i++) {
      if (k_keys[i].kind == TK_K_FLOAT && strstr(k_keys[i].name, ".roundness") &&
          strncmp(k_keys[i].name, "panel.", 6) != 0) { /* widgets; the panel has its own */
        *(float *)((uchar *)&bk_theme + k_keys[i].off) = d->radius;
      }
    }
  }
  for (int i = 0; i < d->n; i++) { /* the short names first, then the exact ones over them */
    if (!d->entries[i].key) {
      apply_entry(&d->entries[i]);
    }
  }
  for (int i = 0; i < d->n; i++) {
    if (d->entries[i].key) {
      apply_entry(&d->entries[i]);
    }
  }
}

int BK_set_theme(const char *name)
{
  bk_theme_mark_set();
  if (!name) {
    name = "dark";
  }
  for (int i = 0; i < g_ndefs; i++) {
    if (names_equal(g_defs[i].name, name)) {
      apply_def(&g_defs[i]);
      snprintf(g_active, sizeof(g_active), "%s", g_defs[i].name);
      bk_theme_changed();
      return 1;
    }
  }
  const int t = bk_theme_from_name(name);
  if (t < 0) {
    return 0;
  }
  reset_applied_text(); /* leaving a custom theme that set the text */
  g_active[0] = '\0';
  bk_theme_load_preset(t);
  bk_theme_changed();
  return 1;
}

const char *BK_theme_name(void)
{
  bk_theme_env_init();
  return g_active[0] ? g_active : (bk_theme_is_light() ? "light" : "dark");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name What a host needs to follow the theme
 * \{ */

int BK_theme_generation(void)
{
  return bk_theme_gen;
}

const char *BK_theme_mode(void)
{
  bk_theme_env_init();
  return bk_theme_is_light() ? "light" : "dark";
}

/** \} */
