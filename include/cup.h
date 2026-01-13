#include "libcup.h"

/* ========================================================================== */
/*                           CONFIGURATION                                    */
/* ========================================================================== */

#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

/* Error codes */
#define CUP_ERROR_FORMAT 3
#define CUP_ERROR_MEM 2
#define FILE_NOT_FOUND -2
#define CUP_FILE_NOT_RECOGNIZED -3

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

/* ========================================================================== */
/*                           DYNAMIC VECTORS                                  */
/* ========================================================================== */

/** Dynamic array container */
typedef struct {
  size_t size;     /**< Current size in bytes */
  size_t capacity; /**< Allocated capacity in bytes */
  void *data;      /**< Pointer to data */
} CVector;

/* Vector operations */
void vector_push(CVector *vec, const void *data, size_t size,
                 CUPReallocFunc *realloc);
void push_vector(CVector *dest, CVector src, CUPReallocFunc *realloc);
void vector_fpush(CVector *vec, const void *data, size_t size,
                  CUPReallocFunc *realloc);
void fpush_vector(CVector *dest, CVector src, CUPReallocFunc *realloc);

/* ========================================================================== */
/*                           TYPE SYSTEM                                      */
/* ========================================================================== */

/** Number type flags */
enum {
  CTypeN_SIZE = 0,  /**< Size type (platform-dependent) */
  CTypeN_UINT8 = 1, /**< 8-bit unsigned integer */
  CTypeN_INT8 = 2   /**< 8-bit signed integer */
};
typedef uint8_t CTypeNType;

/** Number type definition */
typedef struct {
  CTypeNType flags;
} CTypeNumber;

/** Function type definition */
typedef struct {
  const CType *return_type; /**< Function return type */
  const CType *args;        /**< Function argument types */
} CTypeFunc;

/** Pointer type definition */
typedef struct {
  const CType *ptrtype; /**< Pointed-to type */
} CTypePointer;

/** Array type definition */
typedef struct {
  size_t count;         /**< Number of elements */
  const CType *ptrtype; /**< Element type */
} CTypeArray;

/** Complex/tuple type definition */
typedef struct {
  size_t count;         /**< Number of fields */
  const CType *types[]; /**< Field types */
} CTypeComplex;

/** Type categories */
enum {
  R_VOID = 0,
  R_COMPLEX = 1,
  R_NUMBER = 2,
  R_DOUBLE = 3,
  R_POINTER = 4,
  R_ARRAY = 5,
  R_FUNCTION = 6
};
typedef uint8_t CTypeR;

/** Type structure */
struct CType {
  CTypeR type; /**< Type discriminator */
  uint8_t data[sizeof(CTypeFunc)];
};

/** Type list node for type interning */
typedef struct CTypeList {
  struct CTypeList *next;
  CType type;
} CTypeList;

/* ========================================================================== */
/*                           VARIABLES & SCOPE                                */
/* ========================================================================== */

/** Variable definition */
typedef struct {
  const CType *type; /**< Variable type */
  size_t name;       /**< Offset into name table */
  size_t value;      /**< Variable value/address */
} CVariable;

/* ========================================================================== */
/*                           AST NODES                                        */
/* ========================================================================== */

/** AST node union */
typedef union {
  Token token;
  CTType type;
  Loc loc;
  size_t value;
  double dvalue;
} Node;

/** Node array for AST traversal */
typedef struct {
  Node *nodes;  /**< Array of nodes */
  size_t count; /**< Total node count */
  size_t pos;   /**< Current position */
} Nodes;

/* ========================================================================== */
/*                           COMPILER STATE                                   */
/* ========================================================================== */

struct CUPState {
  /* Memory management */
  CUPReallocFunc *reallocator;
  CUPLogFunc *error_func;

  /* Source input */
  const char *input_stream;

  /* Parser state - uses union for efficient storage */
  union {
    CVariable *variable;
    Token token;
    CTType type;
    Loc loc;
    struct {
      Node node;
      union {
        size_t value;
        Node node2;
      };
    };
  };

  /* Symbol table */
  uint8_t *names;   /**< String interning table */
  uint8_t *data;    /**< Additional data storage */
  size_t data_size; /**< Size of data storage */

  /* Type system */
  CTypeList *types; /**< Type interning list */

  /* Variable and memory management */
  CVector vars;  /**< Variable definitions */
  CVector pages; /**< JIT memory pages */

  /* Reserved for future use */
  size_t ssss;
};

/* Ensure consistent memory layout */
_Static_assert(sizeof(CUPState) == 128, "CUPState size must be 128 bytes");
