/**
 * @file libcup.h
 * @brief Main API for the CUP compiler library
 * @version 1.0
 *
 * This header provides the public interface for CUP - a JIT compiler
 * that compiles custom scripts to native x64 machine code.
 */

#ifndef LIBCUP_H
#define LIBCUP_H

#include <stddef.h>
#include <stdint.h>

/* Type system constants */
#define CUP_ERROR_CTYPE nullptr
#define CUP_FILE_NOT_FOUND -2
#define CUP_FILE_NOT_RECOGNIZED -3

#ifdef __cplusplus
extern "C" {
#endif

/** Error levels for diagnostic messages */
typedef enum {
  CEL_INFO = 0, /**< Info messages (can be ignored)*/
  CEL_WARN = 1, /**< Warning message */
  CEL_ERRO = 2  /**< Error message */
} CUPLogLevel;

/** Function pointer for custom memory allocation */
typedef void *(*CUPReallocFunc)(void *ptr, size_t size);

/** Function pointer for custom error handling */
typedef void (*CUPLogFunc)(CUPLogLevel level, const char *msg);

/** Opaque handle to compiler state */
typedef struct CUPState CUPState;

/**
 * @brief Create a new compiler state
 * @param realloc_func Custom allocator (NULL for default)
 * @param error_func Custom error handler (NULL for default)
 * @return New compiler state or NULL on failure
 */
CUPState *cup_new(CUPReallocFunc *realloc_func, CUPLogFunc *error_func);

/**
 * @brief Destroy compiler state and free resources
 * @param state Compiler state to destroy
 */
void cup_delete(CUPState *state);

/**
 * @brief Set custom error handler
 * @param state Compiler state
 * @param error_func Error handler function
 */
void cup_set_error_func(CUPState *state, CUPLogFunc *error_func);

/**
 * @brief Set custom memory allocator
 * @param state Compiler state
 * @param realloc_func Allocator function
 */
void cup_set_realloc(CUPState *state, CUPReallocFunc *realloc_func);

void cup_set_ttable_size(CUPState *state, size_t size);
size_t cup_get_ttable_size(CUPState *state);

/** Supported target architectures */
typedef enum {
  CT_UNKNOWN = 0, /**< Unknown/unsupported architecture */
  CT_X64 = 1      /**< x86-64 architecture */
} CUPTarget;

/**
 * @brief Set target architecture
 * @param state Compiler state
 * @param type Target architecture
 */
void cup_set_target(CUPState *state, CUPTarget target);

/* ========================================================================== */
/*                              TYPE SYSTEM                                   */
/* ========================================================================== */

/** Opaque type handle */
typedef struct CType CType;

/* Calling conventions */
#define ABI_SYSV 1 /**< System V ABI (Linux/Unix x64) */
#define ABI_MX64 2 /**< Microsoft x64 ABI (Windows) */

#ifndef _WIN32
#define ABI_DEFAULT ABI_SYSV
#else
#define ABI_DEFAULT ABI_MX64
#endif

/* Type construction functions */

/**
 * @brief Get void type
 * @param state Compiler state
 * @return Void type or NULL on failure
 */
const CType *cup_type_get_void(CUPState *state);

/**
 * @brief Get numeric type
 * @param state Compiler state
 * @param size Size in bytes
 * @param issigned Whether type is signed
 * @return Number type or NULL on failure
 */
const CType *cup_type_get_number(CUPState *state, uint8_t size, int issigned);

/**
 * @brief Get double-precision floating point type
 * @param state Compiler state
 * @return Double type
 */
const CType *cup_type_get_double(CUPState *state);

/**
 * @brief Get pointer type
 * @param state Compiler state
 * @param ptrtype Pointed-to type
 * @return Pointer type
 */
const CType *cup_type_get_pointer(CUPState *state, const CType *ptrtype);

/**
 * @brief Get array type
 * @param state Compiler state
 * @param count Number of elements
 * @param ptrtype Element type
 * @return Array type
 */
const CType *cup_type_get_array(CUPState *state, size_t count,
                                const CType *ptrtype);

/**
 * @brief Get complex/tuple type
 * @param state Compiler state
 * @param count Number of fields
 * @param types Array of field types
 * @return Complex type
 */
const CType *cup_type_get_complex(CUPState *state, uint8_t count,
                                  const CType **types);

/**
 * @brief Get function type
 * @param state Compiler state
 * @param abi Calling convention
 * @param args Argument types (as complex type)
 * @param return_type Return type
 * @return Function type
 */
const CType *cup_type_get_function(CUPState *state, uint8_t abi,
                                   const CType *args, const CType *return_type);

/* ========================================================================== */
/*                           SYMBOL MANAGEMENT                                */
/* ========================================================================== */

/**
 * @brief Register external symbol
 * @param state Compiler state
 * @param name Symbol name
 * @param val Symbol address
 * @param type Symbol type
 * @return 0 on success, -1 on error
 */
int cup_add_symbol(CUPState *state, const char *name, void *val,
                   const CType *type);

/**
 * @brief Look up symbol by name
 * @param state Compiler state
 * @param name Symbol name
 * @return Symbol address or NULL if not found
 */
void *cup_get_symbol(CUPState *state, const char *name);

/* ========================================================================== */
/*                              COMPILATION                                   */
/* ========================================================================== */

/**
 * @brief Compile source code from string
 * @param state Compiler state
 * @param source Source code string
 * @return 0 on success, error code otherwise
 */
int cup_compile_string(CUPState *state, const char *source);

/**
 * @brief Compile source code from file
 * @param state Compiler state
 * @param filename Path to source file
 * @return 0 on success, error code otherwise
 */
int cup_compile_file(CUPState *state, const char *filename);

// int cup_output_file(CUPState *state, const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* LIBCUP_H */
