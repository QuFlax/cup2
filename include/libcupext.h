#ifndef LIBCUP_H
#error "Include libcup.h before libcupext.h"
#endif

#ifndef LIBCUP_EXT_H
#define LIBCUP_EXT_H

#include <assert.h>
#include <stdarg.h>

/* ========================================================================== */
/*                           MEMORY HELPERS                                   */
/* ========================================================================== */

/*
 * Do NOT redefine malloc/free/calloc as macros — it breaks system headers
 * included after this file and makes calloc unusable as an expression.
 * Use the cup_* prefixed wrappers everywhere inside the compiler.
 */
#define cup_free(x)          cup_realloc((x), 0)
#define cup_malloc(x)        cup_realloc(NULL, (x))
#define cup_calloc(name, T) \
    do { name = (T*)cup_malloc(sizeof(T)); memset(name, 0, sizeof(T)); } while (0)

char *cup_vsprintf(const char *format, va_list args);
char *cup_sprintf(const char *format, ...);
char *cup_strdup(const char *str);

void* talloc(size_t size);
void treset(size_t size);

/* ========================================================================== */
/*                           LOGGING HELPERS                                  */
/* ========================================================================== */

#define cup_warnf(f, ...) \
    do { \
        char *_msg = cup_sprintf((f), __VA_ARGS__); \
        cup_log(CUP_Level_WARN, _msg); \
        cup_free(_msg); \
    } while (0)

#define cup_warn(msg)  cup_log(CUP_Level_WARN, (msg))

#define cup_errorf(f, ...) \
    do { \
        char *_msg = cup_sprintf((f), __VA_ARGS__); \
        cup_log(CUP_Level_ERRO, _msg); \
        cup_free(_msg); \
    } while (0)

#define cup_error(msg) cup_log(CUP_Level_ERRO, (msg))

/* ========================================================================== */
/*                           DYNAMIC VECTORS                                  */
/* ========================================================================== */

/** Generic dynamic array. */
typedef struct CVector {
    void  *data;
    size_t size;
    size_t capacity;
} CVector;

void vector_fpop(CVector *vec, size_t elem_size);
void vector_push (CVector *vec, const void *data, size_t elem_size);
void vector_fpush(CVector *vec, const void *data, size_t elem_size);
void push_vector (CVector *dest, CVector src);   /* consumes src */
void fpush_vector(CVector *dest, CVector src);   /* consumes src */

#define vector_pushT(vec, val)  vector_push (&(vec), &(val), sizeof(val))
#define vector_fpushT(vec, val) vector_fpush(&(vec), &(val), sizeof(val))
#define vector_fpopT(vector)    vector_fpop (&(vector.vec),  sizeof(*vector.data))

#ifndef CVecT
#define CVecT(T) \
typedef union T##s { \
    struct { T *data; size_t size; size_t capacity; }; \
    CVector vec; \
} T##s
#endif

/* ========================================================================== */
/* ========================================================================== */

typedef struct NRange {
    Node *it;
    Node *end;
} NRange;

CVecT(NRange);

typedef struct CRealloc {
    size_t         *var;
    size_t          ptr;
} CRealloc;

CVecT(CRealloc);

struct CUPModule {
    //CUPState *state;
    const char *path;
    uint8_t    *data;
    size_t     size;
    CReallocs  reallocs;
    size_t l1, l2;
};

static_assert(sizeof(struct CUPModule) == (8 * sizeof(size_t)),
              "CUPModule size must be 4 bytes");

typedef struct CValue {
    const struct CUPType *type;
    void *value;
} CValue;

CVecT(CValue);

void codegen_func(CUPState* state, CUPModule* buf, size_t *value, size_t arg_count);

int defType(CUPState *state, const NRange nodes);
const CUPType    *cup_type_get(CUPState *state, const CUPType type);
const CUPType    *cup_type_put(CUPState *state, const CUPType type);
const CUPType    *cup_type_parse(CUPState* state, const char* sig);

void externalSymbol(CUPState* state, size_t name_idx, size_t sig_idx);

typedef struct CUP_GEN_CTX {
    const struct CUPType *argtype;
    void                 *argvalue;
    void                 *userctx;
} CUP_GEN_CTX;

typedef enum CUP_GEN_RESULT {
    CUP_GEN_ERROR = -1,
    CUP_GEN_CONTINUE = 0,
    CUP_GEN_DONE   = 1
} CUP_GEN_RESULT;


typedef CUP_GEN_RESULT (*CUP_Type_Gen)(CUPState* state,
    CValue arg, size_t i, size_t max, struct CValue* rvalue);

#undef CUPType
#define CUPType CUPTypeComplex

typedef struct CUPTypeComplex {
    size_t size;
    uint16_t alignment;
    uint16_t realtype;
    union {
        const struct CUPType **elements;
        CUP_Type_Gen gen;
    };
} CUPTypeComplex;

#define cup_type_generator(_func, ...) \
    (CUPType){ \
        .size = 0, \
        .alignment = 0, \
        .realtype = CUP_TYPE_GENERATOR, \
        .gen = (_func) \
    }

#define cup_type_pointer(_type) \
    (CUPType){ \
        .size = __SIZEOF_POINTER__, \
        .alignment = (uint16_t)sizeof(void *), \
        .realtype = CUP_TYPE_POINTER, \
        .elements = (const CUPType *[]){ \
            (_type), \
            NULL \
        } \
    }

#define cup_type_array(_type, _size) \
    (CUPType){ \
        .size = _size, \
        .alignment = 0, \
        .realtype = CUP_TYPE_ARRAY, \
        .elements = (const CUPType *[]){ \
            (_type), \
            NULL \
        } \
    }

#define cup_type_function(_type, ...) \
    (CUPType){ \
        .size = sizeof(void *), \
        .alignment = (uint16_t)sizeof(void *), \
        .realtype = CUP_TYPE_FUNCTION, \
        .elements = (const CUPType *[]){ \
            (_type), \
            __VA_ARGS__, \
            &cup_type_void \
        } \
    }

#endif