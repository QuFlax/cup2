#include "../include/cup.h"
#include <threads.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define INITIAL_CAPACITY 16
#define LOAD_FACTOR 0.75
#define ALLOCSIZETEMP 64

thread_local static CUP_Realloc_Fn cup_realloc_fn;
thread_local static CUP_Log_Fn cup_log_fn;

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

const char *cup_sprintf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  char* r = cup_vsprintf(format, args);
  va_end(args);
  return r;
}
const char *cup_vsprintf(const char *format, va_list args) {
  int len, size = ALLOCSIZETEMP;
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
      cup_log(CUP_Level_ERRO, "format error");
      return "format error";
    }
    return buffer;
  }
}

void vector_push(CVector *vec, const void *data, size_t size){
  if (size == 0 || data == NULL)
    return;
  size_t newsize = vec->size + size;
  if (vec->capacity < newsize) {
    do {
      vec->capacity = vec->capacity ? vec->capacity * 2 : 8;
    } while (vec->capacity < newsize);
    vec->data = cup_realloc(vec->data, vec->capacity);
  }
  memcpy((char *)vec->data + vec->size, data, size);
  vec->size += size;
}
void push_vector(CVector *dest, CVector src) {
  vector_push(dest, src.data, src.size);
  if (src.data)
    free(src.data);
}
void vector_fpush(CVector *vec, const void *data, size_t size) {
  CVector new_vector = {};
  vector_push(&new_vector, data, size);
  push_vector(&new_vector, *vec);
  memcpy(vec, &new_vector, sizeof(CVector));
}
void fpush_vector(CVector *dest, CVector src) {
  vector_fpush(dest, src.data, src.size);
  if (src.data)
    free(src.data);
}
void hashmap_resize(VectorMap *map) {
  size_t old_capacity = map->capacity;
  Entry **old_buckets = map->buckets;
  map->capacity *= 2;
  map->buckets = cup_realloc(map->buckets, map->capacity * sizeof(Entry*));
  memset(&map->buckets[old_capacity], 0, old_capacity);  
  // Rehash all entries
  Entry *entries_to_rehash[map->size];
  size_t entry_count = 0;
    
  // Collect all entries
  for (size_t i = 0; i < old_capacity; i++) {
    Entry *entry = map->buckets[i];
    map->buckets[i] = NULL;  // Clear old bucket
    while (entry) {
      Entry *next = entry->next;
      entries_to_rehash[entry_count++] = entry;
      entry = next;
    }
  }
    
  // Reinsert all entries
  for (size_t i = 0; i < entry_count; i++) {
    Entry *entry = entries_to_rehash[i];
    size_t index = hash(entry->key) % map->capacity;
    entry->next = map->buckets[index];
    map->buckets[index] = entry;
  }
}
void* hashmap_get(HashMap map, const char *key) {
  size_t index = hash(key) % map.capacity;
  for (Entry *entry = map.buckets[index]; entry; entry = entry->next) {
    if (entry->key == key || (strcmp(entry->key, key) == 0))
      return entry->value;
  }
  return CUP_ERR_CTYPE;
}
void hashmap_put(HashMap *map, const char *key, void* value) {
  if ((float)map->size / map->capacity > LOAD_FACTOR)
    hashmap_resize(map);
  size_t index = hash(key) % map->capacity;
  CUPType* e = hashmap_get(*map, key);
  if (e) return;
  Entry *new_entry = malloc(sizeof(Entry));
  size_t len = strlen(key);
  new_entry->key = malloc(len + 1);
  memcpy(new_entry->key, key, len);
  new_entry->value = value;
  new_entry->next = map->buckets[index];
  map->buckets[index] = new_entry;
  map->size++;
}
void hashmap_destroy(HashMap *map) {
  for (size_t i = 0; i < map->capacity; i++) {
    for (Entry *entry = map->buckets[i]; entry; ) {
      Entry *next = entry->next;
      free(entry->key);
      free(entry);
      entry = next;
    }
  }
  free(map->buckets);
  free(map);
}