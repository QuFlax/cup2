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

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>

/* Error codes */
//#define CUP_ERR_CTYPE               NULL
//#define CUP_ERR_MEMORY              -1
// #define CUP_ERR_ARGUMENTS           -2
// #define CUP_ERR_FILE_NOT_FOUND      -3
// #define CUP_ERR_FILE_NOT_RECOGNIZED -4

#if defined(CUP_SHARED)
#if defined(CUP_EXPORTS)
#   define CUP_API __declspec(dllexport)
#else
#   define CUP_API __declspec(dllimport)
#endif
#else
#   define CUP_API
#endif

#ifdef __cplusplus
#   define CUP_EXTERN extern "C" CUP_API
#else
#   define CUP_EXTERN extern CUP_API
#endif

CUP_API int cup_version_major();
CUP_API int cup_version_minor();
CUP_API int cup_version();

typedef union  Node       Node;
typedef struct CUPState   CUPState;
typedef struct CUPModule  CUPModule;
typedef struct CUPType    CUPType;
typedef union  CUPTypeExt CUPTypeExt;
typedef struct CVariable  CVariable;

/* ========================================================================== */
/*                              ALLOCATOR / LOGGER                            */
/* ========================================================================== */

//typedef void *(*CUP_Realloc_Fn)(const void *ptr, size_t size);
typedef void *(*CUP_Realloc_Fn)(void *ptr, size_t size);

CUP_API CUP_Realloc_Fn *_cup_realloc(void);
#define cup_realloc (*_cup_realloc())

/** @brief Set a custom memory allocator (per-thread). */
CUP_API void cup_set_realloc(CUP_Realloc_Fn realloc_func);

typedef enum CUP_LogLevel {
    CUP_Level_INFO = 0, /**< Informational — if debug */
    CUP_Level_WARN = 1, /**< Warning */
    CUP_Level_ERRO = 2  /**< Error */
} CUP_LogLevel;

typedef void (*CUP_Log_Fn)(CUP_LogLevel level, const char *msg);

CUP_API CUP_Log_Fn *_cup_log(void);
#define cup_log (*_cup_log())

/** @brief Set a custom log/error handler (per-thread). */
CUP_API void cup_set_error_func(CUP_Log_Fn error_func);

/* ========================================================================== */
/*                              TYPE SYSTEM                                   */
/* ========================================================================== */

typedef enum CUP_TYPE : uint8_t {
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
    CUP_TYPE_GENERATOR,
    CUP_TYPE_COUNT
} CUP_TYPE;

struct CUPType {
    CUP_TYPE type;
    uint8_t alignment;
    uint16_t size;
    const CUPType **elements;
};

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

#if 0
#define cup_type_pointer(_type) (CUPType) { \
        .size = __SIZEOF_POINTER__, \
        .type = CUP_TYPE_POINTER, \
        .alignment = (uint16_t)sizeof(void *), \
        .elements = (const CUPType *[]){ \
            (_type), \
            NULL \
        } \
    }

#define cup_type_array(_type, _size) (CUPType) { \
        .size = _size, \
        .type = CUP_TYPE_ARRAY, \
        .alignment = 0, \
        .elements = (const CUPType *[]){ \
            (_type), \
            NULL \
        } \
    }

#define cup_type_function(_type, ...) (CUPType) { \
        .size = sizeof(void *), \
        .type = CUP_TYPE_FUNCTION, \
        .alignment = (uint16_t)sizeof(void *), \
        .elements = (const CUPType *[]){ \
            (_type), \
            __VA_ARGS__, \
            &cup_type_void \
        } \
    }
#endif

/** @brief Return a human-readable name for a type (freshly allocated). */
CUP_API size_t cup_type_snname(char *restrict s, size_t maxlen, const CUPType *type);
//char *cup_type_name(const CUPType *type);

CUP_API const CUPType* cup_type(CUPState* state, CUP_TYPE type, uint16_t size, const CUPType **elements);
CUP_API const CUPType* cup_vtypef(CUPState* state, uint8_t count, va_list args);
CUP_API const CUPType* cup_typef(CUPState* state, uint8_t count, ...);

CUP_API void* cup_var_value(const CVariable* var);
CUP_API const CUPType* cup_var_type(const CVariable* var);

CUP_API const uint8_t *getData(CUPState *state, size_t i);
CUP_API const char    *getString(CUPState *state, size_t index);

/* ========================================================================== */
/*                              TARGETS                                       */
/* ========================================================================== */

typedef void *(*CUPCodeGen)(CUPState *state, CUPModule *buf);

typedef struct CUPTarget {
    char arch[8];
    char os[8];
    CUPCodeGen generator;
} CUPTarget;

CUP_API CUPTarget *cup_get_target(size_t i);
CUP_API size_t     cup_get_target_count(void);

/* ========================================================================== */
/*                              STATE LIFECYCLE                               */
/* ========================================================================== */

/**
 * @brief Create a new compiler state.
 * @param target Target index, or -1 for the default (host) target.
 * @return New state, or NULL on failure.
 */
CUP_API CUPState *cup_new(int target);

/** @brief Destroy a compiler state and free all associated resources. */
CUP_API void cup_delete(CUPState *state);

/* ========================================================================== */
/*                              SYMBOL MANAGEMENT                             */
/* ========================================================================== */

/**
 * @brief Register an external symbol.
 * @return 0 on success, negative error code otherwise.
 */
CUP_API int cup_add_symbol(CUPState *state, const char *name, void *val, const CUPType* type);

/**
 * @brief Look up a symbol by name.
 * @return Pointer to the CVariable entry, or NULL if not found.
 */
CUP_API CVariable *cup_get_symbol(CUPState *state, const char *name);

//void* cup_var_value(CVariable* var);
//const char* cup_var_name(CUPState* state, CVariable* var);
//const CUPType* cup_var_type(CUPState* state, const CVariable* var);

typedef void (*cup_list_symbols_callback)(void *ctx, const char *name,
    const CUPType *type, size_t value, size_t scope);

/** @brief Iterate over all registered symbols. */
CUP_API void cup_list_symbols(CUPState *state, void *ctx, cup_list_symbols_callback cb);

/* ========================================================================== */
/*                              COMPILATION                                   */
/* ========================================================================== */

CUP_API const char* cup_bytecode_path(CUPState* state, const char* path);
CUP_API const char* cup_code_path(CUPState* state, const char* path);

//void cup_write_bytecode(CUPState* state, CUPModule* m);
CUP_API void cup_write_code(CUPState* state, CUPModule *m);

/** @brief Compile source from a NUL-terminated string. */
//int cup_compile_string(CUPState *state, const char *source);
CUP_API CUPModule* cup_compile_string(CUPState *state, const char *source);

/** @brief Compile source from a file. */
//int cup_compile_file(CUPState *state, const char *filename);
CUP_API CUPModule* cup_compile_file(CUPState *state, const char *filename);

#endif /* LIBCUP_H */