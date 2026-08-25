/**
 * @file cup.h
 * @brief Internal header for the CUP compiler (not part of the public API).
 */

#ifndef CUP_H
#define CUP_H

#include "libcup.h"
#include "libcupext.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define CUPDEFPOWER 2

/* ========================================================================== */
/*                           DEBUG LOGGING                                    */
/* ========================================================================== */

#ifdef CUP_DEBUG
#  define cup_dbgf(f, ...) \
    do { \
        const char *_msg = cup_sprintf((f), __VA_ARGS__); \
        cup_log(CUP_Level_INFO, _msg); \
        cup_free(_msg); \
    } while (0)
#  define cup_dbg(msg) cup_log(CUP_Level_INFO, (msg))
#else
#  define cup_dbgf(f, ...) ((void)0)
#  define cup_dbg(msg)     ((void)0)
#endif



/* ========================================================================== */
/*                           SYMBOL HASH MAP                                  */
/* ========================================================================== */

/* TODO: change to true
 * Open-addressing Robin Hood hash map.
 * Key:   size_t name_idx  (interned name offset; SIZE_MAX = empty sentinel)
 * Value: CVariable        (type, name, value) stored inline — no separate array
 *
 * CVariable.name == SYMMAP_EMPTY marks a free slot.
 * Robin Hood invariant keeps probe distances short.
 */

#define CMAP_LOAD_NUM 3   /* grow when count*4 > capacity*3  (75 %) */
#define CMAP_LOAD_DEN 4

size_t map_hash(size_t key);

typedef struct CMap {
  void   *slots; // {key, dist, value}
  size_t count;    /* live entries */
  size_t capacity; /* slot count, always a power of two */
  size_t k_null;
} CMap;

typedef struct CMapItem {
  size_t  key;
  size_t  dist;
  uint8_t data[];
} CMapItem;

typedef void (*map_iter_callback)(void *ctx, const CMapItem *item);

void map_init(CMap *m, size_t k_null, size_t v_size, void* v_default);
void map_free(CMap *m);
CMapItem *map_get(const CMap *m, size_t key, CMapItem *prev, size_t v_size);
CMapItem *map_put(CMap *m, size_t key, void *value, size_t v_size);
/* Remove an entry (used when popping scopes). */
//void       map_del(CMap *m, size_t name_idx, size_t v_size, size_t k_null);
/* Iterate every live entry; cb receives each CVariable* in insertion order. */
//void       map_iter(const CMap *m, void *ctx, map_iter_callback cb, size_t v_size);

/* ========================================================================== */
/*                           TOKEN / NODE TYPES                               */
/* ========================================================================== */

#define DEF(name) name,
enum CTType_ {
#include "token.h"
    COUNT
};
#undef DEF
typedef uint16_t CTType;

#define N_VARIABLE T_IDENTIFIER
#define N_FUNCTION T_AT

extern const char *token_names[];

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

CVecT(Node);

/** Node + optional payload (number value / string index). */
typedef struct Node2 {
    union {
        CTType token;
        CVariable  *variable;
        Node       node;
    };
    union {
        const CUPType* vtype;
        size_t value;
        double dvalue;
    };
} Node2;

void importModule(CUPState* state);
void externalModule(CUPState* state);

const char       *node_name(Node n);
//size_t defTypeEx(CUPState *state, const Node *nodes, size_t *pos, const CUPType **value);

typedef union CTypes {
    struct {
        CUPType **data;
        size_t    size;
        size_t    capacity;
    };
    CVector vec;
} CTypes;

/* ========================================================================== */
/*                           STRING INTERNING                                 */
/* ========================================================================== */

uint8_t       *idToken(CUPState *state, const char *str, size_t len);

void getToken  (CUPState *state);
void skipSpaces(CUPState *state);

int defType(CUPState *state, Nodes* nodes);

/* ========================================================================== */
/*                           CODE EMISSION                                    */
/* ========================================================================== */

typedef struct CUPModuleList {
    CUPModule            module;
    struct CUPModuleList *next;
} CUPModuleList;

//typedef struct CExternal { void *handle; } CExternal;
//CVecT(CExternal);

void emit_error(CUPModule *buf, size_t nbytes);
void emit      (CUPModule *buf, const void *value, size_t size);

#define emit8(buf, v)  do { uint8_t  _v = (v); emit((buf), &_v, 1); } while (0)
#define emit16(buf, v) do { uint16_t _v = (v); emit((buf), &_v, 2); } while (0)
#define emit32(buf, v) do { uint32_t _v = (v); emit((buf), &_v, 4); } while (0)
#define emit64(buf, v) do { uint64_t _v = (v); emit((buf), &_v, 8); } while (0)

#define emiti32(buf, v) do { int32_t _v = (v); emit((buf), &_v, 4); } while (0)

/* ========================================================================== */
/*                           COMPILER STATE                                   */
/* ========================================================================== */

struct CUPState {
    /* Source input */
    const char *input_stream;
    const char *priv_stream;

    /* Current parser token / node */
    union {
        Node2 nodes;
        CVariable *variable;
    };
    union {
        struct {
            size_t *data;
            size_t size;
        };
        CVector vec;
    } scopes;
    size_t scope;

    /* String intern table.
     * names_base: original allocation pointer (needed for safe getString walk
     *             and for cup_delete to free exactly what was allocated).
     * names:      pointer to one past the last interned byte (grows upward). */
    uint8_t *names;

    /* General data (string literal storage) */
    uint8_t *data;
    size_t data_size;

    CUPModuleList *modules;

    union {
        struct {
            void **data;
            size_t size;
            size_t capacity;
        };
        CVector vec;
    } externals;

    /* Variable table */
    CMap symmap;

    CTypes types;
    CUPTarget *target;
    const char *bytecode_path;
    const char *code_path;
    CVector vector_;
    
    size_t l1, l2, l3, l4;
    // CUPCodeGen generator;
};

// 1 2 4 8 16 32 64 128 256
static_assert(sizeof(CUPState) == 256,
               "CUPState size must be 256 bytes");


/*
typedef struct CVariable {
    const CUPType *type; // < Variable type
    //size_t name;       // < Offset into string-intern table
    size_t value;        // < Variable value / symbol address
    size_t scope;
} CVariable;
*/

//CVariable* getVar(CUPState *state, size_t name);
CVariable *getVars(CUPState *state, size_t name, size_t maxscope);
void putVar(CUPState *state, size_t name, CVariable var);

size_t scope_enter(CUPState *state);
void scope_leave(CUPState *state);

const char *get_exe_path(void);
void *allocMemory(size_t size);
int samefile(const char *a, const char *b);

#endif /* CUP_H */