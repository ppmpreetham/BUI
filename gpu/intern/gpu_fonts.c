/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Font discovery for the portable text engine (see gpu_fonts.h).
 *
 * Families by name: each font file in the font folders is opened once and
 * only its table directory, `name` table (family names 1 and 16) and `OS/2`
 * table (weight, italic) are read, never the outlines; collections (.ttc)
 * give each of their faces. That is a few kilobytes per file, on the first
 * lookup of a family that is neither registered nor generic. The folders are
 * read on a thread of their own from the first text drawn
 * (gpu_font_index_warm), and the result is kept in the user's cache folder
 * until a font folder changes; a lookup waits only if the thread is still
 * reading. */

#include "gpu_fonts.h"
#include "GPU_raster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dirent.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

int gpu_font_dirs(char dirs[][GPU_FONT_PATH], int max)
{
  int n = 0;
  const char *env = getenv("BUI_FONT_DIR");
  if (env && env[0] && n < max) {
    snprintf(dirs[n++], GPU_FONT_PATH, "%s", env);
  }
#ifdef _WIN32
  const char *win = getenv("WINDIR");
  if (n < max) {
    snprintf(dirs[n++], GPU_FONT_PATH, "%s/Fonts", win ? win : "C:/Windows");
  }
  const char *local = getenv("LOCALAPPDATA");
  if (local && n < max) {
    snprintf(dirs[n++], GPU_FONT_PATH, "%s/Microsoft/Windows/Fonts", local);
  }
#elif defined(__APPLE__)
  static const char *mac[] = {"/System/Library/Fonts", "/System/Library/Fonts/Supplemental",
                              "/Library/Fonts"};
  for (int i = 0; i < 3 && n < max; i++) {
    snprintf(dirs[n++], GPU_FONT_PATH, "%s", mac[i]);
  }
  const char *home = getenv("HOME");
  if (home && n < max) {
    snprintf(dirs[n++], GPU_FONT_PATH, "%s/Library/Fonts", home);
  }
#else
  static const char *nix[] = {
      "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/dejavu",
      "/usr/share/fonts/TTF",             "/usr/share/fonts/truetype/liberation",
      "/usr/share/fonts/liberation",      "/usr/share/fonts/truetype/noto",
      "/usr/share/fonts/noto",            "/usr/share/fonts/google-noto",
      "/usr/share/fonts/opentype/noto",   "/usr/share/fonts/noto-cjk",
      "/usr/share/fonts/google-noto-cjk", "/usr/share/fonts/truetype",
      "/usr/local/share/fonts",           "/usr/share/fonts"};
  for (int i = 0; i < (int)(sizeof(nix) / sizeof(nix[0])) && n < max; i++) {
    snprintf(dirs[n++], GPU_FONT_PATH, "%s", nix[i]);
  }
  const char *home = getenv("HOME");
  if (home && n < max) {
    snprintf(dirs[n++], GPU_FONT_PATH, "%s/.local/share/fonts", home);
  }
  if (home && n < max) {
    snprintf(dirs[n++], GPU_FONT_PATH, "%s/.fonts", home);
  }
#endif
  return n;
}

/* -- the family index ---------------------------------------------------------------- */

typedef struct Named {
  char family[64];
  char path[GPU_FONT_PATH];
  int index; /* face in a collection */
  int style; /* BUI_FONT_BOLD | BUI_FONT_ITALIC */
} Named;

static Named *g_named = NULL;
static int g_named_n = 0, g_named_cap = 0;

static unsigned rd16(const unsigned char *p)
{
  return (unsigned)p[0] << 8 | p[1];
}

static unsigned long rd32(const unsigned char *p)
{
  return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

static int read_at(FILE *fp, unsigned long off, void *buf, size_t n)
{
  return fseek(fp, (long)off, SEEK_SET) == 0 && fread(buf, 1, n, fp) == n;
}

static int same_name(const char *a, const char *b)
{
  for (; *a && *b; a++, b++) {
    const char ca = (char)(*a >= 'A' && *a <= 'Z' ? *a + 32 : *a);
    const char cb = (char)(*b >= 'A' && *b <= 'Z' ? *b + 32 : *b);
    if (ca != cb) {
      return 0;
    }
  }
  return *a == *b;
}

/* Appends a record (a saved index's are distinct already: see named_add). */
static void named_push(const char *family, const char *path, int index, int style)
{
  if (g_named_n == g_named_cap) {
    const int cap = g_named_cap ? g_named_cap * 2 : 256;
    Named *grown = (Named *)realloc(g_named, sizeof(Named) * (size_t)cap);
    if (!grown) {
      return;
    }
    g_named = grown;
    g_named_cap = cap;
  }
  Named *e = &g_named[g_named_n++];
  snprintf(e->family, sizeof(e->family), "%s", family);
  snprintf(e->path, sizeof(e->path), "%s", path);
  e->index = index;
  e->style = style;
}

/* Adds a record read from a font, once (family 1 and 16 are often the same name). */
static void named_add(const char *family, const char *path, int index, int style)
{
  for (int i = 0; i < g_named_n; i++) {
    const Named *e = &g_named[i];
    if (e->index == index && e->style == style && same_name(e->family, family) && strcmp(e->path, path) == 0) {
      return;
    }
  }
  named_push(family, path, index, style);
}

/* A `name` record's string as UTF-8: UTF-16BE (Windows / Unicode
 * platforms) or a single-byte Mac encoding (its ASCII part). */
static void name_utf8(const unsigned char *s, unsigned len, int utf16, char *out, size_t cap)
{
  size_t o = 0;
  for (unsigned i = 0; i < len && o + 4 < cap;) {
    unsigned c;
    if (utf16) {
      if (i + 1 >= len) {
        break;
      }
      c = rd16(s + i);
      i += 2;
    }
    else {
      c = s[i++];
      if (c >= 0x80) {
        continue;
      }
    }
    if (c < 0x80) {
      out[o++] = (char)c;
    }
    else if (c < 0x800) {
      out[o++] = (char)(0xc0 | c >> 6);
      out[o++] = (char)(0x80 | (c & 0x3f));
    }
    else if (c < 0xd800 || c > 0xdfff) {
      out[o++] = (char)(0xe0 | c >> 12);
      out[o++] = (char)(0x80 | (c >> 6 & 0x3f));
      out[o++] = (char)(0x80 | (c & 0x3f));
    }
  }
  out[o] = 0;
}

/* The family names and style of the face at `off` of a font file. */
static void named_face(FILE *fp, const char *path, int index, unsigned long off)
{
  unsigned char hdr[12];
  if (!read_at(fp, off, hdr, sizeof(hdr))) {
    return;
  }
  const unsigned nt = rd16(hdr + 4);
  if (nt == 0 || nt > 64) {
    return;
  }
  unsigned char dir[64 * 16];
  if (!read_at(fp, off + 12, dir, (size_t)nt * 16)) {
    return;
  }
  unsigned long name_off = 0, name_len = 0, os2_off = 0, os2_len = 0;
  for (unsigned k = 0; k < nt; k++) {
    const unsigned char *t = dir + k * 16;
    if (memcmp(t, "name", 4) == 0) {
      name_off = rd32(t + 8);
      name_len = rd32(t + 12);
    }
    else if (memcmp(t, "OS/2", 4) == 0) {
      os2_off = rd32(t + 8);
      os2_len = rd32(t + 12);
    }
  }
  if (!name_off || name_len < 6 || name_len > (1ul << 20)) {
    return;
  }
  /* only the fields the table holds: usWeightClass (bytes 4-5), fsSelection (62-63) */
  int style = 0;
  unsigned char os2[64];
  const size_t os2_read = os2_len < sizeof(os2) ? (size_t)os2_len : sizeof(os2);
  if (os2_off && os2_read >= 6 && read_at(fp, os2_off, os2, os2_read)) {
    style |= rd16(os2 + 4) >= 600 ? BUI_FONT_BOLD : 0;
    style |= (os2_read >= 64 && (rd16(os2 + 62) & 1)) ? BUI_FONT_ITALIC : 0;
  }
  unsigned char *nm = (unsigned char *)malloc(name_len);
  if (!nm || !read_at(fp, name_off, nm, name_len)) {
    free(nm);
    return;
  }
  const unsigned count = rd16(nm + 2), strings = rd16(nm + 4);
  for (unsigned r = 0; r < count && 6 + (r + 1) * 12 <= name_len; r++) {
    const unsigned char *rec = nm + 6 + r * 12;
    const unsigned plat = rd16(rec), id = rd16(rec + 6), len = rd16(rec + 8), soff = rd16(rec + 10);
    /* family (1) and typographic family (16): "Yu Gothic Medium" and "Yu Gothic" */
    if ((id != 1 && id != 16) || (plat != 0 && plat != 1 && plat != 3) ||
        (unsigned long)strings + soff + len > name_len)
    {
      continue;
    }
    char family[64];
    name_utf8(nm + strings + soff, len, plat != 1, family, sizeof(family));
    if (family[0]) {
      named_add(family, path, index, style);
    }
  }
  free(nm);
}

static void named_file(const char *path)
{
#ifdef _WIN32
  FILE *fp = fopen(path, "rb");
#else
  /* nonblocking, and a regular file only: a FIFO named like a font (or put in one's place
   * since the folder was read) would stall the open or the reads */
  const int fd = open(path, O_RDONLY | O_NONBLOCK);
  struct stat st;
  FILE *fp = fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode) ? fdopen(fd, "rb") : NULL;
  if (!fp && fd >= 0) {
    close(fd);
  }
#endif
  if (!fp) {
    return;
  }
  unsigned char hdr[12];
  if (read_at(fp, 0, hdr, sizeof(hdr))) {
    if (memcmp(hdr, "ttcf", 4) == 0) {
      const unsigned long n = rd32(hdr + 8);
      for (unsigned long i = 0; i < n && i < 32; i++) {
        unsigned char o[4];
        if (read_at(fp, 12 + i * 4, o, 4)) {
          named_face(fp, path, (int)i, rd32(o));
        }
      }
    }
    else {
      named_face(fp, path, 0, 0);
    }
  }
  fclose(fp);
}

static int font_file(const char *name)
{
  const size_t n = strlen(name);
  if (n < 5 || name[n - 4] != '.') {
    return 0;
  }
  const char *e = name + n - 3;
  return same_name(e, "ttf") || same_name(e, "otf") || same_name(e, "ttc");
}

static long long dir_stamp(const char *dir);

/* The folders read below the font folders, with when each last changed: a
 * font added to one changes its stamp, not the font folder's, so the saved
 * index keeps them all (index_save). */
typedef struct SubDir {
  long long stamp;
  char dir[GPU_FONT_PATH];
} SubDir;
static SubDir *g_subdirs = NULL;
static int g_subdir_n = 0, g_subdir_cap = 0;
static int g_subdir_lost = 0; /* one couldn't be kept: the index isn't saved */

static void subdir_add(const char *dir)
{
  if (g_subdir_n == g_subdir_cap) {
    const int cap = g_subdir_cap ? g_subdir_cap * 2 : 64;
    SubDir *grown = (SubDir *)realloc(g_subdirs, sizeof(SubDir) * (size_t)cap);
    if (!grown) {
      g_subdir_lost = 1;
      return;
    }
    g_subdirs = grown;
    g_subdir_cap = cap;
  }
  SubDir *s = &g_subdirs[g_subdir_n++];
  s->stamp = dir_stamp(dir);
  snprintf(s->dir, sizeof(s->dir), "%s", dir);
}

/* Every font file under `dir`, `depth` folders deep. */
static void named_dir(const char *dir, int depth)
{
  char path[GPU_FONT_PATH];
#ifdef _WIN32
  char pattern[GPU_FONT_PATH + 4];
  snprintf(pattern, sizeof(pattern), "%s/*", dir);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) {
    return;
  }
  do {
    if (fd.cFileName[0] == '.') {
      continue;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, fd.cFileName);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (depth > 0) {
        subdir_add(path);
        named_dir(path, depth - 1);
      }
    }
    else if (font_file(fd.cFileName)) {
      named_file(path);
    }
  } while (FindNextFileA(h, &fd));
  FindClose(h);
#else
  DIR *d = opendir(dir);
  if (!d) {
    return;
  }
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.') {
      continue;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
    struct stat st;
    if (stat(path, &st) != 0) {
      continue;
    }
    if (S_ISDIR(st.st_mode)) {
      if (depth > 0) {
        subdir_add(path);
        named_dir(path, depth - 1);
      }
    }
    else if (S_ISREG(st.st_mode) && font_file(ent->d_name)) {
      named_file(path);
    }
  }
  closedir(d);
#endif
}

/* -- the saved index ------------------------------------------------------------------- */

#define INDEX_MAGIC "BUI font index 2"

/* When `dir` last changed (a font added or removed changes it); 0 when absent. */
static long long dir_stamp(const char *dir)
{
#ifdef _WIN32
  WIN32_FILE_ATTRIBUTE_DATA a;
  if (!GetFileAttributesExA(dir, GetFileExInfoStandard, &a)) {
    return 0;
  }
  return (long long)a.ftLastWriteTime.dwHighDateTime << 32 | a.ftLastWriteTime.dwLowDateTime;
#else
  struct stat st;
  return stat(dir, &st) == 0 ? (long long)st.st_mtime : 0;
#endif
}

static void make_dir(const char *dir)
{
#ifdef _WIN32
  CreateDirectoryA(dir, NULL);
#else
  mkdir(dir, 0755);
#endif
}

/* Where the index is kept between runs (the user's cache folder); 0 when there is none. */
static int index_path(char r_path[GPU_FONT_PATH])
{
#ifdef _WIN32
  const char *base = getenv("LOCALAPPDATA");
  if (!base || !base[0]) {
    return 0;
  }
  snprintf(r_path, GPU_FONT_PATH, "%s/bui", base);
#elif defined(__APPLE__)
  const char *home = getenv("HOME");
  if (!home || !home[0]) {
    return 0;
  }
  snprintf(r_path, GPU_FONT_PATH, "%s/Library/Caches/bui", home);
#else
  const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
  if (xdg && xdg[0]) {
    snprintf(r_path, GPU_FONT_PATH, "%s/bui", xdg);
  }
  else if (home && home[0]) {
    char cache[GPU_FONT_PATH];
    snprintf(cache, sizeof(cache), "%s/.cache", home);
    make_dir(cache);
    snprintf(r_path, GPU_FONT_PATH, "%s/bui", cache);
  }
  else {
    return 0;
  }
#endif
  make_dir(r_path);
  const size_t n = strlen(r_path);
  snprintf(r_path + n, GPU_FONT_PATH - n, "/font-index.txt");
  return 1;
}

/* The saved index, when it was made from these folders as they are now. Lines:
 * the magic, "D <stamp> <folder>" per font folder, "S <stamp> <folder>" per
 * folder read below them, then "F <index> <style> <family>\t<path>". */
static int index_load(char dirs[][GPU_FONT_PATH], const long long *stamps, int nd)
{
  char path[GPU_FONT_PATH];
  FILE *fp = index_path(path) ? fopen(path, "r") : NULL;
  if (!fp) {
    return 0;
  }
  char line[GPU_FONT_PATH + 128];
  int ok = fgets(line, sizeof(line), fp) && strncmp(line, INDEX_MAGIC, strlen(INDEX_MAGIC)) == 0;
  for (int i = 0; ok && i < nd; i++) {
    long long stamp = 0;
    int at = 0;
    ok = fgets(line, sizeof(line), fp) && sscanf(line, "D %lld %n", &stamp, &at) == 1 && stamp == stamps[i];
    line[strcspn(line, "\r\n")] = 0;
    ok = ok && strcmp(line + at, dirs[i]) == 0;
  }
  while (ok && fgets(line, sizeof(line), fp)) {
    line[strcspn(line, "\r\n")] = 0;
    long long stamp = 0;
    int index = 0, style = 0, at = 0;
    if (line[0] == 'S' && sscanf(line, "S %lld %n", &stamp, &at) == 1) {
      ok = stamp == dir_stamp(line + at); /* a font added to or removed from it */
      continue;
    }
    char *tab = strchr(line, '\t');
    if (line[0] != 'F' || !tab || sscanf(line, "F %d %d %n", &index, &style, &at) != 2) {
      ok = 0; /* a folder more than now, or a broken line */
      break;
    }
    *tab = 0;
    named_push(line + at, tab + 1, index, style);
  }
  fclose(fp);
  if (!ok) {
    g_named_n = 0;
  }
  return ok;
}

/* Written whole to a temporary file of this process's own (another process
 * may be writing the index too), then put in place: a reader never sees half
 * of it. */
static void index_save(char dirs[][GPU_FONT_PATH], const long long *stamps, int nd)
{
  char path[GPU_FONT_PATH], tmp[GPU_FONT_PATH + 24];
  if (g_subdir_lost || !index_path(path)) {
    return;
  }
#ifdef _WIN32
  const unsigned long pid = (unsigned long)GetCurrentProcessId();
#else
  const unsigned long pid = (unsigned long)getpid();
#endif
  snprintf(tmp, sizeof(tmp), "%s.%lu.tmp", path, pid);
  FILE *fp = fopen(tmp, "w");
  if (!fp) {
    return;
  }
  fprintf(fp, "%s\n", INDEX_MAGIC);
  for (int i = 0; i < nd; i++) {
    fprintf(fp, "D %lld %s\n", stamps[i], dirs[i]);
  }
  for (int i = 0; i < g_subdir_n; i++) {
    fprintf(fp, "S %lld %s\n", g_subdirs[i].stamp, g_subdirs[i].dir);
  }
  for (int i = 0; i < g_named_n; i++) {
    fprintf(fp, "F %d %d %s\t%s\n", g_named[i].index, g_named[i].style, g_named[i].family, g_named[i].path);
  }
  const int written = fclose(fp) == 0;
#ifdef _WIN32
  if (!written || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileA(tmp);
  }
#else
  if (!written || rename(tmp, path) != 0) {
    remove(tmp);
  }
#endif
}

static void index_build(void)
{
  char dirs[GPU_FONT_DIRS_MAX][GPU_FONT_PATH];
  long long stamps[GPU_FONT_DIRS_MAX];
  const int nd = gpu_font_dirs(dirs, GPU_FONT_DIRS_MAX);
  for (int i = 0; i < nd; i++) {
    stamps[i] = dir_stamp(dirs[i]);
  }
  if (index_load(dirs, stamps, nd)) {
    return;
  }
  for (int i = 0; i < nd; i++) {
    named_dir(dirs[i], 3);
  }
  index_save(dirs, stamps, nd);
  free(g_subdirs); /* only the saved index needs them */
  g_subdirs = NULL;
  g_subdir_n = g_subdir_cap = 0;
}

/* -- built off the UI thread ------------------------------------------------------------ */

/* Two run-once steps, so text calls on several threads still build the index exactly once
 * and every lookup sees it whole: `warm` decides whether a thread builds it (started by
 * gpu_font_index_warm, or never once a lookup came first), `ready` waits for that thread or
 * builds it in place. g_named is written only before `ready` completes, read only after. */
static int g_index_started = 0;
#ifdef _WIN32
static INIT_ONCE g_warm_once = INIT_ONCE_STATIC_INIT, g_ready_once = INIT_ONCE_STATIC_INIT;
static HANDLE g_index_thread = NULL;
static DWORD WINAPI index_main(LPVOID arg)
{
  (void)arg;
  index_build();
  return 0;
}
static BOOL CALLBACK warm_start(PINIT_ONCE once, PVOID start, PVOID *ctx)
{
  (void)once, (void)ctx;
  if (start) {
    g_index_thread = CreateThread(NULL, 0, index_main, NULL, 0, NULL);
    g_index_started = g_index_thread != NULL;
  }
  return TRUE;
}
static BOOL CALLBACK ready_main(PINIT_ONCE once, PVOID arg, PVOID *ctx)
{
  (void)once, (void)arg, (void)ctx;
  InitOnceExecuteOnce(&g_warm_once, warm_start, NULL, NULL); /* no thread from now on */
  if (g_index_started) {
    WaitForSingleObject(g_index_thread, INFINITE);
    CloseHandle(g_index_thread);
  }
  else {
    index_build();
  }
  return TRUE;
}

void gpu_font_index_warm(void)
{
  InitOnceExecuteOnce(&g_warm_once, warm_start, (PVOID)1, NULL);
}

/* The index, waiting for its thread if it is still reading (built here when
 * nothing started it). */
static void index_ready(void)
{
  InitOnceExecuteOnce(&g_ready_once, ready_main, NULL, NULL);
}
#else
#  include <pthread.h>
static pthread_once_t g_warm_once = PTHREAD_ONCE_INIT, g_ready_once = PTHREAD_ONCE_INIT;
static pthread_t g_index_thread;
static void *index_main(void *arg)
{
  (void)arg;
  index_build();
  return NULL;
}
static void warm_start(void)
{
  g_index_started = pthread_create(&g_index_thread, NULL, index_main, NULL) == 0;
}
static void warm_none(void) {}
static void ready_main(void)
{
  pthread_once(&g_warm_once, warm_none); /* no thread from now on */
  if (g_index_started) {
    pthread_join(g_index_thread, NULL);
  }
  else {
    index_build();
  }
}

void gpu_font_index_warm(void)
{
  pthread_once(&g_warm_once, warm_start);
}

/* The index, waiting for its thread if it is still reading (built here when
 * nothing started it). */
static void index_ready(void)
{
  pthread_once(&g_ready_once, ready_main);
}
#endif

int gpu_font_named(const char *family, int style, char r_path[GPU_FONT_PATH], int *r_index, int *r_have)
{
  index_ready();
  /* the exact style, else the nearest: bold matters more than italic */
  int best = -1, best_cost = 99;
  for (int i = 0; i < g_named_n; i++) {
    const Named *e = &g_named[i];
    if (!same_name(e->family, family)) {
      continue;
    }
    const int diff = e->style ^ style;
    const int cost = ((diff & BUI_FONT_BOLD) ? 2 : 0) + ((diff & BUI_FONT_ITALIC) ? 1 : 0);
    if (cost < best_cost) {
      best = i;
      best_cost = cost;
    }
  }
  if (best < 0) {
    return 0;
  }
  snprintf(r_path, GPU_FONT_PATH, "%s", g_named[best].path);
  *r_index = g_named[best].index;
  *r_have = g_named[best].style;
  return 1;
}

/* -- fallback for scripts ------------------------------------------------------------ */

typedef struct Range {
  int lo, hi, group;
} Range;

static const Range g_ranges[] = {
    {0x1100, 0x11ff, GPU_FALLBACK_HANGUL}, {0x3130, 0x318f, GPU_FALLBACK_HANGUL},
    {0xa960, 0xa97f, GPU_FALLBACK_HANGUL}, {0xac00, 0xd7ff, GPU_FALLBACK_HANGUL},
    {0x3040, 0x30ff, GPU_FALLBACK_KANA},   {0x31f0, 0x31ff, GPU_FALLBACK_KANA},
    {0xff65, 0xff9f, GPU_FALLBACK_KANA},   {0x2e80, 0x2fdf, GPU_FALLBACK_HAN},
    {0x3000, 0x303f, GPU_FALLBACK_HAN},    {0x3400, 0x4dbf, GPU_FALLBACK_HAN},
    {0x4e00, 0x9fff, GPU_FALLBACK_HAN},    {0xf900, 0xfaff, GPU_FALLBACK_HAN},
    {0xff00, 0xffef, GPU_FALLBACK_HAN},    {0x20000, 0x3ffff, GPU_FALLBACK_HAN},
    {0x0e00, 0x0e7f, GPU_FALLBACK_THAI},   {0x0900, 0x0dff, GPU_FALLBACK_INDIC},
    {0x2000, 0x2bff, GPU_FALLBACK_SYMBOLS}, {0x1f000, 0x1faff, GPU_FALLBACK_SYMBOLS},
};

/* Bold faces, most wanted first (bold text drawn with a real bold face, not
 * a smeared regular one); a group without one falls back to its regular. */
static const char *g_bold_files[GPU_FALLBACK_GROUPS] = {
    "malgunbd.ttf|NotoSansCJK-Bold.ttc|NotoSansKR-Bold.otf|NotoSansCJKkr-Bold.otf",
    "YuGothB.ttc|meiryob.ttc|NotoSansCJK-Bold.ttc|NotoSansJP-Bold.otf|NotoSansCJKjp-Bold.otf",
    "msyhbd.ttc|YuGothB.ttc|NotoSansCJK-Bold.ttc|NotoSansSC-Bold.otf|NotoSansCJKsc-Bold.otf",
    "LeelaUIb.ttf|NotoSansThai-Bold.ttf",
    "NirmalaB.ttf|NotoSansDevanagari-Bold.ttf",
    "seguisym.ttf|NotoSansSymbols2-Regular.ttf|DejaVuSans-Bold.ttf",
    "segoeuib.ttf|arialbd.ttf|NotoSans-Bold.ttf|DejaVuSans-Bold.ttf",
};

/* Most wanted first: Windows, macOS, then Noto / DejaVu (Linux). */
static const char *g_files[GPU_FALLBACK_GROUPS] = {
    /* Hangul */
    "malgun.ttf|AppleSDGothicNeo.ttc|NotoSansCJK-Regular.ttc|NotoSansKR-Regular.otf|"
    "NotoSansCJKkr-Regular.otf",
    /* kana */
    "YuGothM.ttc|meiryo.ttc|msgothic.ttc|Hiragino Sans GB.ttc|NotoSansCJK-Regular.ttc|"
    "NotoSansJP-Regular.otf|NotoSansCJKjp-Regular.otf",
    /* Han */
    "msyh.ttc|YuGothM.ttc|simsun.ttc|PingFang.ttc|Hiragino Sans GB.ttc|NotoSansCJK-Regular.ttc|"
    "NotoSansSC-Regular.otf|NotoSansCJKsc-Regular.otf",
    /* Thai */
    "LeelawUI.ttf|leelawui.ttf|Thonburi.ttc|NotoSansThai-Regular.ttf",
    /* Indic */
    "Nirmala.ttf|Kohinoor.ttc|NotoSansDevanagari-Regular.ttf",
    /* symbols */
    "seguisym.ttf|seguiemj.ttf|Apple Symbols.ttf|NotoSansSymbols2-Regular.ttf|DejaVuSans.ttf",
    /* anything else (Arabic, Hebrew, Greek and Cyrillic beyond the UI font ...) */
    "segoeui.ttf|arial.ttf|ArialHB.ttc|GeezaPro.ttc|NotoSans-Regular.ttf|DejaVuSans.ttf",
};

int gpu_font_fallback_group(int cp)
{
  for (size_t i = 0; i < sizeof(g_ranges) / sizeof(g_ranges[0]); i++) {
    if (cp >= g_ranges[i].lo && cp <= g_ranges[i].hi) {
      return g_ranges[i].group;
    }
  }
  return GPU_FALLBACK_ANY;
}

const char *gpu_font_fallback_files(int group, int bold)
{
  if (group < 0 || group >= GPU_FALLBACK_GROUPS) {
    return "";
  }
  return bold ? g_bold_files[group] : g_files[group];
}
