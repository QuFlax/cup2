#include "../include/cup.h"
#include <ctype.h>

#if 0
struct CUPType {
    uint8_t type;
    uint8_t alignment;
    uint16_t size;
    const CUPType **elements;
};
#endif

#define CUPTypeD(T,N) (CUPType){.stype = {N, (uint8_t)sizeof(T), (uint16_t)sizeof(T)}};

const CUPType cup_type_void = (CUPType){.stype = {0, CUP_TYPE_VOID, 0}};
const CUPType cup_type_int = CUPTypeD(size_t, CUP_TYPE_INT);
const CUPType cup_type_float = CUPTypeD(float, CUP_TYPE_FLOAT);
const CUPType cup_type_double = CUPTypeD(double, CUP_TYPE_DOUBLE);
const CUPType cup_type_uint8 = CUPTypeD(uint8_t, CUP_TYPE_UINT8);
const CUPType cup_type_sint8 = CUPTypeD(int8_t, CUP_TYPE_SINT8);
const CUPType cup_type_uint16 = CUPTypeD(uint16_t, CUP_TYPE_UINT16);//{2, 2, CUP_TYPE_UINT16, {0}};
const CUPType cup_type_sint16 = CUPTypeD(int16_t, CUP_TYPE_SINT16);//{2, 2, CUP_TYPE_SINT16, {0}};
const CUPType cup_type_uint32 = CUPTypeD(uint32_t, CUP_TYPE_UINT32);//{4, 4, CUP_TYPE_UINT32, {0}};
const CUPType cup_type_sint32 = CUPTypeD(int32_t, CUP_TYPE_SINT32);//{4, 4, CUP_TYPE_SINT32, {0}};
const CUPType cup_type_uint64 = CUPTypeD(uint64_t, CUP_TYPE_UINT64);//{8, 8, CUP_TYPE_UINT64, {0}};
const CUPType cup_type_sint64 = CUPTypeD(int64_t, CUP_TYPE_SINT64);//{, 8, CUP_TYPE_SINT64, {0}};
//CUPType cup_type_efunction = cup_type_function(&cup_type_void, &cup_type_void);
 
static const CUPType* cup_type_getEx(CUPState *state, CUP_TYPE type, uint16_t size, const void * data) {
  size_t count = state->types.size / sizeof(CUPType*);
  for (size_t i = 0; i < count; i++) {
    const CUPType* ptr = state->types.data[i];
    if (ptr->cuptype != type) continue;
    if (ptr->size != size) continue;
    if (ptr->data == data) return ptr;
    if (type == CUP_TYPE_FUNCTION) {
      if (memcmp(ptr->data, data, sizeof(CUPType*) + size * sizeof(CUPType*)))
        continue;
      return ptr;
    }
    if (type == CUP_TYPE_META) {
      if (ptr->data != data)
        continue;
      return ptr;
    }
    char tname[256];
    cup_type_snname(tname, sizeof(tname), ptr);
    cup_errorf("^ %s ^", tname);
    exit(1);
    //if (ptr->elements[0] == elements[0]) return ptr;
  }
  return NULL;
}

static const CUPType* cup_type_get(CUPState *state, CUP_TYPE type, uint16_t size, const void * data) {
  if (state == NULL) return NULL;
  switch (type) {
  default: assert(0);
  case CUP_TYPE_VOID: return &cup_type_void;
  case CUP_TYPE_INT: return &cup_type_int;
  case CUP_TYPE_FLOAT: return &cup_type_float;
  case CUP_TYPE_DOUBLE: return &cup_type_double;
  case CUP_TYPE_UINT8: return &cup_type_uint8;
  case CUP_TYPE_SINT8: return &cup_type_sint8;
  case CUP_TYPE_UINT16: return &cup_type_uint16;
  case CUP_TYPE_SINT16: return &cup_type_sint16;
  case CUP_TYPE_UINT32: return &cup_type_uint32;
  case CUP_TYPE_SINT32: return &cup_type_sint32;
  case CUP_TYPE_UINT64: return &cup_type_uint64;
  case CUP_TYPE_SINT64: return &cup_type_sint64;
  case CUP_TYPE_POINTER: 
  case CUP_TYPE_ARRAY:
  //case CUP_TYPE_GENERATOR:
  case CUP_TYPE_FUNCTION:
  case CUP_TYPE_META:
    return cup_type_getEx(state, type, size, data);
  }
}

#if 0
const CUPType* cup_typef(CUPState* state, size_t count, ...) {
  va_list args;
  va_start(args, count);
  const CUPType* r = cup_vtypef(state, count, args);
  va_end(args);
  return r;
}

const CUPType* cup_vtypef(CUPState* state, size_t count, va_list args) {
  size_t size = (count + 1) * sizeof(const CUPType *);
  const void ** elements = (const void **)cup_malloc(size);
  memset(elements, 0, size);
  elements[count] = &cup_type_void;
  for (size_t j = 0; j < count; j++) {
    const CUPType *element = va_arg(args, const CUPType *);
    if (element == NULL) continue; /* unknown/any parameter */
    if (element == (const CUPType *)&cup_type_void && j != 0) {
      assert(j != 0);
    } else {
      elements[j] = cup_type(state, element->type, element->size, (const void **)element->elements);
    }
  }
  const CUPType* r = cup_type(state, CUP_TYPE_FUNCTION, count, elements);
  cup_free(elements);
  return r;
}
#endif

//const CUPType* cup_typeGen(CUPGenContext* ctx, CUP_TYPE type, uint16_t size, const CUPType **elements) {
//  return cup_type(ctx->state, type, size, (void **)elements);
//}

const CUPType* cup_typemeta(CUPState* state, uint16_t size, CUP_Type_Meta gen) {
  return cup_type(state, CUP_TYPE_META, size, (const void *)gen);
}

static CUPType* newtype(CUPState* state, CUP_TYPE type, uint16_t size, const void* data) {
  //size_t temp_size = sizeof(CUPType); // 16
  switch (type) {
  case CUP_TYPE_META: {
    CUPTypeMeta* newt = (CUPTypeMeta*)cup_malloc(sizeof(*newt));
    //memset(newt, 0, temp_size);
    //newt->type = CUP_TYPE_META;
    newt->alignment = 0;
    newt->size = size;
    newt->gen = (CUP_Type_Meta)data;
    return (CUPType*)newt;
  }
  case CUP_TYPE_POINTER: {
    CUPTypeArr* newt = (CUPTypeArr*)cup_malloc(sizeof(*newt));
    //memset(newt, 0, temp_size);
    //newt->type = CUP_TYPE_POINTER;
    newt->alignment = __SIZEOF_POINTER__;
    newt->size = __SIZEOF_POINTER__;
    const CUPType* item = (const CUPType*)data;
    newt->element = cup_type(state, item->cuptype, item->size, (const void *)item->data);
    return (CUPType*)newt;
  }
  case CUP_TYPE_ARRAY: {
    CUPTypeArr* newt = (CUPTypeArr*)cup_malloc(sizeof(*newt));
    //memset(newt, 0, temp_size);
    //newt->type = CUP_TYPE_ARRAY;
    newt->alignment = 0;
    newt->size = size;
    const CUPType* item = (const CUPType*)data;
    newt->element = cup_type(state, item->cuptype, item->size, (const void *)item->data);
    return (CUPType*)newt;
  }
  case CUP_TYPE_FUNCTION: {
    /* Include return type, all parameter slots, and the void sentinel. */
    //cup_dbgf("Function - %zu / %zu", size, i);
    CUPTypeFunction* newt = (CUPTypeFunction*)cup_malloc(
      sizeof(CUPTypeFunction) + sizeof(CUPType*) + size * sizeof(CUPType*)
    );
    //memset(newt, 0, temp_size);
    //newt->type = CUP_TYPE_FUNCTION;
    newt->alignment = 0;
    newt->size = size;
    const CUPType ** ptrel = (const CUPType **)data;
    newt->returntype = *(ptrel++);
    newt->argstype[size] = &cup_type_void;
    for (size_t j = 0; j < size; j++) {
      const CUPType *it = ptrel[j];
      if (it == NULL) continue; /* unknown/any parameter */
      assert(it != &cup_type_void);
      newt->argstype[j] = cup_type(state, it->cuptype, it->size, (const void *)it->data);
    }
    return (CUPType*)newt;
  }
  default:
    cup_error("newtype UNKNOWN");
    assert(0);
    exit(22);
    break;
  }
}

const CUPType* cup_type(CUPState* state, CUP_TYPE type, uint16_t size, const void * data) {
  const CUPType* t = cup_type_get(state, type, size, data);
  if (t) return t;
  CUPType* newt = NULL;
  if (data) {
    newt = newtype(state, type, size, data);
  } else {
    cup_error("cup_type: data == NULL");
    exit(1);
  }
  newt->cuptype = type;
  vector_pushT(state->types.vec, newt);
  return newt;
    /*case CUP_TYPE_META: {
    assert(elements);
    size_t i = 0;
    while (elements[i] != &cup_type_void) i++;
    temp_size += (i + 1) * sizeof(*newt->elements);
    newt = (CUPType*)cup_malloc(temp_size);
    memset(newt, 0, temp_size);
    newt->size = size;
    size_t j = 0;
    for (; j < i; j++) {
      const CUPType *element = (const CUPType *)(elements[j]);
      if (element == NULL) continue;
      if (element == (const CUPType *)&cup_type_void) {
        assert(0);
      } else {
        newt->elements[j] = (void*)cup_type(state, element->type, element->size, (const void **)element->elements);
      }
    }
    if (elements[j] != &cup_type_void) {
      assert(0);
    }
    

  } break;
  */
}

const CUPType* parse_one_type(CUPState* state, const char **p) {
  const char *start = *p;
  while (isalnum((unsigned char)**p)) (*p)++;
  size_t len = *p - start;
  const CUPType* base = NULL;

  if      (len == 1 && start[0] == 'v') base = &cup_type_void;
  else if (len == 1 && start[0] == 'i') base = &cup_type_int;
  else if (len == 1 && start[0] == 'f') base = &cup_type_float;
  else if (len == 1 && start[0] == 'd') base = &cup_type_double;
  else if (len == 1 && start[0] == 'p') {
    return cup_type(state, CUP_TYPE_POINTER, __SIZEOF_POINTER__, (const void *[]){&cup_type_uint8});
  }
  else if (len == 2 && !memcmp(start,"u8",2))  base = &cup_type_uint8;
  else if (len == 2 && !memcmp(start,"s8",2))  base = &cup_type_sint8;
  else if (len == 3 && !memcmp(start,"u16",3)) base = &cup_type_uint16;
  else if (len == 3 && !memcmp(start,"s16",3)) base = &cup_type_sint16;
  else if (len == 3 && !memcmp(start,"u32",3)) base = &cup_type_uint32;
  else if (len == 3 && !memcmp(start,"s32",3)) base = &cup_type_sint32;
  else if (len == 3 && !memcmp(start,"u64",3)) base = &cup_type_uint64;
  else if (len == 3 && !memcmp(start,"s64",3)) base = &cup_type_sint64;
  else {
    cup_errorf("external: unknown type code '%.*s'", (int)len, start);
    return NULL;
    // exit(1);
  }

  while (**p == '*') {
    base = cup_type(state, CUP_TYPE_POINTER, __SIZEOF_POINTER__, (const void*[]){base});
    (*p)++;
  }
  return base;
}

const CUPType* cup_type_parse(CUPState* state, const char* sig) {
  const char *p = sig;
  const CUPType* ret = parse_one_type(state, &p);
  if (*p == '(') {
    p++;
    const void* args[UINT8_MAX] = {0};
    size_t argc = 0;
    if (*p != ')') {
      for (;;) {
        if (argc >= UINT8_MAX - 1) {
          cup_error("external: too many arguments in signature");
          exit(1);
        }
        if ((args[argc++] = parse_one_type(state, &p)) == NULL) {
          cup_errorf("external: expected ')' in signature '%s'", sig);
          exit(1);
        }
        if (*p == ',') { p++; continue; }
        if (*p == ')') break;
      }
    }
    // const CUPType** elements = (const CUPType**)cup_malloc(sizeof(CUPType*) * (argc + 2));
    memmove(&args[1], &args[0], sizeof(*args) * argc);
    args[argc + 1] = &cup_type_void;
    args[0] = ret;
    return cup_type(state, CUP_TYPE_FUNCTION, argc + 1, (const void **)args);
  } else {
    if (*p == '\0') return ret;
    cup_errorf("external: expected 'END' in signature '%s'", sig);
    exit(1);
  }
  return ret;
}

// Append a literal string (compile-time length) to the context
#define APPEND_LITERAL(lit) do { \
    static const char str[] = lit; \
    size_t len = sizeof(str) - 1;   /* exclude null terminator */ \
    if (total < maxlen) { \
        size_t copy = len < (maxlen - total) ? len : (maxlen - total) - 1; \
        memcpy(s + total, str, copy + 1); \
        total += copy; \
    } \
} while (0)

size_t cup_type_snname(char *restrict s, size_t maxlen, const CUPType *type) {
  assert(s != NULL);
  size_t total = 0;
  if (type == NULL) {
    APPEND_LITERAL("null");
    return total;
  }
  switch (type->cuptype) {
    case CUP_TYPE_VOID:    APPEND_LITERAL("void"); break;
    case CUP_TYPE_INT:     APPEND_LITERAL("int"); break;
    case CUP_TYPE_FLOAT:   APPEND_LITERAL("float"); break;
    case CUP_TYPE_DOUBLE:  APPEND_LITERAL("double"); break;
    case CUP_TYPE_UINT8:   APPEND_LITERAL("uint8"); break;
    case CUP_TYPE_SINT8:   APPEND_LITERAL("sint8"); break;
    case CUP_TYPE_UINT16:  APPEND_LITERAL("uint16"); break;
    case CUP_TYPE_SINT16:  APPEND_LITERAL("sint16"); break;
    case CUP_TYPE_UINT32:  APPEND_LITERAL("uint32"); break;
    case CUP_TYPE_SINT32:  APPEND_LITERAL("sint32"); break;
    case CUP_TYPE_UINT64:  APPEND_LITERAL("uint64"); break;
    case CUP_TYPE_SINT64:  APPEND_LITERAL("sint64"); break;
    case CUP_TYPE_POINTER:
      total += cup_type_snname(s, maxlen, type->arrtype.element);
      APPEND_LITERAL("*");
      break;
    case CUP_TYPE_ARRAY:
      total += cup_type_snname(s, maxlen, type->arrtype.element);
      APPEND_LITERAL("[]");
      break;
    case CUP_TYPE_FUNCTION: {
      const CUPTypeFunction fnt = type->functiontype;
      total += cup_type_snname(s, maxlen, fnt.returntype);
      APPEND_LITERAL("(");
      for (size_t i = 0; fnt.argstype[i] != &cup_type_void; ++i) {
        if (i) APPEND_LITERAL(", ");
        total += cup_type_snname(s + total, maxlen - total, fnt.argstype[i]);
      }
      APPEND_LITERAL(")");
      break;
    }
    case CUP_TYPE_META: {
      APPEND_LITERAL("meta ( ");
      char* str = cup_sprintf("%zu", type->size);
      size_t len = strlen(str);
      if (total < maxlen) {
        size_t copy = len < (maxlen - total) ? len : (maxlen - total) - 1;
        memcpy(s + total, str, copy + 1);
        total += copy;
      }
      cup_free(str);
      APPEND_LITERAL(" )");
      break;
    }
    case CUP_TYPE_COUNT:    APPEND_LITERAL("count"); break;
    default:
      APPEND_LITERAL("unknown");
      break;
  }
  if (total < maxlen)
    s[total] = '\0';
  return total;  
}