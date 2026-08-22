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

//void* talloc(size_t size);
//void treset(size_t size);

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

void vector_fpop(CVector *vec, size_t sizeT);
void vector_push (CVector *vec, const void *data, size_t sizeT);
void vector_fpush(CVector *vec, const void *data, size_t sizeT);
void push_vector (CVector *dest, CVector src);   /* consumes src */
void fpush_vector(CVector *dest, CVector src);   /* consumes src */
CVector split_vector(CVector *vec, size_t index);

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
    NRange     range;
};

static_assert(sizeof(struct CUPModule) == (8 * sizeof(size_t)),
              "CUPModule size must be 4 bytes");

CVecT(CValue);

void codegen_func(CUPState* state, CUPModule* buf, size_t *value, size_t arg_count);

const CUPType    *cup_type_get(CUPState *state, const CUPType type);
const CUPType    *cup_type_put(CUPState *state, const CUPType type);
const CUPType    *cup_type_parse(CUPState* state, const char* sig);

void externalSymbol(CUPState* state, size_t name_idx, size_t sig_idx);

#endif