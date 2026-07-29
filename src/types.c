#include "../include/cup.h"
#include <ctype.h>

#define CUPTypeD(T,N) (CUPType){sizeof(T), sizeof(T), N, {}};

CUPType cup_type_void = (CUPType){0, 0, CUP_TYPE_VOID, {}};
CUPType cup_type_int = CUPTypeD(size_t, CUP_TYPE_INT);
CUPType cup_type_float = CUPTypeD(float, CUP_TYPE_FLOAT);
CUPType cup_type_double = CUPTypeD(double, CUP_TYPE_DOUBLE);
CUPType cup_type_uint8 = CUPTypeD(uint8_t, CUP_TYPE_UINT8);
CUPType cup_type_sint8 = CUPTypeD(int8_t, CUP_TYPE_SINT8);
CUPType cup_type_uint16 = CUPTypeD(uint16_t, CUP_TYPE_UINT16);//{2, 2, CUP_TYPE_UINT16, {0}};
CUPType cup_type_sint16 = CUPTypeD(int16_t, CUP_TYPE_SINT16);//{2, 2, CUP_TYPE_SINT16, {0}};
CUPType cup_type_uint32 = CUPTypeD(uint32_t, CUP_TYPE_UINT32);//{4, 4, CUP_TYPE_UINT32, {0}};
CUPType cup_type_sint32 = CUPTypeD(int32_t, CUP_TYPE_SINT32);//{4, 4, CUP_TYPE_SINT32, {0}};
CUPType cup_type_uint64 = CUPTypeD(uint64_t, CUP_TYPE_UINT64);//{8, 8, CUP_TYPE_UINT64, {0}};
CUPType cup_type_sint64 = CUPTypeD(int64_t, CUP_TYPE_SINT64);//{, 8, CUP_TYPE_SINT64, {0}};
//CUPType cup_type_efunction = cup_type_function(&cup_type_void, &cup_type_void);
 
const CUPType* cup_type_get(CUPState *state, const CUPType type) {
  if (state == NULL) return CUP_ERR_CTYPE;
  if (memcmp(&cup_type_void, &type, sizeof(type)) == 0) return &cup_type_void;
  if (memcmp(&cup_type_int, &type, sizeof(type)) == 0) return &cup_type_int;
  if (memcmp(&cup_type_float, &type, sizeof(type)) == 0) return &cup_type_float;
  if (memcmp(&cup_type_double, &type, sizeof(type)) == 0) return &cup_type_double;
  if (memcmp(&cup_type_uint8, &type, sizeof(type)) == 0) return &cup_type_uint8;
  if (memcmp(&cup_type_sint8, &type, sizeof(type)) == 0) return &cup_type_sint8;
  if (memcmp(&cup_type_uint16, &type, sizeof(type)) == 0) return &cup_type_uint16;
  if (memcmp(&cup_type_sint16, &type, sizeof(type)) == 0) return &cup_type_sint16;
  if (memcmp(&cup_type_uint32, &type, sizeof(type)) == 0) return &cup_type_uint32;
  if (memcmp(&cup_type_sint32, &type, sizeof(type)) == 0) return &cup_type_sint32;
  if (memcmp(&cup_type_uint64, &type, sizeof(type)) == 0) return &cup_type_uint64;
  if (memcmp(&cup_type_sint64, &type, sizeof(type)) == 0) return &cup_type_sint64;
  if (type.realtype == CUP_TYPE_ARRAY)
    return CUP_ERR_CTYPE;
  size_t count = state->types.size / sizeof(CUPType*);
  for (size_t i = 0; i < count; i++) {
    if (memcmp(state->types.data[i], &type, sizeof(type)) == 0)
      return state->types.data[i];
  }
  return CUP_ERR_CTYPE;
}
const CUPType* cup_type_put(CUPState *state, const CUPType type) {
  if (state == NULL) return CUP_ERR_CTYPE;
  const CUPType* t = cup_type_get(state, type);
  if (t != CUP_ERR_CTYPE)
    return t;
  CUPType* newt = NULL;
  size_t i = 0;
  if (type.elements) {
    if (type.realtype == CUP_TYPE_FUNCTION) {
      i = 1;
      while (type.elements[i] != &cup_type_void) i++;
      /* Include return type, all parameter slots, and the void sentinel. */
      size_t temp_size = (i + 1) * sizeof(*newt->elements) + sizeof(CUPType);
      newt = (CUPType*)cup_malloc(temp_size);
      memset(newt, 0, temp_size);
      newt->elements = (const CUPType **)(newt + 1);
      for (size_t j = 0; j <= i; j++) {
        const CUPType *element = type.elements[j];
        if (element == NULL) {
          newt->elements[j] = NULL; /* unknown/any parameter */
        } else if (element == &cup_type_void) {
          newt->elements[j] = &cup_type_void; /* end sentinel */
        } else {
          newt->elements[j] = cup_type_put(state, *element);
        }
      }
    } else if (type.realtype == CUP_TYPE_GENERATOR) {
      size_t temp_size = (1) * sizeof(*newt->elements) + sizeof(CUPType);
      newt = (CUPType*)cup_malloc(temp_size);
      memset(newt, 0, temp_size);
      newt->gen = type.gen;
    } else if (type.realtype == CUP_TYPE_ARRAY) {
      size_t temp_size = (2) * sizeof(*newt->elements) + sizeof(CUPType);
      newt = (CUPType*)cup_malloc(temp_size);
      memset(newt, 0, temp_size);
      newt->elements = (const CUPType **)(newt + 1);
      newt->elements[i] = cup_type_put(state, *type.elements[i]);
    } else if (type.realtype == CUP_TYPE_POINTER) {
      size_t temp_size = (2) * sizeof(*newt->elements) + sizeof(CUPType);
      newt = (CUPType*)cup_malloc(temp_size);
      memset(newt, 0, temp_size);
      newt->elements = (const CUPType **)(newt + 1);
      newt->elements[i] = cup_type_put(state, *type.elements[i]);
    } else {
      char tname[256];
      cup_type_snname(tname, sizeof(tname), &type);
      cup_errorf("cup_type_put UNKNOWN %s", tname);
      exit(22);
    }
  } else {
    cup_calloc(newt, CUPType);
  }
  newt->realtype = type.realtype;
  newt->alignment = type.alignment;
  newt->size = type.size;
  vector_pushT(state->types.vec, newt);
  // assert(type.alignment != 0);
  // assert(type.size != 0);
  return newt;
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
    CUPType pt = cup_type_pointer(&cup_type_uint8);
    return cup_type_put(state, pt);
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
    CUPType pt = cup_type_pointer(base);
    base = cup_type_put(state, pt);
    (*p)++;
  }
  return base;
}

const CUPType* cup_type_parse(CUPState* state, const char* sig) {
  const char *p = sig;
  const CUPType* ret = parse_one_type(state, &p);
  if (*p == '(') {
    p++;
    const CUPType* args[UINT8_MAX] = {0};
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
    CUPType fn_t = {};
    fn_t.size = sizeof(void*);
    fn_t.alignment = (uint16_t)sizeof(void*);
    fn_t.realtype = CUP_TYPE_FUNCTION;
    fn_t.elements = args;
    return cup_type_put(state, fn_t);
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
  if (!type) {
    APPEND_LITERAL("null");
    return total;
  }
  switch (type->realtype) {
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
      total += cup_type_snname(s, maxlen, type->elements[0]);
      APPEND_LITERAL("*");
      break;
    case CUP_TYPE_ARRAY:
      total += cup_type_snname(s, maxlen, type->elements[0]);
      APPEND_LITERAL("[]");
      break;
    case CUP_TYPE_STRUCT:
      APPEND_LITERAL("struct{");
      if (type->elements) {
        for (size_t i = 0; type->elements[i]; ++i) {
          if (i != 0) APPEND_LITERAL(", ");
            total += cup_type_snname(
              s + total,
              maxlen - total,
              type->elements[i]);
        }
      }
      APPEND_LITERAL("}");
      break;
    case CUP_TYPE_FUNCTION: {
      const CUPType *ret = type->elements[0];
      total += cup_type_snname(s, maxlen, ret);
      APPEND_LITERAL("(");
      for (size_t i = 1; type->elements[i] != &cup_type_void; ++i) {
        if (i != 1) APPEND_LITERAL(", ");
        total += cup_type_snname(s + total, maxlen - total, type->elements[i]);
      }
      APPEND_LITERAL(")");
      break;
    }
    case CUP_TYPE_GENERATOR: {
      APPEND_LITERAL("generator ( ");
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