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
#define TEMP_CAP 2048

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

#define CMAP_INIT_CAPACITY 16   /* must be a power of two */
#define CMAP_LOAD_NUM 3   /* grow when count*4 > capacity*3  (75 %) */
#define CMAP_LOAD_DEN 4

size_t map_hash(size_t key);

typedef struct CMap {
  void   *slots; // {key, dist, value}
  size_t count;    /* live entries */
  size_t capacity; /* slot count, always a power of two */
  //size_t k_null;
  size_t v_size;
} CMap;

typedef struct CMapItem {
  size_t  key;
  size_t  dist;
  uint8_t data[];
} CMapItem;

typedef struct {
  CUPState *state;
  void *user_ctx;
  cup_list_symbols_callback cb;
} SymbolIterCtx;
typedef void (*map_iter_callback)(void *ctx, const CMapItem *item);

//void map_init(CMap *m, size_t k_null, size_t v_size, void* v_default);
//void map_free(CMap *m);
//CMapItem *map_get(const CMap *m, size_t key, CMapItem *prev, size_t k_null);
//CMapItem *map_put(CMap *m, size_t key, void *value, size_t k_null);
//void map_iter(const CMap *m, void *ctx, map_iter_callback cb, size_t k_null);
/* Remove an entry (used when popping scopes). */
//void       map_del(CMap *m, size_t name_idx, size_t v_size, size_t k_null);

/* ========================================================================== */
/*                           TOKEN / NODE TYPES                               */
/* ========================================================================== */

#define DEF(name) name,
enum CTType_ {
#include "token.h"
    COUNT
};
#undef DEF

static_assert(COUNT < UINT16_MAX, "TOO MANY TOKENS");

#define N_VARIABLE T_IDENTIFIER
#define N_FUNCTION T_AT

extern const char *token_names[];

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
        size_t      value;
        double      dvalue;
        const char* svalue;
    };
} Node2;

struct CVariable {
    const char* name;
    const CUPType* typee;
    size_t value;         /**< Variable value / symbol address */
    size_t scope;
};

CVecT(CVariable);

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

const char* idToken(CUPState *state, const char *str, size_t len);

void getToken  (CUPState *state, int space);
//void skipSpaces(CUPState *state);

int resolve(CUPState *state, Nodes* nodes);

/* ========================================================================== */
/*                           CODE EMISSION                                    */
/* ========================================================================== */

typedef struct CUPModuleList {
    CUPModule            module;
    struct CUPModuleList *next;
} CUPModuleList;

//typedef struct CExternal { void *handle; } CExternal;
//CVecT(CExternal);

/* ========================================================================== */
/*                           COMPILER STATE                                   */
/* ========================================================================== */

//struct CUPGenContext {
//    CUPState* state;
//    Node node[3];
//};

struct CUPState {
    /* Source input */
    const char *input_stream;
    const char *priv_stream;

    /* Current parser token / node */
    union {
        Node2 nodes;
        CVariable *variable;
    };
    size_ts scopes;
    size_t scope;

    /* String intern table.
     * names_base: original allocation pointer (needed for safe getString walk
     *             and for cup_delete to free exactly what was allocated).
     * names:      pointer to one past the last interned byte (grows upward). */
    //uint8_t *names;

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
    //CMap symmap;
    CVariables vars;
    size_t* vars_map;

    CTypes types;
    CUPTarget *target;
    const char *bytecode_path;
    const char *code_path;
    Nodes ast;
    //CVector vector_;
    
    size_t l1, l2, l3, l4, l5;
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
CVariable *varLookup(CUPState *state, const char* name, size_t scope);
void putVar(CUPState *state, const char* name, const CUPType* type, size_t value, size_t scope);

//const CUPType    *cup_type_get(CUPState *state, const CUPType type);
//const CUPType    *cup_type(CUPState *state, const CUPType type);
const CUPType    *cup_type_parse(CUPState* state, const char* sig);

size_t scope_enter(CUPState *state);
void scope_leave(CUPState *state);

const char *get_exe_path(void);
void *allocMemory(size_t size);
int samefile(const char *a, const char *b);

#endif /* CUP_H */