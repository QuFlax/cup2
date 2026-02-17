#ifndef CUP_H
#define CUP_H

#include "libcup.h"
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>

#define free(x) cup_realloc((x), 0)
#define malloc(x) cup_realloc(NULL, x)
#define callocT(v, T) do {v = malloc(sizeof(T)); memset(v, 0, sizeof(T));} while (0)

const char *cup_vsprintf(const char *format, va_list args);
const char *cup_sprintf(const char *format, ...);

#if 0
#define cup_warnf(state, f, ...)                                               \
  do {                                                                         \
    char *str = cup_sprintf((f), __VA_ARGS__);           \
    cup_log(CUP_Level_WARN, str);                                               \
    free(str);                                              \
  } while (0)
#define cup_warn(state, msg) cup_log(CUP_Level_WARN, msg);

#define cup_errorf(state, f, ...)                                              \
  do {                                                                         \
    char *str = cup_sprintf((f), __VA_ARGS__);           \
    cup_log(CUP_Level_ERRO, str);                                               \
    free(str);                                              \
  } while (0)
#define cup_error(state, msg) cup_log(CUP_Level_ERRO, msg);
#else
#endif // CUP_ERROR_H

/* ========================================================================== */
/*                           DYNAMIC VECTORS                                  */
/* ========================================================================== */

/** Dynamic array container */
typedef struct CVector {
  void *data;      /**< Pointer to data */
  size_t count;
  size_t capacity; /**< Allocated capacity in bytes */
} CVector;

void vector_push(CVector *vec, const void *data, size_t size);
void push_vector(CVector *dest, CVector src);
void vector_fpush(CVector *vec, const void *data, size_t size);
void fpush_vector(CVector *dest, CVector src);

#define vector_pushT(left, data)  vector_push( (left),&(data),sizeof(data))
#define vector_fpushT(left, data) vector_fpush((left),&(data),sizeof(data))

/* ========================================================================== */
/*                           HASH TABLE                                       */
/* ========================================================================== */

/* typedef struct _Entry {
    char *key;
    void* value;
    struct _Entry *next;
} Entry;
*/

/* typedef struct HashMap {
    Entry **buckets;
    size_t capacity;
    size_t size;
} HashMap;
*/

typedef struct VectorMap {
    void *data;
    size_t *indexs;
    size_t capacity;
    size_t count;
} VectorMap;

static unsigned long hash(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

void hashmap_resize(VectorMap *map);
void* hashmap_get(VectorMap map, const char *key);
void hashmap_put(VectorMap *map, const char *key, void* value);
void hashmap_free(VectorMap *map);

/* ========================================================================== */
/*                           VARIABLES & SCOPE                                */
/* ========================================================================== */

/** Variable definition */
typedef struct CVariable {
  const char *type;  /**< Variable type */
  size_t name;       /**< Offset into name table */
  size_t value;      /**< Variable value/address */
} CVariable;

/* ========================================================================== */
/*                           AST NODES                                        */
/* ========================================================================== */

enum {
  T_EOF = '\0',
  N_END = T_EOF,

  T_NL = '\n',
  N_NEWLINE = T_NL,
  T_CATNL = '\\',

  T_RETURN = 'r',
  N_RETURN = T_RETURN,
  T_CONTINUE = 'n',
  N_CONTINUE = T_CONTINUE,
  T_BREAK = 'b',
  N_BREAK = T_BREAK,
  T_IF = 'i',
  N_IF = T_IF,
  T_ELSE = 'e',
  N_IFELSE = T_ELSE,
  T_FOR = 'f',
  N_FOR = T_FOR,
  T_WHILE = 'w',
  N_WHILE = T_WHILE,

  T_IDENTIFIER = '0',
  N_VARIABLE = T_IDENTIFIER,
  T_NUMBER = '1',
  N_NUMBER = T_NUMBER,
  T_DOUBLE = '2',
  N_DOUBLE = T_DOUBLE,
  T_STRING = '3',
  N_STRING = T_STRING,
  T_MSTRING = '4',
  N_MSTRING = T_MSTRING,
  T_RANGE = '5',
  N_RANGE = T_RANGE,

  T_ADD = '+',
  N_ADD = T_ADD,
  T_SUB = '-',
  N_SUB = T_SUB,
  T_MUL = '*',
  N_MUL = T_MUL,
  T_DIV = '/',
  N_DIV = T_DIV,
  T_MOD = '%',
  N_MOD = T_MOD,
  T_AND = '&',
  N_AND = T_AND,
  T_OR = '|',
  N_OR = T_OR,
  T_XOR = '^',
  N_XOR = T_XOR,
  T_LESS = '<',
  N_LESS = T_LESS,
  T_GREAT = '>',
  N_GREAT = T_GREAT,
  T_NOT = '!',
  N_NOT = T_NOT,
  T_EQ = '=',
  N_ASSIGN = T_EQ,

  T_ORB = '(',
  T_CRB = ')',
  T_OCB = '{',
  T_CCB = '}',
  T_OSB = '[',
  T_CSB = ']',

  N_CALL = 'c',
  N_UNARY = 'u',
  N_BLOCK = 'l',
  N_OBJECT = 'o',
  N_SUBSCRIPT = 's',
  N_ARRAY = 'a',
  T_DOT = '.',
  N_MEMBER = T_DOT,
  T_COMMA = ',',
  N_COMMA = T_COMMA,
  T_COLON = ':',
  T_SCOLON = ';',

  T_IMPORT = '#',
  T_EXTERNAL = '~',

  T_ASK = '?',
  T_AT = '@',
  N_FUNCTION = T_AT,
  T_THIS = '$',
  T_VARG = 'V',
  N_VARG = T_VARG,

  T_ADDEQ = 'A',
  N_ADDASSIGN = T_ADDEQ,
  T_SUBEQ = 'S',
  N_SUBASSIGN = T_SUBEQ,
  T_MULEQ = 'M',
  N_MULASSIGN = T_MULEQ,
  T_DIVEQ = 'D',
  N_DIVASSIGN = T_DIVEQ,
  T_MODEQ = 'O',
  N_MODASSIGN = T_MODEQ,
  T_ANDEQ = 'N',
  N_ANDASSIGN = T_ANDEQ,
  T_OREQ = 'R',
  N_ORASSIGN = T_OREQ,
  T_XOREQ = 'X',
  N_XORASSIGN = T_XOREQ,
  T_LESSEQ = 'L',
  N_LESSEQ = T_LESSEQ,
  T_GREATEQ = 'G',
  N_GREATEQ = T_GREATEQ,
  T_NOTEQ = 'T',
  N_NOTEQ = T_NOTEQ,
  T_EQEQ = 'E',
  N_EQEQ = T_EQEQ,
};
typedef uint16_t CTType;

/** Source code location for error reporting */
typedef struct {
  CTType type;   /**< Token type at this location */
  uint16_t col;  /**< Column number (1-indexed) */
  uint32_t line; /**< Line number (1-indexed) */
} Loc;

/** Token with location information */
typedef union {
  CTType type;
  Loc loc;
} Token;

const uint8_t* getdata(CUPState* state, const char* str, size_t len);
const char *getString(CUPState *state, size_t index);
uint8_t *idToken(CUPState *state, const char *str, size_t len);

void getToken(CUPState *state);
void skipSpaces(CUPState *state);

/** AST node union */
typedef union {
  Token token;
  CTType type;
  Loc loc;
  size_t value;
  double dvalue;
} Node;

typedef struct {
  Node node;
  union {
    Node node2;
    size_t value;
  };
} Node2;

typedef struct {
  Node *nodes;
  size_t count;  
  size_t pos;
} Nodes;

typedef struct {
  size_t *var;
  size_t ptr;
} CRealloc;

typedef struct {
  CRealloc* reallocs;
  size_t count;
  size_t capacity;
} CReallocs;

typedef struct {
  uint8_t *data;
  size_t size;
  size_t capacity;
  CRealloc *reallocs;
  size_t r_count;
} CBuffer_;

/* ========================================================================== */
/*                           COMPILER STATE                                   */
/* ========================================================================== */

typedef struct _CUPCompiledModule {
  const char* path;
  uint8_t* code;
  size_t size;
  size_t capacity;
  CReallocs reallocs;
  struct _CUPCompiledModule* next;
} CUPCompiledModule;

struct CUPModule {
  const char* path;
  uint8_t* code;
  size_t size;
  size_t capacity;
  Nodes nodes;
  CUPModule* next;
};

static_assert(sizeof(CUPModule) == (8 * sizeof(size_t)), "CUPModule size must be 8 bytes");

typedef struct CUPVars {
  CVariable *data;      /**< Pointer to data */
  size_t size;     /**< Current size in bytes */
  size_t capacity; /**< Allocated capacity in bytes */
} CUPVars;

typedef struct CUPTypes {
  size_t* indexs;
  CUPType* types;
  size_t count;
  size_t maxlen;
} CUPTypes;


struct CUPState {
  /* Source input */
  const char *input_stream;
  const char *priv_stream;

  /* Parser state - uses union for efficient storage */
  union {
    CVariable *variable;
    Token token;
    CTType type;
    Loc loc;
    Node node;
    Node2 nodes;
  };

  /* Symbol table */
  uint8_t *names;   /**< String interning table */
  uint8_t *data;    /**< Additional data storage */
  size_t data_size; /**< Size of data storage */
  CUPModule* module;

  /* Variable and memory management */
  CUPVars vars;  /**< Variable definitions */

  VectorMap types;

  CUPTarget* target;
};

/* Ensure consistent memory layout */
static_assert(sizeof(CUPState) == 128, "CUPState size must be 128 bytes");

#endif /* CUP_H */