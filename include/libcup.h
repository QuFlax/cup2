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

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

#define CUP_ERR_CTYPE NULL
#define CUP_ERR_MOMORY -1
#define CUP_ERR_STATE -2
#define CUP_ERR_FILE_NOT_FOUND -3
#define CUP_ERR_FILE_NOT_RECOGNIZED -4

//TODO: CROSSCOMPILE STATIC/DLL(import)/SO
#define CUP_API
# define CUP_EXTERN extern CUP_API

typedef enum {
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
};
typedef uint16_t CUP_TYPE;

typedef enum {
  CUP_Level_INFO = 0, /**< Info messages (can be ignored)*/
  CUP_Level_WARN = 1, /**< Warning message */
  CUP_Level_ERRO = 2  /**< Error message */
} CUP_LogLevel;

typedef void *(*CUP_Realloc_Fn)(void *ptr, size_t size);

CUP_Realloc_Fn* _cup_realloc(void);
#define cup_realloc (*_cup_realloc())

/**
 * @brief Set custom memory allocator
 * @param realloc_func Allocator function
 */
void cup_set_realloc(CUP_Realloc_Fn realloc_func);

typedef void (*CUP_Log_Fn)(CUP_LogLevel level, const char *msg);

CUP_Log_Fn* _cup_log(void);
#define cup_log (*_cup_log())

/**
 * @brief Set custom error handler
 * @param error_func Error handler function
 */
void cup_set_error_func(CUP_Log_Fn error_func);

typedef struct CUPState CUPState;

/**
 * @brief Create a new compiler state
 * @param target Custom target generator (-1 for default)
 * @return New compiler state or NULL on failure
 */
CUPState *cup_new(int target);

/**
 * @brief Destroy compiler state and free resources
 * @param state Compiler state to destroy
 */
void cup_delete(CUPState *state);

typedef struct CUPModule CUPModule;

typedef void *(*CUPCodeGen)(CUPState *state, CUPModule* buf);

typedef struct CUPTarget {
  char arch[8];
  char os[8];
  CUPCodeGen generator;
} CUPTarget;

CUPTarget* cup_get_target(size_t i);
size_t cup_get_target_count();

/* ========================================================================== */
/*                              TYPE SYSTEM                                   */
/* ========================================================================== */

typedef struct _CUPType {
  size_t size;
  uint16_t alignment;
  CUP_TYPE realtype;
  union {
    struct _CUPType** types;
    struct _CUPType* type;
  };
} CUPType;

CUP_EXTERN CUPType cup_type_void;
CUP_EXTERN CUPType cup_type_uint8;
CUP_EXTERN CUPType cup_type_sint8;
CUP_EXTERN CUPType cup_type_uint16;
CUP_EXTERN CUPType cup_type_sint16;
CUP_EXTERN CUPType cup_type_uint32;
CUP_EXTERN CUPType cup_type_sint32;
CUP_EXTERN CUPType cup_type_uint64;
CUP_EXTERN CUPType cup_type_sint64;
CUP_EXTERN CUPType cup_type_float;
CUP_EXTERN CUPType cup_type_double;

//const CUPType* cup_type_geti(CUPState *state, const size_t i);
//void cup_type_put(CUPState *state, const char *key, CUPType value);
//const CUPType *cup_type_get_pointer(CUPState *state, const CUPType *ptrtype);
//const CUPType *cup_type_get_array(CUPState *state, size_t count, const CUPType *ptrtype);
//const CUPType *cup_type_get_function(CUPState *state, CUPTarget* target, const CUPType *args, const CUPType *return_type);

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
                   const CUPType *type);

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

/**
 * @brief Output object code from context
 * @param state Compiler state
 * @param filename Path to object file
 * @return 0 on success, error code otherwise
 */
int cup_output_object(CUPState *state, const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* LIBCUP_H */
