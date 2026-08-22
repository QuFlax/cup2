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

/*
#define TEMP_CAP 2048

thread_local static size_t temppos = 0;
thread_local static uint8_t tempmem[TEMP_CAP + TEMP_CAP] = {};

void* talloc(size_t size) {
  size_t pos = size + temppos;
  if (pos > TEMP_CAP) {
    cup_error("MAX");
    exit(1);
  }
  return tempmem + temppos;
}

void treset(size_t size) {
  temppos = size;
}
*/

void emit_error(CUPModule *buf, size_t size) {
  size_t capacity = PAGESIZE;
  if (buf == NULL)
  {
    cup_error("emit_error: buf is NULL");
    exit(1);
  }
  if (buf->data == NULL)
    buf->data = (uint8_t *)cup_malloc(capacity);
  buf->size += size;
  if (buf->size > capacity)
  {
    do
    {
      capacity += PAGESIZE;
    } while (buf->size > capacity);
    buf->data = (uint8_t *)cup_realloc(buf->data, capacity);
  }
}
void emit(CUPModule *buf, const void *value, size_t size) {
  if (buf == NULL)
    return;
  size_t offset = buf->size;
  emit_error(buf, size);
  memcpy(buf->data + offset, value, size);
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

void vector_fpop(CVector *vec, size_t sizeT) {
  assert(vec && sizeT > 0);
  if (vec->size > sizeT) {
    memmove(vec->data,
      (char *)vec->data + sizeT,
      vec->size - sizeT);
    vec->size -= sizeT;
  } else {
    cup_free(vec->data);
    vec->size = 0;
  }
}

void vector_push(CVector *vec, const void *data, size_t sizeT) {
  assert(sizeT > 0 && data != NULL);
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

CVector split_vector(CVector *vec, size_t index) {
  assert(vec);
  if (index >= vec->size) {
    CVector empty = {};
    return empty;
  }
  CVector out = {};
  out.size = vec->size - index;
  out.capacity = out.size;
  out.data = cup_malloc(out.size);
  memcpy(out.data, (char *)vec->data + index, out.size);
  vec->size = index;
  return out;
}

/* ========================================================================== */
/*                         SYMBOL HASH MAP                                    */
/* ========================================================================== */

/* TODO: change to true
 * Open-addressing Robin Hood hash map.
 * Key:   var.name  (interned name offset; SYMMAP_EMPTY == free slot)
 * Value: CVariable stored inline inside SymEntry — no external array needed.
 *
 * Robin Hood: on collision we steal the slot from whichever entry has the
 * smaller probe distance ("richer" entry), keeping max probe length short.
 */

#define CMAP_INIT_CAPACITY 16   /* must be a power of two */

size_t map_hash(size_t key) {
  /* Fibonacci / multiplicative hashing for size_t keys */
  return key * (size_t)11400714819323198485ULL;
}

static inline void map_set(size_t *slot, size_t key, size_t dist, const void *value, size_t v_size) {
  slot[1] = dist;
  *slot = key;
  memcpy((void*)(slot + 2), value, v_size);
}

void map_init(CMap *m, size_t k_null, size_t v_size, void* v_default) {
  m->count    = 0;
  m->capacity = CMAP_INIT_CAPACITY;
  //m->v_size   = v_size;
  //m->k_null   = k_null;
  //m->hash     = hash_fn;
  size_t kdv_size = sizeof(size_t) + sizeof(size_t) + v_size;
  assert(sizeof(size_t) * 32 >= kdv_size);
  assert(sizeof(size_t) <= v_size);
  m->slots = cup_malloc(kdv_size * m->capacity);
  for (size_t i = 0; i < m->capacity; i++)
    map_set(m->slots + i * kdv_size, k_null, SIZE_MAX, v_default, v_size);
/*
  if (v_default)
    for (size_t i = 0; i < m->capacity; i++) {
      size_t *slot = m->slots + i * kdv_size;
      map_set(slot, k_null, 0, v_default, v_size);
    }
  else
    memset(m->slots, 0, kdv_size * m->capacity);
*/
}
void map_free(CMap *m) {
  if (m == NULL) return;
  cup_free(m->slots);
  m->slots = NULL;
  m->count = 0;
  m->capacity = 0;
}
static void cmap_grow(CMap *m, size_t v_size, size_t k_null) {
  size_t    new_cap   = m->capacity * 2;
  size_t kdv_size = sizeof(size_t) + sizeof(size_t) + v_size;
  void *new_slots = cup_malloc(kdv_size * new_cap);
#if 1
  size_t* kdv_default = NULL;
  for (size_t i = 0; !kdv_default && i < m->capacity; i++) {
    size_t *slot = m->slots + i * kdv_size;
    if (*slot == k_null) kdv_default = slot;
  }
  for (size_t i = 0; i < new_cap; i++)
    memcpy(new_slots + i * kdv_size, kdv_default, kdv_size);
#else

    for (size_t i = 0; i < new_cap; i++) {
    memcpy(new_slots + i * (sizeof(int32_t) + m->kv_size), kv_null, m->kv_size);
  }
#endif
  void* old_slots = m->slots;
  size_t old_cap = m->capacity;
  m->slots    = new_slots;
  m->capacity = new_cap;

  for (size_t i = 0; i < old_cap; i++) {
    size_t *slot = old_slots + i * kdv_size;
    if (*slot == k_null) continue;
    map_put(m, *slot, (void*)(slot + 2), v_size, k_null);
  }
  cup_free(old_slots);
}
void *map_get(const CMap *m, size_t key, size_t v_size, size_t k_null) {
  if (!m->slots || m->capacity == 0) return NULL;
  size_t i  = map_hash(key) & (m->capacity - 1);
  size_t kdv_size = sizeof(size_t) + sizeof(size_t) + v_size;
  size_t dist = 0;
  for (;;) {
    size_t *slot = m->slots + i * kdv_size;
    if (*slot == k_null || slot[1] < dist) return NULL;
    if (*slot == key) return (void*)(slot + 2);
    dist++;
    i = (i + 1) & (m->capacity - 1);
  }
}
void *map_put(CMap *m, size_t key, void *value, size_t v_size, size_t k_null) {
  if (m->count * CMAP_LOAD_DEN >= m->capacity * CMAP_LOAD_NUM)
    cmap_grow(m, v_size, k_null);
  size_t i = map_hash(key) & (m->capacity - 1);
  size_t kdv_size = sizeof(size_t) + sizeof(size_t) + v_size;
  size_t dist = 0;
  for (;;) {
    size_t *slot = m->slots + i * kdv_size;
    if (*slot == k_null) {
      map_set(slot, key, dist, value, v_size);
      m->count++;
      return (void*)(slot + 2);
    }
    if (*slot == key) {
      memcpy((void*)(slot + 2),
             value, v_size);
      return (void*)(slot + 2);
    }
    if (slot[1] < dist) {
      /* Robin Hood swap */
      uint8_t tmp[sizeof(size_t) * 32];
      memcpy(tmp, slot, kdv_size); //SymEntry tmp = *slot;
      map_set(slot, key, dist, value, v_size);
      //*slot  = entry;
      dist = ((size_t*)tmp)[1];
      key = *((size_t*)tmp);
      memcpy(value, (void*)tmp + sizeof(size_t) + sizeof(size_t), v_size);
      //entry  = tmp;
    }
    dist++;
    i = (i + 1) & (m->capacity - 1);
  }
}


void map_iter(const CMap *m, void *ctx, map_iter_callback cb, size_t v_size, size_t k_null) {
  if (!m || !m->slots) return;
  size_t kdv_size = sizeof(size_t) + sizeof(size_t) + v_size;
  for (size_t i = 0; i < m->capacity; i++) {
    size_t *slot = m->slots + i * kdv_size;
    if (*slot == k_null) continue;
    cb(ctx, *slot, slot[1], (void*)(slot + 2));
  }
}

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
 
void symmap_iter(const SymbolMap *m, void *ctx,
                 void (*cb)(void *ctx, const CVariable *var)) {
  if (!m->slots) return;
  for (size_t i = 0; i < m->capacity; i++)
    if (!slot_empty(&m->slots[i]))
      cb(ctx, &m->slots[i].vars[0]);
}
*/