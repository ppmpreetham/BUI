/* See kit_json.h.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kit_json.h"

#define MAX_DEPTH 32
#define MAX_NODES 200000
#define MAX_INPUT ((size_t)16 * 1024 * 1024)

typedef struct Parser {
  const char *s, *end, *p;
  int nodes;
  char *err;
  size_t err_size;
  bool failed;
} Parser;

static void fail(Parser *ps, const char *msg)
{
  if (ps->failed) {
    return;
  }
  ps->failed = true;
  int line = 1, col = 1;
  for (const char *c = ps->s; c < ps->p && c < ps->end; c++) {
    if (*c == '\n') {
      line++;
      col = 1;
    }
    else {
      col++;
    }
  }
  if (ps->err && ps->err_size) {
    snprintf(ps->err, ps->err_size, "line %d, column %d: %s", line, col, msg);
  }
}

static void skip_ws(Parser *ps)
{
  while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) {
    ps->p++;
  }
}

static BKJson *node_new(Parser *ps, BKJsonType type)
{
  if (++ps->nodes > MAX_NODES) {
    fail(ps, "too many values");
    return NULL;
  }
  BKJson *v = (BKJson *)calloc(1, sizeof(BKJson));
  if (!v) {
    fail(ps, "out of memory");
    return NULL;
  }
  v->type = type;
  return v;
}

static int hex4(Parser *ps)
{
  int v = 0;
  for (int i = 0; i < 4; i++) {
    if (ps->p >= ps->end || !isxdigit((unsigned char)*ps->p)) {
      fail(ps, "bad \\u escape");
      return -1;
    }
    const char c = *ps->p++;
    v = v * 16 + (isdigit((unsigned char)c) ? c - '0' : (tolower((unsigned char)c) - 'a' + 10));
  }
  return v;
}

static size_t utf8_put(char *out, unsigned cp)
{
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

/* A string after its opening quote; returns a malloc'd buffer. */
static char *parse_string_body(Parser *ps)
{
  size_t cap = 16, n = 0;
  char *buf = (char *)malloc(cap);
  if (!buf) {
    fail(ps, "out of memory");
    return NULL;
  }
  for (;;) {
    if (ps->p >= ps->end) {
      fail(ps, "unterminated string");
      break;
    }
    const unsigned char c = (unsigned char)*ps->p++;
    if (c == '"') {
      buf[n] = '\0';
      return buf;
    }
    if (c < 0x20) {
      fail(ps, "control character in string");
      break;
    }
    if (n + 5 >= cap) {
      cap *= 2;
      char *grown = (char *)realloc(buf, cap);
      if (!grown) {
        fail(ps, "out of memory");
        break;
      }
      buf = grown;
    }
    if (c != '\\') {
      buf[n++] = (char)c;
      continue;
    }
    if (ps->p >= ps->end) {
      fail(ps, "unterminated string");
      break;
    }
    const char e = *ps->p++;
    switch (e) {
      case '"': buf[n++] = '"'; break;
      case '\\': buf[n++] = '\\'; break;
      case '/': buf[n++] = '/'; break;
      case 'b': buf[n++] = '\b'; break;
      case 'f': buf[n++] = '\f'; break;
      case 'n': buf[n++] = '\n'; break;
      case 'r': buf[n++] = '\r'; break;
      case 't': buf[n++] = '\t'; break;
      case 'u': {
        int cp = hex4(ps);
        if (cp < 0) {
          goto bad;
        }
        if (cp >= 0xD800 && cp < 0xDC00) { /* a surrogate pair */
          if (ps->p + 1 < ps->end && ps->p[0] == '\\' && ps->p[1] == 'u') {
            ps->p += 2;
            const int lo = hex4(ps);
            if (lo < 0) {
              goto bad;
            }
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          else {
            cp = 0xFFFD;
          }
        }
        n += utf8_put(buf + n, (unsigned)cp);
        break;
      }
      default:
        fail(ps, "bad escape");
        goto bad;
    }
  }
bad:
  free(buf);
  return NULL;
}

static BKJson *parse_value(Parser *ps, int depth);

static BKJson *parse_container(Parser *ps, int depth, bool object)
{
  if (depth >= MAX_DEPTH) {
    fail(ps, "nested too deeply");
    return NULL;
  }
  BKJson *v = node_new(ps, object ? BK_JSON_OBJECT : BK_JSON_ARRAY);
  if (!v) {
    return NULL;
  }
  const char close = object ? '}' : ']';
  BKJson **tail = &v->first;
  skip_ws(ps);
  if (ps->p < ps->end && *ps->p == close) {
    ps->p++;
    return v;
  }
  for (;;) {
    skip_ws(ps);
    char *key = NULL;
    if (object) {
      if (ps->p >= ps->end || *ps->p != '"') {
        fail(ps, "expected a member name");
        break;
      }
      ps->p++;
      key = parse_string_body(ps);
      if (!key) {
        break;
      }
      skip_ws(ps);
      if (ps->p >= ps->end || *ps->p != ':') {
        free(key);
        fail(ps, "expected ':'");
        break;
      }
      ps->p++;
    }
    BKJson *child = parse_value(ps, depth + 1);
    if (!child) {
      free(key);
      break;
    }
    child->key = key;
    *tail = child;
    tail = &child->next;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == ',') {
      ps->p++;
      continue;
    }
    if (ps->p < ps->end && *ps->p == close) {
      ps->p++;
      return v;
    }
    fail(ps, object ? "expected ',' or '}'" : "expected ',' or ']'");
    break;
  }
  bk_json_free(v);
  return NULL;
}

static BKJson *parse_value(Parser *ps, int depth)
{
  skip_ws(ps);
  if (ps->p >= ps->end) {
    fail(ps, "unexpected end of input");
    return NULL;
  }
  const char c = *ps->p;
  if (c == '{' || c == '[') {
    ps->p++;
    return parse_container(ps, depth, c == '{');
  }
  if (c == '"') {
    ps->p++;
    BKJson *v = node_new(ps, BK_JSON_STRING);
    if (!v) {
      return NULL;
    }
    v->string = parse_string_body(ps);
    if (!v->string) {
      bk_json_free(v);
      return NULL;
    }
    return v;
  }
  if (ps->end - ps->p >= 4 && !strncmp(ps->p, "true", 4)) {
    ps->p += 4;
    BKJson *v = node_new(ps, BK_JSON_BOOL);
    if (v) {
      v->boolean = true;
    }
    return v;
  }
  if (ps->end - ps->p >= 5 && !strncmp(ps->p, "false", 5)) {
    ps->p += 5;
    return node_new(ps, BK_JSON_BOOL);
  }
  if (ps->end - ps->p >= 4 && !strncmp(ps->p, "null", 4)) {
    ps->p += 4;
    return node_new(ps, BK_JSON_NULL);
  }
  if (c == '-' || isdigit((unsigned char)c)) {
    char *stop = NULL;
    char tmp[64];
    size_t n = 0;
    const char *q = ps->p;
    while (q < ps->end && n < sizeof(tmp) - 1 && strchr("+-0123456789.eE", *q)) {
      tmp[n++] = *q++;
    }
    tmp[n] = '\0';
    const double d = strtod(tmp, &stop);
    if (stop == tmp || *stop != '\0' || !isfinite(d)) {
      fail(ps, "bad number");
      return NULL;
    }
    ps->p = q;
    BKJson *v = node_new(ps, BK_JSON_NUMBER);
    if (v) {
      v->number = d;
    }
    return v;
  }
  fail(ps, "unexpected character");
  return NULL;
}

BKJson *bk_json_parse(const char *text, size_t len, char *err, size_t err_size)
{
  if (err && err_size) {
    err[0] = '\0';
  }
  if (!text || len > MAX_INPUT) {
    if (err && err_size) {
      snprintf(err, err_size, "input missing or larger than %zu bytes", MAX_INPUT);
    }
    return NULL;
  }
  Parser ps = {text, text + len, text, 0, err, err_size, false};
  if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
      (unsigned char)text[2] == 0xBF)
  {
    ps.p += 3; /* a UTF-8 byte order mark */
  }
  BKJson *v = parse_value(&ps, 0);
  if (v) {
    skip_ws(&ps);
    if (ps.p < ps.end) {
      fail(&ps, "unexpected text after the value");
      bk_json_free(v);
      return NULL;
    }
  }
  return v;
}

void bk_json_free(BKJson *v)
{
  while (v) {
    BKJson *next = v->next;
    bk_json_free(v->first);
    free(v->string);
    free(v->key);
    free(v);
    v = next;
  }
}

const BKJson *bk_json_get(const BKJson *obj, const char *key)
{
  if (!obj || obj->type != BK_JSON_OBJECT) {
    return NULL;
  }
  for (const BKJson *c = obj->first; c; c = c->next) {
    if (c->key && !strcmp(c->key, key)) {
      return c;
    }
  }
  return NULL;
}
