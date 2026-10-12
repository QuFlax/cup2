#ifndef LIBCUP_H
#error "Include libcup.h before libcupext.h"
#endif

#ifndef LIBCUP_EXT_H
#define LIBCUP_EXT_H

#include <assert.h>

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

CUP_API char *cup_vsprintf(const char *format, va_list args);
CUP_API char *cup_sprintf(const char *format, ...);
CUP_API char *cup_strdup(const char *str);

CUP_API void* cup_talloc(size_t size);
CUP_API void cup_treset(size_t size);
CUP_API size_t cup_tpos();


/* ========================================================================== */
/*                           LOGGING HELPERS                                  */
/* ========================================================================== */

#define cup_dbgf(f, ...) \
    do { \
        char *_msg = cup_sprintf((f), __VA_ARGS__); \
        cup_log(CUP_Level_INFO, _msg); \
        cup_free(_msg); \
    } while (0)

#define cup_dbg(msg) cup_log(CUP_Level_INFO, (msg))

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
    size_t      value;
    double      dvalue;
    const char* svalue;
    CVariable  *variable;
    const CUPType* vtype;
};

typedef enum CUP_GEN_RESULT {
    CUP_GEN_ERROR = -1,
    CUP_GEN_CONTINUE = 0,
    CUP_GEN_DONE   = 1
} CUP_GEN_RESULT;

typedef struct CUPGenContext CUPGenContext;

// typedef CUP_GEN_RESULT (*CUP_Type_Gen)(CUPGenContext *ctx,
//     const CUPType **argtypes, size_t **argvalues, size_t max);

CUP_API void cup_gen_value(CUPGenContext *ctx, void *value, const CUPType* type);
CUP_API void cup_gen_call(CUPGenContext *ctx, void *value, size_t max, const CUPType* const* types);
CUP_API void cup_gen_address(CUPGenContext *ctx, void *lvalue, const CUPType* type);
//void* cup_gen_get_value(CUPGenContext *ctx);

/*
 * Meta types ($, printf, sizeof, ...) are resolved at compile time by a
 * CUP_Type_Meta callback. The callback receives the operation, the value
 * bound to the symbol and a CUP_META describing the operands.
 * CUP_META.op always equals the `op` argument; the active member of the
 * anonymous union is selected by it:
 *   CUP_MOP_MEMBER -> meta.member  ($.name)
 *   CUP_MOP_BRACK  -> meta.brack   ($[index])
 *   CUP_MOP_CALL   -> meta.call    ($(args...))
 * Callback must report exactly one result through cup_gen_value /
 * cup_gen_address / cup_gen_call and return CUP_GEN_DONE, or return
 * CUP_GEN_ERROR (after logging). CUP_GEN_CONTINUE is reserved and is
 * treated as an error by the resolver.
 */
typedef enum CUP_META_OP : uint8_t {
    CUP_MOP_MEMBER, // $.
    CUP_MOP_BRACK,  // $[]
    CUP_MOP_CALL,   // $(...)
} CUP_META_OP;

typedef union CUP_META {
    struct {
        const char* name;      /* NUL-terminated member name */
        size_t      len;
    } member;
    struct {
        const CUPType *indextype;   /* NULL if unknown */
        size_t        indexvalue;
    } brack;
    struct {
        const CUPType **argtypes; // [max], entries may be NULL
        size_t        *argvalues; // [max]
        size_t        max;
    } call;
} CUP_META;

typedef CUP_GEN_RESULT (*CUP_Type_Meta)(CUPGenContext *ctx, CUP_META_OP op, void* value, CUP_META meta);

/* Metadata callbacks: ERROR rejects the operation, CONTINUE leaves it in the
 * AST for normal resolution, and DONE consumes it and returns a result through
 * cup_gen_value(), cup_gen_address(), or cup_gen_call(). CUP_META pointers are
 * borrowed and must not be retained. */

typedef struct CUPTypeMeta {
    CUP_TYPE type;
    uint8_t alignment;
    uint16_t size;
    CUP_Type_Meta gen;
} CUPTypeMeta;

typedef struct CUPTypeArr {
    CUP_TYPE type;
    uint8_t alignment;
    uint16_t size;
    const CUPType* element;
} CUPTypeArr;

typedef struct CUPTypeFunction {
    CUP_TYPE type;
    uint8_t alignment;
    uint16_t size;
    const CUPType* returntype;
    const CUPType* argstype[];
} CUPTypeFunction;

typedef struct CUPTypeSimple {
    CUP_TYPE type;
    uint8_t alignment;
    uint16_t size;
} CUPTypeSimple;

union CUPType {
    struct {
        CUP_TYPE       cuptype;
        uint8_t alignment;
        uint16_t size;
        void* data[];
    };
    const CUPTypeSimple   stype;
    const CUPTypeArr      arrtype;
    const CUPTypeFunction functiontype;
    const CUPTypeMeta     metatype;
};

CUP_API const CUPType* cup_typemeta(CUPState* state, uint16_t size, CUP_Type_Meta gen);

static_assert(sizeof(CUPTypeSimple) == 4, "1");
static_assert(sizeof(CUPTypeMeta) == 16, "2");
static_assert(sizeof(CUPTypeArr) == 16, "3");
static_assert(sizeof(CUPTypeFunction) == 16, "3");
static_assert(sizeof(CUPType) == 16, "4");

#if 0
#define cup_type_generator(_func) (CUPTypeExt){ \
        .size = 0, \
        .alignment = 0, \
        .type = CUP_TYPE_GENERATOR, \
        .gen = (_func) \
    }
#endif

//const CUPType *cup_type_puts(CUPGenContext *ctx, const CUPType type);

/* ========================================================================== */
/*                           DYNAMIC VECTORS                                  */
/* ========================================================================== */

/** Generic dynamic array. */
typedef struct CVector {
    void  *data;
    size_t size;
    size_t capacity;
} CVector;

CUP_API void vector_new(CVector *vec, size_t sizeT);
CUP_API void vector_pop(CVector *vec, size_t sizeT);
CUP_API void vector_fpop(CVector *vec, size_t sizeT);
CUP_API void vector_push (CVector *vec, const void *data, size_t sizeT);
CUP_API void vector_fpush(CVector *vec, const void *data, size_t sizeT);
CUP_API void push_vector (CVector *dest, CVector src);   /* consumes src */
CUP_API void fpush_vector(CVector *dest, CVector src);   /* consumes src */
CUP_API void split_vector(CVector *dest, CVector* src, size_t size);

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

CVecT(size_t);

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
    union { \
        struct { void *data; size_t size; }; \
        CVector vec; \
    };

    //CReallocs  reallocs;
    //NRange     range;
    //CObjRefs   objrefs;
};

static_assert(sizeof(struct CUPModule) == (4 * sizeof(size_t)),
              "CUPModule size must be 4 bytes");

CUP_API void emit_error(CUPModule *buf, size_t nbytes);
CUP_API void emit      (CUPModule *buf, const void *value, size_t size);

#define emit8(buf, v)  do { uint8_t  _v = (v); emit((buf), &_v, 1); } while (0)
#define emit16(buf, v) do { uint16_t _v = (v); emit((buf), &_v, 2); } while (0)
#define emit32(buf, v) do { uint32_t _v = (v); emit((buf), &_v, 4); } while (0)
#define emit64(buf, v) do { uint64_t _v = (v); emit((buf), &_v, 8); } while (0)

#define emiti32(buf, v) do { int32_t _v = (v); emit((buf), &_v, 4); } while (0)

CUP_API void codegen_func(CUPState* state, CUPModule* buf, CReallocs* reallocs, NRange* range, size_t *value, size_t arg_count);

CUP_API void externalSymbol(CUPState* state, const char* name, size_t sig_idx);

/** @brief Write an ELF64 relocatable object file. */
CUP_API int cup_output_object(CUPState *state, CReallocs* reallocs, const char *filename);

#endif