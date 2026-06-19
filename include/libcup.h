/**
 * @file libcup.h
 * @brief Main API for the CUP compiler library
 * @version 1.1
 *
 * Public interface for CUP - a JIT compiler that compiles custom scripts
 * to native x64 machine code.
 */

#ifndef LIBCUP_H
#define LIBCUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

/* Error codes */
#define CUP_ERR_CTYPE               NULL
#define CUP_ERR_MEMORY              -1
#define CUP_ERR_ARGUMENTS           -2
#define CUP_ERR_FILE_NOT_FOUND      -3
#define CUP_ERR_FILE_NOT_RECOGNIZED -4

/* Backwards-compat alias for the old typo */
#define CUP_ERR_MOMORY CUP_ERR_MEMORY

#define CUP_API
#define CUP_EXTERN extern CUP_API

typedef struct CUPState  CUPState;
typedef struct CUPModule CUPModule;

/* ========================================================================== */
/*                              ALLOCATOR / LOGGER                            */
/* ========================================================================== */

//typedef void *(*CUP_Realloc_Fn)(const void *ptr, size_t size);
typedef void *(*CUP_Realloc_Fn)(void *ptr, size_t size);

CUP_Realloc_Fn *_cup_realloc(void);
#define cup_realloc (*_cup_realloc())

/** @brief Set a custom memory allocator (per-thread). */
void cup_set_realloc(CUP_Realloc_Fn realloc_func);

typedef enum {
    CUP_Level_INFO = 0, /**< Informational — may be ignored */
    CUP_Level_WARN = 1, /**< Warning */
    CUP_Level_ERRO = 2  /**< Error */
} CUP_LogLevel;

typedef void (*CUP_Log_Fn)(CUP_LogLevel level, const char *msg);

CUP_Log_Fn *_cup_log(void);
#define cup_log (*_cup_log())

/** @brief Set a custom log/error handler (per-thread). */
void cup_set_error_func(CUP_Log_Fn error_func);

/* ========================================================================== */
/*                              TYPE SYSTEM                                   */
/* ========================================================================== */

typedef enum CUP_TYPE {
    CUP_TYPE_VOID,
    CUP_TYPE_INT,
    CUP_TYPE_FLOAT,
    CUP_TYPE_DOUBLE,
    CUP_TYPE_UINT8,
    CUP_TYPE_SINT8,
    CUP_TYPE_UINT16,
    CUP_TYPE_SINT16,
    CUP_TYPE_UINT32,
    CUP_TYPE_SINT32,
    CUP_TYPE_UINT64,
    CUP_TYPE_SINT64,
    CUP_TYPE_STRUCT,
    CUP_TYPE_POINTER,
    CUP_TYPE_ARRAY,
    CUP_TYPE_FUNCTION,
    CUP_TYPECOUNT
} CUP_TYPE;

typedef struct CUPType {
    size_t size;
    uint16_t alignment;
    uint16_t realtype;
    const struct CUPType **elements;
} CUPType;

CUP_EXTERN CUPType cup_type_void;
CUP_EXTERN CUPType cup_type_int;
CUP_EXTERN CUPType cup_type_float;
CUP_EXTERN CUPType cup_type_double;
CUP_EXTERN CUPType cup_type_uint8;
CUP_EXTERN CUPType cup_type_sint8;
CUP_EXTERN CUPType cup_type_uint16;
CUP_EXTERN CUPType cup_type_sint16;
CUP_EXTERN CUPType cup_type_uint32;
CUP_EXTERN CUPType cup_type_sint32;
CUP_EXTERN CUPType cup_type_uint64;
CUP_EXTERN CUPType cup_type_sint64;

#define cup_type_struct(...) \
    (CUPType){ 0, 0, CUP_TYPE_STRUCT, \
        (const CUPType *[]){ __VA_ARGS__, NULL } }

#define cup_type_pointer(type) \
    (CUPType){ 0, 0, CUP_TYPE_POINTER, \
        (const CUPType *[]){ (type), NULL } }

#define cup_type_array(type) \
    (CUPType){ 0, 0, CUP_TYPE_ARRAY, \
        (const CUPType *[]){ (type), NULL } }

#define cup_type_function(rtype, ...) \
    (CUPType){ \
        sizeof(void *), \
        (uint16_t)sizeof(void *), \
        CUP_TYPE_FUNCTION, \
        (const CUPType *[]){ \
            (rtype), \
            __VA_ARGS__, \
            NULL \
        } \
    }

typedef struct CVariable CVariable;

/** @brief Return a human-readable name for a type (freshly allocated). */
size_t cup_type_snname(char *restrict s, size_t maxlen, const CUPType *type);
//char *cup_type_name(const CUPType *type);

const uint8_t *getdata(CUPState *state, const char *str, size_t len);
const char    *getString(CUPState *state, size_t index);

/* ========================================================================== */
/*                              TARGETS                                       */
/* ========================================================================== */

typedef void *(*CUPCodeGen)(CUPState *state, CUPModule *buf);

typedef struct CUPTarget {
    char arch[8];
    char os[8];
    CUPCodeGen generator;
} CUPTarget;

CUPTarget *cup_get_target(size_t i);
size_t     cup_get_target_count(void);

/* ========================================================================== */
/*                              STATE LIFECYCLE                               */
/* ========================================================================== */

/**
 * @brief Create a new compiler state.
 * @param target Target index, or -1 for the default (host) target.
 * @return New state, or NULL on failure.
 */
CUPState *cup_new(int target);

/** @brief Destroy a compiler state and free all associated resources. */
void cup_delete(CUPState *state);

/* ========================================================================== */
/*                              SYMBOL MANAGEMENT                             */
/* ========================================================================== */

/**
 * @brief Register an external symbol.
 * @return 0 on success, negative error code otherwise.
 */
int cup_add_symbol(CUPState *state, const char *name, void *val,
                   CUPType type);

/**
 * @brief Look up a symbol by name.
 * @return Pointer to the CVariable entry, or NULL if not found.
 */
CVariable *cup_get_symbol(CUPState *state, const char *name);

void* cup_var_value(CVariable* var);
//const char* cup_var_name(CUPState* state, CVariable* var);
//const CUPType* cup_var_type(CUPState* state, const CVariable* var);

typedef void (*cup_list_symbols_callback)(void *ctx, const char *name, const size_t val, const CUPType *type);

/** @brief Iterate over all registered symbols. */
void cup_list_symbols(CUPState *state, void *ctx, cup_list_symbols_callback cb);

/* ========================================================================== */
/*                              COMPILATION                                   */
/* ========================================================================== */

/** @brief Compile source from a NUL-terminated string. */
int cup_compile_string(CUPState *state, const char *source);

/** @brief Compile source from a file. */
int cup_compile_file(CUPState *state, const char *filename);

/** @brief Write an ELF64 relocatable object file. */
int cup_output_object(CUPState *state, const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* LIBCUP_H */