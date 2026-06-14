#include "../include/cup.h"

#define CUPTypeD(T,N) (CUPType){sizeof(T), sizeof(T), N, 0};

CUPType cup_type_void = (CUPType){0, 0, CUP_TYPE_VOID, 0};
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

 
const CUPType* cup_type_get(CUPState *state, const CUPType* type) {
  if (state == NULL) return CUP_ERR_CTYPE;
  if (memcmp(&cup_type_void, type, sizeof(*type)) == 0) return &cup_type_void;
  if (memcmp(&cup_type_int, type, sizeof(*type)) == 0) return &cup_type_int;
  if (memcmp(&cup_type_float, type, sizeof(*type)) == 0) return &cup_type_float;
  if (memcmp(&cup_type_double, type, sizeof(*type)) == 0) return &cup_type_double;
  if (memcmp(&cup_type_uint8, type, sizeof(*type)) == 0) return &cup_type_uint8;
  if (memcmp(&cup_type_sint8, type, sizeof(*type)) == 0) return &cup_type_sint8;
  if (memcmp(&cup_type_uint16, type, sizeof(*type)) == 0) return &cup_type_uint16;
  if (memcmp(&cup_type_sint16, type, sizeof(*type)) == 0) return &cup_type_sint16;
  if (memcmp(&cup_type_uint32, type, sizeof(*type)) == 0) return &cup_type_uint32;
  if (memcmp(&cup_type_sint32, type, sizeof(*type)) == 0) return &cup_type_sint32;
  if (memcmp(&cup_type_uint64, type, sizeof(*type)) == 0) return &cup_type_uint64;
  if (memcmp(&cup_type_sint64, type, sizeof(*type)) == 0) return &cup_type_sint64;
  size_t count = state->types.size / sizeof(CUPType*);
  for (size_t i = 0; i < count; i++) {
    if (memcmp(state->types.data[i], type, sizeof(*type)) == 0)
      return state->types.data[i];
  }
  return CUP_ERR_CTYPE;
}
const CUPType* cup_type_put(CUPState *state, const CUPType* type) {
  if (state == NULL) return CUP_ERR_CTYPE;
  const CUPType* t = cup_type_get(state, type);
  if (t != CUP_ERR_CTYPE)
    return t;
  CUPType* newt = NULL;
  size_t i = 0;
  if (type->elements) {
    if (type->realtype == CUP_TYPE_FUNCTION)
      i = 1;
    while (type->elements[i]) i++;
    size_t temp_size = (i + 1) * sizeof(*newt->elements) + sizeof(CUPType);
    newt = (CUPType*)cup_malloc(temp_size);
    memset(newt, 0, temp_size);
    newt->elements = (const CUPType **)(newt + 1);
    while (i--) {
      if (type->elements[i] == NULL)
        continue;
      newt->elements[i] = cup_type_put(state, type->elements[i]);
    }
    //for (i = 0; type.elements[i]; i++)
    //  newt->elements[i] = cup_type_put(state, *(type.elements[i]));
  } else {
    cup_alloc(newt, CUPType);
  }
  newt->realtype = type->realtype;
  newt->alignment = type->alignment;
  newt->size = type->size;
  vector_pushT(state->types.vec, newt);
  assert(type->alignment != 0);
  assert(type->size != 0);
  return newt;
}

// Append a literal string (compile-time length) to the context
#define APPEND_LITERAL(lit) do { \
    static const char str[] = lit; \
    size_t len = sizeof(str) - 1;   /* exclude null terminator */ \
    if (total < maxlen) { \
        size_t copy = len < (maxlen - total) ? len : (maxlen - total) - 1; \
        memcpy(s + total, str, copy); \
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
      const CUPType *args = type->elements[1];
      total += cup_type_snname(s, maxlen, ret);
      APPEND_LITERAL("(");
      if (args) {
        for (size_t i = 1; type->elements[i]; ++i) {
          if (i != 1) APPEND_LITERAL(", ");
            total += cup_type_snname(s + total, maxlen - total, type->elements[i]);
        }
      }
      APPEND_LITERAL(")");
      break;
    }
    default:
      APPEND_LITERAL("unknown");
      break;
  }
  if (total < maxlen)
    s[total] = '\0';
  return total;  
}

size_t _cup_type_snname(char *restrict s, size_t maxlen, const CUPType *type) {
  if (!s || maxlen == 0)
    return 0;
#define APPEND_FMT(...) \
  do { \
    if (written < maxlen) { \
      int n = snprintf(s + written, maxlen - written, __VA_ARGS__); \
      if (n < 0) \
        return written; \
      written += (size_t)n; \
    } \
  } while (0)

  size_t written = 0;
  if (!type) {
    APPEND_FMT("null");
    goto end;
  }
  switch (type->realtype) {
        case CUP_TYPE_VOID:
            APPEND_FMT("void");
            break;

        case CUP_TYPE_INT:
            APPEND_FMT("int");
            break;

        case CUP_TYPE_FLOAT:
            APPEND_FMT("float");
            break;

        case CUP_TYPE_DOUBLE:
            APPEND_FMT("double");
            break;

        case CUP_TYPE_UINT8:
            APPEND_FMT("uint8");
            break;

        case CUP_TYPE_SINT8:
            APPEND_FMT("sint8");
            break;

        case CUP_TYPE_UINT16:
            APPEND_FMT("uint16");
            break;

        case CUP_TYPE_SINT16:
            APPEND_FMT("sint16");
            break;

        case CUP_TYPE_UINT32:
            APPEND_FMT("uint32");
            break;

        case CUP_TYPE_SINT32:
            APPEND_FMT("sint32");
            break;

        case CUP_TYPE_UINT64:
            APPEND_FMT("uint64");
            break;

        case CUP_TYPE_SINT64:
            APPEND_FMT("sint64");
            break;

        case CUP_TYPE_POINTER: {
            const CUPType *base = type->elements[0];
            written += cup_type_snname(
                s + (written < maxlen ? written : maxlen),
                written < maxlen ? maxlen - written : 0,
                base
            );
            APPEND_FMT("*");
            break;
        }

        case CUP_TYPE_ARRAY: {
            const CUPType *base = type->elements[0];
            written += cup_type_snname(
                s + (written < maxlen ? written : maxlen),
                written < maxlen ? maxlen - written : 0,
                base
            );
            APPEND_FMT("[]");
            break;
        }

        case CUP_TYPE_STRUCT: {
            APPEND_FMT("struct{");

            if (type->elements) {
                for (size_t i = 0; type->elements[i]; ++i) {
                    if (i != 0)
                        APPEND_FMT(", ");

                    written += cup_type_snname(
                        s + (written < maxlen ? written : maxlen),
                        written < maxlen ? maxlen - written : 0,
                        type->elements[i]
                    );
                }
            }

            APPEND_FMT("}");
            break;
        }

        case CUP_TYPE_FUNCTION: {
            const CUPType *ret = type->elements[0];
            const CUPType *args = type->elements[1];

            written += cup_type_snname(
                s + (written < maxlen ? written : maxlen),
                written < maxlen ? maxlen - written : 0,
                ret
            );

            APPEND_FMT("(");

            if (args) {
                for (size_t i = 1; type->elements[i]; ++i) {
                    if (i != 1)
                        APPEND_FMT(", ");

                    written += cup_type_snname(
                        s + (written < maxlen ? written : maxlen),
                        written < maxlen ? maxlen - written : 0,
                        type->elements[i]
                    );
                }
            }

            APPEND_FMT(")");
            break;
        }

        default:
            APPEND_FMT("unknown");
            break;
    }
end:
    if (maxlen > 0) {
        if (written >= maxlen)
            s[maxlen - 1] = '\0';
        else
            s[written] = '\0';
    }

    return written;

#undef APPEND_FMT
}
