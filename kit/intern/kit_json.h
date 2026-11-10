/* A small JSON reader for theme files: objects, arrays, strings, numbers, booleans and null,
 * into a tree that is walked and freed by the caller. Bounded: nesting depth, input size and
 * node count are limited, and every failure reports where it happened.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef KIT_JSON_H
#define KIT_JSON_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  BK_JSON_NULL,
  BK_JSON_BOOL,
  BK_JSON_NUMBER,
  BK_JSON_STRING,
  BK_JSON_ARRAY,
  BK_JSON_OBJECT,
} BKJsonType;

typedef struct BKJson {
  BKJsonType type;
  bool boolean;
  double number;
  char *string;         /* BK_JSON_STRING: UTF-8, NUL terminated */
  char *key;            /* the member name when this is a member of an object */
  struct BKJson *first; /* first child (array element or object member) */
  struct BKJson *next;  /* next sibling */
} BKJson;

/* Parse `text` (`len` bytes). On failure returns NULL and writes a message such as
 * "line 3, column 14: expected ':'" into `err` (`err_size` bytes). */
BKJson *bk_json_parse(const char *text, size_t len, char *err, size_t err_size);
void bk_json_free(BKJson *v);

/* The member `key` of an object, or NULL. */
const BKJson *bk_json_get(const BKJson *obj, const char *key);

#endif
