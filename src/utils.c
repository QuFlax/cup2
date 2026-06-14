/**
 * @file utils.c
 * @brief Runtime utilities: allocator, logger, dynamic vectors, symbol hashmap
 */

#include "cup.h"
#include <threads.h>

#define INITIAL_CAPACITY (sizeof(size_t) * 4)

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
    exit(CUP_ERR_MEMORY);
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

void vector_push(CVector *vec, const void *data, size_t sizeT) {
  assert(sizeT > 0 && data != NULL);
  vector_resize(vec, vec->size + sizeT);
  memcpy((char *)vec->data + vec->size, data, sizeT);
  vec->size += sizeT;
}

void push_vector(CVector *dest, CVector src) {
  vector_push(dest, src.data, src.size);
  if (src.data) cup_free(src.data);
}

void vector_fpush(CVector *vec, const void *data, size_t sizeT) {
  assert(sizeT > 0 && data != NULL);
  vector_resize(vec, vec->size + sizeT);
  memmove((char *)vec->data + sizeT, vec->data, vec->size);
  memcpy(vec->data, data, sizeT);
  vec->size += sizeT;
}

void fpush_vector(CVector *dest, CVector src) {
  vector_fpush(dest, src.data, src.size);
  if (src.data) cup_free(src.data);
}

/* ========================================================================== */
/*                         SYMBOL HASH MAP                                    */
/* ========================================================================== */
/*
 * Open-addressing Robin Hood hash map.
 * Key:   var.name  (interned name offset; SYMMAP_EMPTY == free slot)
 * Value: CVariable stored inline inside SymEntry — no external array needed.
 *
 * Robin Hood: on collision we steal the slot from whichever entry has the
 * smaller probe distance ("richer" entry), keeping max probe length short.
 */
 
#define SYMMAP_INIT_CAPACITY 16   /* must be a power of two */
 
static size_t sym_hash(size_t key) {
  /* Fibonacci / multiplicative hashing for size_t keys */
  return key * (size_t)11400714819323198485ULL;
}
 
void symmap_init(SymbolMap *m) {
  m->count    = 0;
  m->capacity = SYMMAP_INIT_CAPACITY;
  cup_calloc(m->slots, sizeof(SymEntry) * m->capacity);
  for (size_t i = 0; i < m->capacity; i++)
    for (size_t j = 0; j < SYMMAP_VAR_DEPTH; j++)
      m->slots[i].vars[j].name = SYMMAP_EMPTY;
}
 
void symmap_free(SymbolMap *m) {
  cup_free(m->slots);
  m->slots    = NULL;
  m->count    = 0;
  m->capacity = 0;
}

static void symmap_insert_raw(SymEntry *slots, size_t cap, SymEntry entry, size_t depth) {
  size_t idx = sym_hash(entry.vars[depth].name) & (cap - 1);
  entry.dist = 0;
  for (;;) {
    SymEntry *slot = &slots[idx];
    if (slot_empty(slot)) {
      *slot = entry;
      return;
    }
    if (slot->dist < entry.dist) {
      SymEntry tmp = *slot;
      *slot  = entry;
      entry  = tmp;
    }
    entry.dist++;
    idx = (idx + 1) & (cap - 1);
  }
}
 
static void symmap_grow(SymbolMap *m) {
  size_t    new_cap   = m->capacity * 2;
  SymEntry *new_slots = (SymEntry *)cup_malloc(sizeof(SymEntry) * new_cap);
  for (size_t i = 0; i < new_cap; i++)
    for (size_t j = 0; j < SYMMAP_VAR_DEPTH; j++)
      new_slots[i].vars[j].name = SYMMAP_EMPTY;
  for (size_t i = 0; i < m->capacity; i++)
    if (!slot_empty(&m->slots[i]))
      symmap_insert_raw(new_slots, new_cap, m->slots[i], 0);
  cup_free(m->slots);
  m->slots    = new_slots;
  m->capacity = new_cap;
}
 
CVariable *symmap_put(SymbolMap *m, size_t name_idx,
                      const CUPType *type, size_t value, size_t depth) {
  /* Grow at 75 % load */
  if (m->count * SYMMAP_LOAD_DEN >= m->capacity * SYMMAP_LOAD_NUM)
    symmap_grow(m);
  size_t idx = sym_hash(name_idx) & (m->capacity - 1);
 
  SymEntry entry = { .dist = 0,
                     .vars  = { { .type = type, .name = name_idx, .value = value, .scope = depth } } };
 
  for (;;) {
    SymEntry *slot = &m->slots[idx];
 
    if (slot_empty(slot)) {
      *slot = entry;
      m->count++;
      return &slot->vars[0];
    }
    if (slot->vars[0].name == name_idx) {
      /* Update in place */
      slot->vars[0].type  = type;
      slot->vars[0].value = value;
      return &slot->vars[0];
    }
    if (slot->dist < entry.dist) {
      /* Robin Hood swap */
      SymEntry tmp = *slot;
      *slot  = entry;
      entry  = tmp;
    }
    entry.dist++;
    idx = (idx + 1) & (m->capacity - 1);
  }
}
 
CVariable *symmap_get(const SymbolMap *m, size_t name_idx) {
  if (!m->slots || m->capacity == 0) return NULL;
  size_t idx  = sym_hash(name_idx) & (m->capacity - 1);
  size_t dist = 0;
  for (;;) {
    SymEntry *slot = &m->slots[idx];
    /* Empty slot or entry closer than expected — key absent */
    if (slot_empty(slot) || slot->dist < dist) return NULL;
    if (slot->vars[0].name == name_idx)             return &slot->vars[0];
    dist++;
    idx = (idx + 1) & (m->capacity - 1);
  }
}
 
void symmap_del(SymbolMap *m, size_t name_idx) {
  if (!m->slots || m->capacity == 0) return;
  size_t idx  = sym_hash(name_idx) & (m->capacity - 1);
  size_t dist = 0;
  for (;;) {
    SymEntry *slot = &m->slots[idx];
    if (slot_empty(slot) || slot->dist < dist) return;
    if (slot->vars[0].name == name_idx) {
      /* Backward-shift deletion preserves Robin Hood invariant */
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
 
void symmap_iter(const SymbolMap *m, void *ctx,
                 void (*cb)(void *ctx, const CVariable *var)) {
  if (!m->slots) return;
  for (size_t i = 0; i < m->capacity; i++)
    if (!slot_empty(&m->slots[i]))
      cb(ctx, &m->slots[i].vars[0]);
}