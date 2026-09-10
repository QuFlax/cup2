#ifndef LIBCUP_H
#error "Include libcup.h before libcupext.h"
#endif

#ifndef LIBCUP_EXT_H
#define LIBCUP_EXT_H

#include <assert.h>
#include <stdarg.h>

#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

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

void* cup_talloc(size_t size);
void cup_treset(size_t size);
size_t cup_tpos();


/* ========================================================================== */
/*                           LOGGING HELPERS                                  */
/* ========================================================================== */

#ifndef CUP_DEBUG
#  define cup_dbgf(f, ...) ((void)0)
#  define cup_dbg(msg)     ((void)0)
#else

#define cup_dbgf(f, ...) \
    do { \
        char *_msg = cup_sprintf((f), __VA_ARGS__); \
        cup_log(CUP_Level_INFO, _msg); \
        cup_free(_msg); \
    } while (0)
#define cup_dbg(msg) cup_log(CUP_Level_INFO, (msg))

#endif


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
/*                           TODO NAME                                  */
/* ========================================================================== */

typedef uint16_t CTType;

/** Source location for error reporting. */
typedef struct {
    CTType   token;
    uint16_t col;
    uint32_t line;
} Loc;

/** Single AST node. */
union Node {
    CTType token;
    Loc    loc;
    size_t value;
    double dvalue;
    CVariable  *variable;
    const CUPType* vtype;
};

typedef enum CUP_GEN_RESULT {
    CUP_GEN_ERROR = -1,
    CUP_GEN_CONTINUE = 0,
    CUP_GEN_DONE   = 1
} CUP_GEN_RESULT;

/**
 * Generator callback.
 *
 * A generator can either return a normal compile-time value with
 * cup_gen_set_value(), or construct a replacement AST expression with
 * cup_gen_add_node()/cup_gen_set_node(). The generated root must be node 0;
 * nodes must be emitted in the same prefix/preorder layout as the parser AST.
 */
typedef CUP_GEN_RESULT (*CUP_Type_Gen)(CUPGenContext *ctx,
    const CUPType **argtypes, size_t **argvalues, size_t max);

void cup_gen_value(CUPGenContext *ctx, const CUPType *type, void *value);
void cup_gen_call(CUPGenContext *ctx, const CUPType *rtype, void *value);
//void* cup_gen_get_value(CUPGenContext *ctx);
//CUP_EXTERN size_t cup_gen_add_node(CUPGenContext *ctx, Node node);
//CUP_EXTERN void cup_gen_set_node(CUPGenContext *ctx, size_t root);
//CUP_EXTERN void cup_gen_set_value(CUPGenContext *ctx, const CUPType *type, void *value);

//typedef CUP_GEN_RESULT (*CUP_Type_Gen)(CUPState* state, CUPValue* args, size_t max, const CUPType** rtype);
//typedef CUP_GEN_RESULT (*CUP_Type_Gen)(CUPState* state,
//    const CUPType **argtypes, void **argvalues,
//    size_t max);


union CUPTypeExt {
    CUPType cuptype;
    struct {
        size_t size;
        uint16_t type;
        uint16_t alignment;
        CUP_Type_Gen gen;
    };
};

static_assert(sizeof(CUPType) == sizeof(CUPTypeExt), "sizeof(CUPType) == sizeof(CUPTypeExt)");

#define cup_type_generator(_func) (CUPTypeExt){ \
        .size = 0, \
        .alignment = 0, \
        .type = CUP_TYPE_GENERATOR, \
        .gen = (_func) \
    }


const CUPType *cup_type_puts(CUPGenContext *ctx, const CUPType type);

/* ========================================================================== */
/*                           DYNAMIC VECTORS                                  */
/* ========================================================================== */

/** Generic dynamic array. */
typedef struct CVector {
    void  *data;
    size_t size;
    size_t capacity;
} CVector;

void vector_pop(CVector *vec, size_t sizeT);
void vector_fpop(CVector *vec, size_t sizeT);
void vector_push (CVector *vec, const void *data, size_t sizeT);
void vector_fpush(CVector *vec, const void *data, size_t sizeT);
void push_vector (CVector *dest, CVector src);   /* consumes src */
void fpush_vector(CVector *dest, CVector src);   /* consumes src */
void split_vector(CVector *dest, CVector* src, size_t size);

#define vector_pushT(vec, val)  vector_push (&(vec), &(val), sizeof(val))
#define vector_fpushT(vec, val) vector_fpush(&(vec), &(val), sizeof(val))
//#define vector_popT(vector)     vector_pop  (&(vector.vec),  sizeof(*vector.data))
//#define vector_fpopT(vector)    vector_fpop (&(vector.vec),  sizeof(*vector.data))

#ifndef CVecT
#define CVecT(T) \
typedef union T##s { \
    struct { T *data; size_t size; }; \
    CVector vec; \
} T##s
#endif

//CVecT(CUPValue);

typedef struct NRange {
    Node *it;
    Node *end;
} NRange;

CVecT(NRange);

/* ========================================================================== */
/* ========================================================================== */

typedef struct CObjRef {
    size_t     offset;      /* byte offset in buf->data where the imm64 starts */
    int        kind;        /* 0 = variable address (&var->value), 1 = literal data offset */
    CVariable *var;         /* kind == 0 */
    size_t     data_offset; /* kind == 1: offset into state->data */
} CObjRef;

CVecT(CObjRef);

typedef struct CRealloc {
    size_t         *var;
    size_t          ptr;
} CRealloc;

CVecT(CRealloc);

struct CUPModule {
#if 0
    CUPState *state;
#endif
    const char *path;
    uint8_t    *data;
    size_t     size;
    CReallocs  reallocs;
    //NRange     range;
    CObjRefs   objrefs;
};

static_assert(sizeof(struct CUPModule) == (9 * sizeof(size_t)),
              "CUPModule size must be 4 bytes");

void emit_error(CUPModule *buf, size_t nbytes);
void emit      (CUPModule *buf, const void *value, size_t size);

#define emit8(buf, v)  do { uint8_t  _v = (v); emit((buf), &_v, 1); } while (0)
#define emit16(buf, v) do { uint16_t _v = (v); emit((buf), &_v, 2); } while (0)
#define emit32(buf, v) do { uint32_t _v = (v); emit((buf), &_v, 4); } while (0)
#define emit64(buf, v) do { uint64_t _v = (v); emit((buf), &_v, 8); } while (0)

#define emiti32(buf, v) do { int32_t _v = (v); emit((buf), &_v, 4); } while (0)

void codegen_func(CUPState* state, CUPModule* buf, NRange* range, size_t *value, size_t arg_count);

void externalSymbol(CUPState* state, size_t name_idx, size_t sig_idx);

#endif