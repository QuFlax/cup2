#include "cup.h"
#include <threads.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define INITIAL_CAPACITY (sizeof(size_t) * 4)
#define LOAD_FACTOR 0.75

thread_local static CUP_Realloc_Fn cup_realloc_fn;
thread_local static CUP_Log_Fn cup_log_fn;

const char* CFE = "format error";

static void default_log(CUP_LogLevel level, const char *msg) {
  switch (level) {
  case CUP_Level_INFO:
    fputs("INFO: ", stderr);
    break;
  case CUP_Level_WARN:
    fputs("WARN: ", stderr);
    break;
  case CUP_Level_ERRO:
    fputs("ERROR: ", stderr);
    break;
  }
  fputs(msg, stderr);
  fputc('\n', stderr);
}
static void *default_reallocator(void *ptr, size_t size) {
  if (size != 0) {
    void *p = realloc(ptr, size);
    if (p)
      return p;
    default_log(CUP_Level_ERRO, "memory full for reallocator");
    exit(CUP_ERR_MOMORY);
  }
  free(ptr);
  return NULL;
}

CUP_Realloc_Fn* _cup_realloc(void) {
  if (!cup_realloc_fn)
    cup_realloc_fn = default_reallocator;
  return &cup_realloc_fn;
}
CUP_Log_Fn* _cup_log(void) {
  if (!cup_log_fn)
    cup_log_fn = default_log;
  return &cup_log_fn;
}

void cup_set_realloc(CUP_Realloc_Fn realloc_func) {
  *_cup_realloc() = realloc_func;
}
void cup_set_error_func(CUP_Log_Fn error_func) {
  *_cup_log() = error_func;
}

const char *cup_strdup(const char *str) {
  size_t len = strlen(str);
  char* s = malloc(len+1);
  memcpy(s,str,len);
  s[len] = '\0';
  return s;
}
const char *cup_sprintf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  char* r = cup_vsprintf(format, args);
  va_end(args);
  return r;
}
const char *cup_vsprintf(const char *format, va_list args) {
  int len, size = INITIAL_CAPACITY;
  char *buffer = (char*)malloc(size);
  while (1) {
    va_list args_copy;
    va_copy(args_copy, args);
    len = vsnprintf(buffer, size, format, args_copy);
    va_end(args_copy);
    if (len >= size) {
      size = len;
      buffer = cup_realloc(buffer, ++size);
      continue;
    }
    if (len < 0) { // Encoding error
      free(buffer);
      cup_log(CUP_Level_ERRO, CFE);
      return cup_strdup(CFE);
    }
    return buffer;
  }
}

static void vector_resize(CVector *vec, size_t newsize) {
  if (vec == NULL || newsize == 0)
    return;
  if (vec->capacity < newsize) {
    do {
      vec->capacity = vec->capacity ? vec->capacity * 2 : INITIAL_CAPACITY;
    } while (vec->capacity < newsize);
    vec->data = cup_realloc(vec->data, vec->capacity);
  }
}
void vector_push(CVector *vec, const void *data, size_t sizeT) {
  assert(sizeT > 0);
  assert(data != NULL);
  vec->size += sizeT;
  vector_resize(vec, vec->size);
  memcpy((char *)vec->data + vec->size - sizeT, data, sizeT);
}
void push_vector(CVector *dest, CVector src, size_t sizeT) {
  vector_push(dest, src.data, src.size);
  if (src.data)
    free(src.data);
}
void vector_fpush(CVector *vec, const void *data, size_t sizeT) {
  assert(sizeT > 0);
  assert(data != NULL);
#if 1
  vector_res(&vec, vec->size + sizeT);
  memmove(vec->data + sizeT, vec->data, vec->size);
  memcpy(vec->data         , data,      sizeT);
#else
  CVector new_vector = {};
  vector_res(&new_vector, sizeT + vec->size);
  memcpy((char *)new_vector.data,            data,      sizeT);
  memcpy((char *)new_vector.data + sizeT, vec->data, vec->size);
  if (vec->data)
    free(vec->data);
  memcpy(vec, &new_vector, sizeof(CVector));
#endif
}
void fpush_vector(CVector *dest, CVector src, size_t sizeT) {
  vector_fpush(dest, src.data, src.size);
  if (src.data)
    free(src.data);
}

void hashmap_resize(VectorMap *map, size_t sizeT) {
#if 1
  size_t count           = map->capacity / sizeT;
  map->capacity          = map->capacity * 2;
  map->data              = cup_realloc(map->data, map->capacity);
  VectorMapKey *new_keys;
  calloc(new_keys, count * (2 * sizeof(VectorMapKey)));
  for (size_t i = 0; i < count; i++) {
    if (map->keys[i].key) {
      hash_t h = hash(map->keys[i].key) % (count * 2);
      while (new_keys[h].key)
        h = (h + 1) % (count * 2);
      new_keys[h] = map->keys[i];
    }
  }
  free(map->keys);
  map->keys     = new_keys;
#endif
}
VectorMapKey* hashmap_get_keyslot(VectorMap map, const char *key, size_t sizeT) {
  if (!map.data || map.count == 0)
    return NULL;
  size_t count    = map.capacity / sizeT;
  size_t i = hash(key) % count, end = i;
  do {
    if (!map.keys[i].key)
      return &map.keys[i];
    if (strcmp(map.keys[i].key, key) == 0)
      return &map.keys[i];
    i = (i + 1) % count;
  } while (i != end);
  return NULL;
}
void* hashmap_get(VectorMap map, const char *key, size_t sizeT) {
  if (!map.data || map.capacity == 0 || !key)
    return NULL;
  VectorMapKey* k = hashmap_get_keyslot(map, key, sizeT);
  if (k) 
    return (char *)map.data + k->offset;
  return NULL;
}
void* hashmap_put(VectorMap *map, const char *key, const void* value, size_t sizeT) {
  if (!map || !key)
    return NULL;
  if (!map->data || map->capacity == 0) {
    map->capacity = INITIAL_CAPACITY * sizeT;
    map->count    = 0;
    map->data     = malloc(map->capacity);
    calloc(map->keys, INITIAL_CAPACITY * sizeof(VectorMapKey));
  }
  if (((double)(map->count + 1) * sizeT / (double)map->capacity) > LOAD_FACTOR) {
    hashmap_resize(map, sizeT);
  }
  VectorMapKey* v = hashmap_get_keyslot(*map, key, sizeT);
  if (!v)
    return NULL;
  if (v->key) {
    v->key      = cup_strdup(key);
    v->offset   = sizeT * map->count++;
  }
  memcpy((char *)map->data + v->offset, value, sizeT);
  return v;
}
void hashmap_free(VectorMap *map) {
  if (!map) return;
  free(map->data);
  free(map->keys);
  map->data     = NULL;
  map->keys     = NULL;
  map->count    = 0;
  map->capacity = 0;
}