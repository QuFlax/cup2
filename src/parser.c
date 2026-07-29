#include "../include/cup.h"
//#include <threads.h>
#include <sys/stat.h>
#include <sys/mman.h>

//#include <dlfcn.h>
//#include <libelf.h>

#define DEBUG_LOG 0

#ifndef CUPMAXTYPES
#define CUPMAXTYPES 256
#endif

#define pushNode(nodes, node) \
  assert(node);\
  vector_fpushT(nodes.vec, node)
#define pushNode2(nodes, node2) \
  assert(node2);\
  vector_fpushT(nodes.vec, node2)

void* cup_var_value(CVariable* var) {
  if (var == NULL)
    return NULL;
  return (void*)var->value;
}
const CUPType* cup_var_type(CUPState* state, const CVariable* var) {
  if (state == NULL || var == NULL)
    return NULL;
  if (var->type == NULL)
    return NULL;
  return var->type;
}

Nodes primary(CUPState *state, uint8_t mpower);

Nodes statement(CUPState *state) {
  Nodes n = primary(state, CUPDEFPOWER);
  if (state->nodes.token == T_NL)
    skipSpaces(state);
  return n;
}

CVARS* getVars(CUPState *state, size_t name) {
  if (name == SIZE_MAX) return NULL;
  return map_get(&state->symmap, name, sizeof(CVARS), SIZE_MAX);
}

CVariable* getVarScoped(CUPState *state, size_t name, size_t maxscope) {
  assert(maxscope <= CVARS_MAX);
  CVARS* vars = getVars(state, name);
  if (!vars) return NULL;
  for (size_t s = maxscope; s-- > 0;) {
    if (vars->vars[s].scope != 0)
      return &vars->vars[s];
  }
  return NULL;
}

// Names
#define DEF(name) #name,
const char *token_names[] = {
#include "token.h"
};
#undef DEF

#define expectAndNext(ntype)                                                   \
  do {                                                                         \
    if (state->nodes.token != ntype) {                                                \
      cup_errorf("Expected " #ntype ", but got %s", token_names[state->nodes.token]); \
      exit(1);                                                                 \
    }                                                                          \
    getToken(state);                                                           \
  } while (0)

/*
CUP_TYPE commonType(CUPType left, CUPType right) {
  if (left.realtype == right.realtype) {
    if (left.realtype == CUP_TYPE_STRUCT) {
      if (left.size != right.size) {
        cup_error("commonType: struct size mismatch");
        return CUP_TYPE_VOID;
      }
      for (size_t i = 0; left.elements[i] && right.elements[i]; i++) {
        if (left.elements[i] != right.elements[i]) {
          cup_error("commonType: struct element type mismatch");
          return CUP_TYPE_VOID;
        }
      }
    }
    if (left.realtype == CUP_TYPE_FUNCTION) {
      if (left.size != right.size) {
        cup_error("commonType: function size mismatch");
        return CUP_TYPE_VOID;
      }
      for (size_t i = 0; left.elements[i] && right.elements[i]; i++) {
        CUP_TYPE t = commonType(*left.elements[i], *right.elements[i]);
        if (t != CUP_TYPECOUNT) {
          cup_error("commonType function element type mismatch");
          return t;
        }
      }
    }
    return CUP_TYPECOUNT;
  }
  if (left.realtype == CUP_TYPE_VOID)
    return right.realtype;
  if (right.realtype == CUP_TYPE_VOID)
    return left.realtype;
  if ((left.realtype == CUP_TYPE_FLOAT && right.realtype == CUP_TYPE_DOUBLE) ||
      (left.realtype == CUP_TYPE_DOUBLE && right.realtype == CUP_TYPE_FLOAT))
    return CUP_TYPE_DOUBLE;
  if ((left.realtype == CUP_TYPE_SINT64 && right.realtype == CUP_TYPE_UINT64) ||
      (left.realtype == CUP_TYPE_UINT64 && right.realtype == CUP_TYPE_SINT64))
    return CUP_TYPE_UINT64;
  if ((left.realtype == CUP_TYPE_SINT32 && right.realtype == CUP_TYPE_UINT32) ||
      (left.realtype == CUP_TYPE_UINT32 && right.realtype == CUP_TYPE_SINT32))
    return CUP_TYPE_UINT32;
  if ((left.realtype == CUP_TYPE_SINT16 && right.realtype == CUP_TYPE_UINT16) ||
      (left.realtype == CUP_TYPE_UINT16 && right.realtype == CUP_TYPE_SINT16))
    return CUP_TYPE_UINT16;
  if ((left.realtype == CUP_TYPE_SINT8 && right.realtype == CUP_TYPE_UINT8) ||
      (left.realtype == CUP_TYPE_UINT8 && right.realtype == CUP_TYPE_SINT8))
    return CUP_TYPE_UINT8;
  if ((left.realtype == CUP_TYPE_SINT64 && right.realtype == CUP_TYPE_SINT32) ||
      (left.realtype == CUP_TYPE_SINT32 && right.realtype == CUP_TYPE_SINT64))
    return CUP_TYPE_SINT64;
  if ((left.realtype == CUP_TYPE_SINT64 && right.realtype == CUP_TYPE_SINT16) ||
      (left.realtype == CUP_TYPE_SINT16 && right.realtype == CUP_TYPE_SINT64))
    return CUP_TYPE_SINT64;
  if ((left.realtype == CUP_TYPE_SINT64 && right.realtype == CUP_TYPE_SINT8) ||
      (left.realtype == CUP_TYPE_SINT8 && right.realtype == CUP_TYPE_SINT64))
    return CUP_TYPE_SINT64;
  if ((left.realtype == CUP_TYPE_SINT32 && right.realtype == CUP_TYPE_SINT16) ||
      (left.realtype == CUP_TYPE_SINT16 && right.realtype == CUP_TYPE_SINT32))
    return CUP_TYPE_SINT32;
  if ((left.realtype == CUP_TYPE_SINT32 && right.realtype == CUP_TYPE_SINT8) ||
      (left.realtype == CUP_TYPE_SINT8 && right.realtype == CUP_TYPE_SINT32))
    return CUP_TYPE_SINT32;
  if ((left.realtype == CUP_TYPE_SINT16 && right.realtype == CUP_TYPE_SINT8) ||
      (left.realtype == CUP_TYPE_SINT8 && right.realtype == CUP_TYPE_SINT16))
    return CUP_TYPE_SINT16;
  return CUP_TYPECOUNT;
}
*/

//const CUPType *DTypes[UINT8_MAX];
//const CUPType **DTTypes[UINT8_MAX];
//static size_t DRType = 0;

#if FRACLIBM
#include <math.h> // for pow, floor, log10
static double frac(uint64_t frac_part) {
  if (frac_part == 0) return 0.0;
  return (frac_part / pow(10, floor(log10(frac_part) + 1)));
}
#else
static double frac(uint64_t frac_part) {
  static const double pow10table[] = {
        1e0, 1e1, 1e2, 1e3, 1e4,
        1e5, 1e6, 1e7, 1e8, 1e9,
        1e10, 1e11, 1e12, 1e13, 1e14,
        1e15, 1e16, 1e17, 1e18, 1e19, 1e20
  };

  if (frac_part == 0) return 0.0;

  unsigned digits = 0;
  for (uint64_t t = frac_part; t; t /= 10)
    ++digits;

  return (double)frac_part / pow10table[digits];
}
#endif

uint8_t getPower(CTType t) {
  switch (t) {
  default:
    return UINT8_MAX;

  case T_EOF:
    return 0;
  case T_IF:
  case T_RETURN:
  case T_NL:
  case T_OCB:
  case T_CRB:
  case T_CCB:
  case T_CSB:
    return 1;

  case T_COMMA:
    return 2;

  case T_EQ:
  case T_ADDEQ:
  case T_SUBEQ:
  case T_MULEQ:
  case T_DIVEQ:
  case T_MODEQ:
  case T_ANDEQ:
  case T_OREQ:
  case T_XOREQ:
    return 3;

  case T_OR:
  case T_AND:
  case T_XOR:
    return 6;

  case T_EQEQ:
  case T_NOTEQ:
    return 8;

  case T_LESSEQ:
  case T_GREATEQ:
  case T_LESS:
  case T_GREAT:
    return 10;

  case T_ADD:
  case T_SUB:
    return 12;

  case T_MUL:
  case T_DIV:
  case T_MOD:
    return 14;

  case T_DOT:
    return 16;
  case T_CALL:
    return 18;
  case T_OSB:
    return CUPDEFPOWER;
  }
}

Nodes primary(CUPState *state, uint8_t mpower) {
  Nodes left = {};
  switch (state->nodes.token) {
  case T_IMPORT: {
    getToken(state);
    if (state->nodes.token != T_STRING) {
      cup_errorf("Expected Module name[string] but got '%s'", token_names[state->nodes.token]);
      return left;
    }
    importModule(state);
    getToken(state);
    return left;
  }
  case T_EXTERNAL: {
    getToken(state);
    if (state->nodes.token != T_STRING) {
      cup_errorf("Expected .DLL/.SO name[string] but got '%s'", token_names[state->nodes.token]);
      return left;
    }
    externalModule(state);
    getToken(state);
    while (state->nodes.token == T_IDENTIFIER) {
      size_t name_idx = state->nodes.value;
      getToken(state);
      if (state->nodes.token != T_STRING) {
        cup_errorf("external: expected signature string after name, got '%s'", token_names[state->nodes.token]);
        return left;
      }
      size_t sig_idx = state->nodes.value;
      externalSymbol(state, name_idx, sig_idx);
      getToken(state);
    }
    return left;
  }
  case T_AT: { // function
    Node n = state->nodes.node;
    getToken(state);

    if (state->nodes.token == T_AT) {
      getToken(state);
      cup_error("function macro for 'T_AT' not implemented yet");
      return left;
    }
    if (state->nodes.token != T_IDENTIFIER) {
      cup_errorf("Expected T_IDENTIFIER, but got %s", "");
      return left;
    }
    vector_pushT(left.vec, n);           // T_AT
    vector_pushT(left.vec, state->nodes.node); // T_IDENTIFIER
    size_t name = state->nodes.value;
    CVariable* vvv = getVarScoped(state, name, state->scope);
    char tname[256];
    if (vvv) {
      if (vvv->type) {
        cup_type_snname(tname, sizeof(tname), vvv->type);
        printf("defType_ function args type = %s\n", tname);
        cup_error("redefined variable function");
        exit(1);
      }
    } else {
      CVARS vars = {(CVariable){NULL, 0, state->scope}};
      map_put(&state->symmap, name, &vars, sizeof(CVARS), SIZE_MAX);
    }
    vector_pushT(left.vec, name);
    getToken(state);

    //CVector vars = {cup_malloc(state->vars_size), state->vars_size, state->vars_size};
    //state->vars_size = 0;

    Node2 comma = state->nodes;
    state->scope++;
    Nodes args_nodes = statement(state);
    size_t argc = 1;
    if (args_nodes.data && args_nodes.data->token == T_COMMA)
      argc = args_nodes.data[1].value;
    else {
      comma.token = T_COMMA;
      comma.value = 1;
      vector_pushT(left.vec, comma);
    }
    
    const CUPType* DTypes[UINT8_MAX] = {};
    /* Function layout: [return, arg0, ..., argN-1, void-sentinel].
     * NULL argument slots mean unknown/any. */
    DTypes[argc + 1] = &cup_type_void;
    CUPType fn_t = (CUPType){ sizeof(void *), (uint16_t)sizeof(void *), CUP_TYPE_FUNCTION, { DTypes } };
    const CUPType *type = cup_type_put(state, fn_t);
    cup_type_snname(tname, sizeof(tname), type);
    printf("defType function type = %s\n", tname);
    getVarScoped(state, name, state->scope)->type = type;
    
    push_vector(&left.vec, args_nodes.vec);

    Nodes block = statement(state);
    push_vector(&left.vec, block.vec);
    state->scope--;
    return left;
  }
  case T_BREAK:
  case T_CONTINUE: {
    vector_pushT(left.vec, state->nodes.node);
    getToken(state);
    break;
  }
  case T_WHILE: {
    vector_pushT(left.vec, state->nodes.node);
    skipSpaces(state);
    push_vector(&left.vec, statement(state).vec);
    push_vector(&left.vec, statement(state).vec);
    return left;
  }
  case T_RETURN: {
    vector_pushT(left.vec, state->nodes.node);
    skipSpaces(state);
    push_vector(&left.vec, statement(state).vec);
    break;
  }
  case T_SUB: {
    state->nodes.token = N_UNARY;
    vector_pushT(left.vec, state->nodes.node);
    getToken(state);
    push_vector(&left.vec, primary(state, CUPDEFPOWER).vec);
    break;
  }
  case T_NOT: {
    vector_pushT(left.vec, state->nodes.node);
    getToken(state);
    push_vector(&left.vec, primary(state, CUPDEFPOWER).vec);
    break;
  }
  case T_IDENTIFIER: {
    vector_pushT(left.vec, state->nodes.node);
    size_t name = state->nodes.value;
    CVariable* v = getVarScoped(state, name, state->scope);
    if (v) {
      // TODO: check is it conflict name and type
      //const char *s = "cup_type_name(((CVariable *)state->vars.data)[v].type)";
      //cup_errorf("TODO: check is it conflict name and type %s", cup_type_name(state->vars[v].type));
    } else {
      //symmap_put(&state->symmap, name, NULL, 0, state->scope);
      CVARS vars = {(CVariable){NULL, 0, state->scope}};
      map_put(&state->symmap, name, &vars, sizeof(CVARS), SIZE_MAX);
    }
    vector_pushT(left.vec, name);
    getToken(state);
    if (state->nodes.token == T_OSB) {
      cup_error("Subscripts not implemented");
      exit(1);
    }
    break;
  }
  case T_STRING: {
    vector_pushT(left.vec, state->nodes);
    getToken(state);
    break;
  }
  case T_NUMBER: {
    Node2 n = state->nodes;
    getToken(state);
    if (state->nodes.token == T_DOT) {
      n.token = T_DOUBLE;
      n.dvalue = (double)n.value;
      getToken(state);
      if (state->nodes.token == T_NUMBER) {
        n.dvalue += frac(state->nodes.value);
        getToken(state);
      }
    }
    vector_pushT(left.vec, n);
    break;
  }
  case T_CALL: { // '('
    skipSpaces(state);
    if (state->nodes.token != T_CRB) {
      left = statement(state);
      expectAndNext(T_CRB);
    }
    else {
      Node2 n = state->nodes;
      n.token = T_COMMA;
      vector_pushT(left.vec, n);
      getToken(state); // T_CRB;
    }
    break;
  }
  case T_OCB: { // '{'
    state->nodes.token = N_BLOCK;
    vector_pushT(left.vec, state->nodes);
    skipSpaces(state);
    state->scope++;
    while (state->nodes.token != T_CCB) {
      {
        Nodes block = statement(state);
        if (block.data) left.data[1].value++;
        push_vector(&left.vec, block.vec);
      }
    }
    getToken(state); // T_CCB
    state->scope--;
    return left;
  }
  case T_OSB: { // '['
    state->nodes.token = N_ARRAY;
    vector_pushT(left.vec, state->nodes);
    skipSpaces(state);
    if (state->nodes.token != T_CSB) {
      Nodes body = primary(state, CUPDEFPOWER);
      if (body.data) left.data[1].value++;
      push_vector(&left.vec, body.vec);
    }
    //if (state->type == T_NL)
    //  skipSpaces(state);
    expectAndNext(T_CSB);
    break;
  }
  case T_IF: {
    vector_pushT(left.vec, state->nodes.node);
    skipSpaces(state);
    push_vector(&left.vec, statement(state).vec);
    push_vector(&left.vec, statement(state).vec);
    if (state->nodes.token == T_ELSE) {
      vector_fpushT(left.vec, state->nodes.node);
      skipSpaces(state);
      push_vector(&left.vec, statement(state).vec);
    }
    break;
  }
  case T_VARG: {
    vector_pushT(left.vec, state->nodes.node);
    getToken(state);
    break;
  }
  default: {
    cup_errorf("primary: Unexpected token '%s'", token_names[state->nodes.token]);
    exit(1);
    break;
  }
  }
  //if (state->type == T_EOF) {
  //  exit(1);
  //}

  while (1) {
    Node2 opnode = state->nodes;
    const uint8_t power = getPower(opnode.token);
#if DEBUG_LOG
    printf("power: %d > %s %d\n", mpower, token_names[opnode.token], power);
#endif
    if (power == UINT8_MAX) {
      cup_errorf("primary op1: Unexpected token %s", token_names[opnode.token]);
      exit(1);
    }
    if (power < mpower)
      return left;
    skipSpaces(state);
    Nodes right = {};
    int norb = opnode.token != T_CALL;
    if (state->nodes.token != T_CRB || norb)
      right = primary(state, norb ? power : CUPDEFPOWER);
    else {
      printf("N_COMMA 0\n");
      Node2 tempn = opnode;
      tempn.token = T_COMMA;
      tempn.value = 0;
      vector_pushT(right.vec, tempn);
    }

    switch (opnode.token) {
    case T_CALL: {
      if (state->nodes.token != T_CRB) {
        cup_errorf("Expected T_CRB, but got %s", token_names[state->nodes.token]);
        exit(1);
      }
      getToken(state);
      //expectAndNext(T_CRB);
      opnode.token = T_CALL;
      vector_fpushT(left.vec, opnode.node);
      Node *nptr = right.data;
      //cup_errorf("CALL: %s", token_names[nptr->type]);
      if (nptr->token != T_COMMA) {
        opnode.token = T_COMMA;
        opnode.value = 1;
        vector_pushT(left.vec, opnode);
      }
      break;
    }
    case T_EQ: {
      //defType(state, left.data,
        //defType(state, right.data, (CUPType*)0)
      //);
      vector_fpushT(right.vec, opnode.node);
      fpush_vector(&left.vec, right.vec);
      continue;
    }
    case T_COMMA: {
      Node n = {.value = 2};
      if (right.data->token == T_COMMA)
        n.value = right.data[1].value + 1;
      vector_fpushT(left.vec, n);
      vector_fpushT(left.vec, opnode.node);
      if (right.data->token == T_COMMA)
        vector_fpop(&right.vec, sizeof(opnode));
      break;
    }
    case T_ADD:
    case T_EQEQ:
    case T_MUL:
    case T_LESSEQ:
    case T_SUB: {
      vector_fpushT(left.vec, opnode.node);
      break;
    }
    default:
      cup_errorf("primary op: Unexpected token '%s'", token_names[opnode.token]);
      exit(1);
    }
    push_vector(&left.vec, right.vec);
  }
}