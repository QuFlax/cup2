/**
 * @file utils.c
 * @brief Runtime utilities: allocator, logger, dynamic vectors, symbol hashmap
 */

#include "cup.h"
//#include <threads.h>
#ifndef thread_local
  #if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define thread_local _Thread_local
  #elif defined(__GNUC__) || defined(__clang__)
    #define thread_local __thread
  #elif defined(__MSC_VER)
    #define thread_local __declspec(thread)
  #else
    #error "Thread-local storage not supported by this compiler."
  #endif
#endif

#define INITIAL_CAPACITY (sizeof(size_t) * 8)

/* ========================================================================== */
/*                       THREAD-LOCAL ALLOCATOR / LOGGER                      */
/* ========================================================================== */

thread_local static CUP_Realloc_Fn cup_realloc_fn;
thread_local static CUP_Log_Fn     cup_log_fn;

static void default_log(CUP_LogLevel level, const char *msg) {
  switch (level) {
  case CUP_Level_INFO: fputs("INFO: ",  stderr); break;
  case CUP_Level_WARN: fputs("WARN: ",  stderr); break;
  case CUP_Level_ERRO: fputs("ERROR: ", stderr); break;
  }
  fputs(msg, stderr);
  fputc('\n', stderr);
}

static void *default_reallocator(void *ptr, size_t size) {
  if (size != 0) {
    void *p = realloc(ptr, size);
    if (p) return p;
    default_log(CUP_Level_ERRO, "out of memory");
    exit(1);
  }
  free(ptr);
  return NULL;
}

CUP_Realloc_Fn *_cup_realloc(void) {
  if (!cup_realloc_fn) cup_realloc_fn = default_reallocator;
  return &cup_realloc_fn;
}
CUP_Log_Fn *_cup_log(void) {
  if (!cup_log_fn) cup_log_fn = default_log;
  return &cup_log_fn;
}

void cup_set_realloc(CUP_Realloc_Fn fn)  { *_cup_realloc() = fn; }
void cup_set_error_func(CUP_Log_Fn fn)   { *_cup_log()     = fn; }

/* ========================================================================== */
/*                             STRING HELPERS                                 */
/* ========================================================================== */

char *cup_strdup(const char *str) {
  size_t len = strlen(str);
  char *s = cup_malloc(len + 1);
  memcpy(s, str, len);
  s[len] = '\0';
  return s;
}

char *cup_sprintf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  char *r = cup_vsprintf(format, args);
  va_end(args);
  return r;
}

char *cup_vsprintf(const char *format, va_list args) {
  size_t size = INITIAL_CAPACITY;
  char *buf = (char *)cup_malloc(size);
  for (;;) {
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(buf, size, format, copy);
    va_end(copy);
    if (n < 0) {
      cup_free(buf);
      cup_log(CUP_Level_ERRO, "format error");
      return cup_strdup("format error");
    }
    if ((size_t)n < size) return buf;
    size = (size_t)(n + 1);
    buf = (char *)cup_realloc(buf, size);
  }
}

size_t next_power_of_2(size_t n) {
  if (n <= 1)
    return n;
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  return n + 1;
}

thread_local static size_t temppos = 0;
thread_local static uint8_t tempmem[TEMP_CAP] = {};

void* cup_talloc(size_t size) {
  size_t pos = size + temppos;
  if (pos < TEMP_CAP) 
    return tempmem + temppos;
  cup_error("MAX");
  exit(1);
}

void cup_treset(size_t size) {
  temppos = size;
}

size_t cup_tpos() {
  return temppos;
}

/* ========================================================================== */
/*                           DYNAMIC VECTORS                                  */
/* ========================================================================== */

static void vector_resize(CVector *vec, size_t needed) {
  if (!vec || needed == 0) return;
  if (vec->capacity >= needed) return;
  size_t cap = vec->capacity ? vec->capacity : INITIAL_CAPACITY;
  while (cap < needed) cap *= 2;
  vec->data     = cup_realloc(vec->data, cap);
  vec->capacity = cap;
}

void vector_new(CVector *vec, size_t sizeT) {
  assert(vec && sizeT > 0 && vec->size == 0);
  vector_resize(vec, sizeT);
}

void vector_pop(CVector *vec, size_t sizeT) {
  assert(vec && sizeT > 0);
  if (vec->size <= sizeT) {
    cup_free(vec->data);
    vec->size = 0;
    return;
  }
  vec->size -= sizeT;
}

void vector_fpop(CVector *vec, size_t sizeT) {
  vector_pop(vec, sizeT);
  memmove(vec->data, (char *)vec->data + sizeT, vec->size);
}

void vector_push(CVector *vec, const void *data, size_t sizeT) {
  assert(vec && sizeT > 0 && data);
  vector_resize(vec, vec->size + sizeT);
  memcpy((char *)vec->data + vec->size, data, sizeT);
  vec->size += sizeT;
}

void push_vector(CVector *dest, CVector src) {
  if (!src.data) return;
  vector_push(dest, src.data, src.size);
  cup_free(src.data);
}

void vector_fpush(CVector *vec, const void *data, size_t sizeT) {
  assert(vec && sizeT > 0 && data);
  vector_resize(vec, vec->size + sizeT);
  memmove((char *)vec->data + sizeT, vec->data, vec->size);
  memcpy(vec->data, data, sizeT);
  vec->size += sizeT;
}

void fpush_vector(CVector *dest, CVector src) {
  vector_fpush(dest, src.data, src.size);
  if (src.data) cup_free(src.data);
}

void split_vector(CVector *dest, CVector* src, size_t size) {
  assert(dest && src);
  if (size >= src->size)
    return;
  vector_push(dest, (char*)src->data + size, src->size - size);
  src->size = size;
}

/* ========================================================================== */
/*                         SYMBOL HASH MAP                                    */
/* ========================================================================== */

/* 
 * Open-addressing Robin Hood hash map.
 * Robin Hood: on collision we steal the slot from whichever entry has the
 * smaller probe distance ("richer" entry), keeping max probe length short.
 */

/*
size_t map_hash(size_t key) {
  // Fibonacci / multiplicative hashing for size_t keys
  return key * (size_t)11400714819323198485ULL;
}

void map_init(CMap *m, size_t k_null, size_t v_size, void* v_default) {
  m->count    = 0;
  m->capacity = CMAP_INIT_CAPACITY;
  //m->k_null   = k_null;
  m->v_size   = v_size;
  //m->hash     = hash_fn;
  size_t item_size = offsetof(CMapItem, data) + m->v_size;
  assert(sizeof(size_t) * 8 >= item_size);
  assert(sizeof(size_t) <= m->v_size);
  m->slots = cup_malloc(item_size * m->capacity);
  for (size_t i = 0; i < m->capacity; i++) {
    CMapItem *item = (CMapItem *)((char*)m->slots + i * item_size);
    item->key = k_null; item->dist = SIZE_MAX;
    if (v_default)
      memcpy(item->data, v_default, m->v_size);
    else
      memset(item->data, 0, m->v_size);
  }
}
void map_free(CMap *m) {
  if (m == NULL) return;
  cup_free(m->slots);
  m->slots = NULL;
  m->count = 0;
  m->capacity = 0;
}
static void cmap_grow(CMap *m, size_t k_null) {
  size_t new_cap   = m->capacity * 2;
  size_t item_size = offsetof(CMapItem, data) + m->v_size;
  void *new_slots = cup_malloc(item_size * new_cap);
  CMapItem* item_default = NULL;
  for (size_t i = 0; !item_default && i < m->capacity; i++) {
    CMapItem *item = (CMapItem *)((char*)m->slots + i * item_size);
    if (item->key == k_null) item_default = item;
  }
  for (size_t i = 0; i < new_cap; i++) {
    CMapItem *item = (CMapItem *)((char*)new_slots + i * item_size);
    memcpy(item, item_default, item_size);
  }
  void* old_slots = m->slots;
  size_t old_cap = m->capacity;
  m->slots    = new_slots;
  m->capacity = new_cap;

  for (size_t i = 0; i < old_cap; i++) {
    CMapItem *item = (CMapItem *)((char*)old_slots + i * item_size);
    if (item->key == k_null) continue;
    map_put(m, item->key, item->data, k_null);
  }
  cup_free(old_slots);
}
CMapItem *map_get(const CMap *m, size_t key, CMapItem *prev, size_t k_null) {
  if (!m->slots || m->capacity == 0) return NULL;
  size_t item_size = offsetof(CMapItem, data) + m->v_size;
  size_t i;
  if (prev) {
    i = ((const char *)prev - (const char *)m->slots) / item_size;
    i = (i + 1) & (m->capacity - 1);
  } else {
    i = map_hash(key) & (m->capacity - 1);
  }
  size_t dist = 0;
  for (;;) {
    CMapItem *item = (CMapItem *)((char*)m->slots + i * item_size);
    if (item->key == key) return item;
    if (item->key == k_null || item->dist < dist) return NULL;
    ++dist;
    i = (i + 1) & (m->capacity - 1);
  }
}
CMapItem *map_put(CMap *m, size_t key, void *value, size_t k_null) {
  assert(value);
  if (m->count * CMAP_LOAD_DEN >= m->capacity * CMAP_LOAD_NUM)
    cmap_grow(m, k_null);
  size_t item_size = offsetof(CMapItem, data) + m->v_size;
  size_t i = map_hash(key) & (m->capacity - 1);
  size_t dist = 0;
  for (;;) {
    CMapItem *item = (CMapItem *)((char*)m->slots + i * item_size);
    if (item->key == k_null) {
      item->key = key; item->dist = dist; memcpy(item->data, value, m->v_size);
      m->count++;
      return item;
    }
#ifdef MAP_REPUT
    if (item->key == key) {
      memcpy(item->data, value, m->v_size);
      return item;
    }
#endif
    if (item->dist < dist) {
      // Robin Hood swap
      uint8_t tmp[sizeof(size_t) * 8];
      memcpy(tmp, item, item_size);
      item->key = key; item->dist = dist; memcpy(item->data, value, m->v_size);
      dist = ((size_t*)tmp)[1];
      key = *((size_t*)tmp);
      memcpy(value, (char*)tmp + offsetof(CMapItem, data), m->v_size);
    }
    dist++;
    i = (i + 1) & (m->capacity - 1);
  }
}
void map_iter(const CMap *m, void *ctx, map_iter_callback cb, size_t k_null) {
  if (!m || !m->slots) return;
  size_t item_size = offsetof(CMapItem, data) + m->v_size;
  for (size_t i = 0; i < m->capacity; i++) {
    const CMapItem *item = (CMapItem *)((char*)m->slots + i * item_size);
    if (item->key == k_null) continue;
    cb(ctx, item);
  }
}
*/

/*

void symmap_del(SymbolMap *m, size_t name_idx) {
  if (!m->slots || m->capacity == 0) return;
  size_t idx  = sym_hash(name_idx) & (m->capacity - 1);
  size_t dist = 0;
  for (;;) {
    SymEntry *slot = &m->slots[idx];
    if (slot_empty(slot) || slot->dist < dist) return;
    if (slot->vars[0].name == name_idx) {
       Backward-shift deletion preserves Robin Hood invariant
      slot->vars[0].name = SYMMAP_EMPTY;
      m->count--;
      for (;;) {
        size_t    next      = (idx + 1) & (m->capacity - 1);
        SymEntry *next_slot = &m->slots[next];
        if (slot_empty(next_slot) || next_slot->dist == 0) break;
        m->slots[idx]       = *next_slot;
        m->slots[idx].dist--;
        next_slot->vars[0].name = SYMMAP_EMPTY;
        idx = next;
      }
      return;
    }
    dist++;
    idx = (idx + 1) & (m->capacity - 1);
  }
}
*/