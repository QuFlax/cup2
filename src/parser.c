#include "../include/cup.h"
//#include <threads.h>
#include <math.h> // for pow, floor, log10
#include <sys/stat.h>
#include <sys/mman.h>

//#include <dlfcn.h>
//#include <libelf.h>

#define DEBUG_LOG 0

#ifndef CUPMAXTYPES
#define CUPMAXTYPES 256
#endif


void* cup_var_value(CVariable* var) {
  if (var == NULL)
    return NULL;
  return (void*)var->value;
}
/*
const char*    cup_var_name(CUPState* state, CVariable* var) {
  if (state == NULL || var == NULL)
    return NULL;
  return getString(state, var->name);
}
const CUPType* cup_var_type(CUPState* state, const CVariable* var) {
  if (state == NULL || var == NULL)
    return NULL;
  if (var->type == NULL)
    return NULL;
  return var->type;
}
*/

Nodes primary(CUPState *state, uint8_t mpower);

Nodes statement(CUPState *state) {
  Nodes n = primary(state, CUPDEFPOWER);
  if (state->type == T_NL)
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
    if (state->type != ntype) {                                                \
      cup_errorf("Expected " #ntype ", but got %s", token_names[state->type]); \
      exit(1);                                                                 \
    }                                                                          \
    getToken(state);                                                           \
  } while (0)


int cahngeType(const CUPType* left, const CUPType* right) {
  if (!left || !right) return 0;
  if (left->realtype == right->realtype) {
    if (left->realtype == CUP_TYPE_STRUCT)
      return INT_MAX;
    if (left->realtype == CUP_TYPE_FUNCTION) {
      if (left->size != right->size) {
        cup_error("cahngeType: function size mismatch");
        return INT_MAX;
      }
      for (size_t i = 0; left->elements[i] && right->elements[i]; i++) {
        int t = cahngeType(left->elements[i], right->elements[i]);
        if (t != 0) {
          cup_error("commonType function element type mismatch");
          return t;
        }
      }
    }
    return 0;
  }
  if (left->realtype == CUP_TYPE_VOID)
    return 1;
  if (right->realtype == CUP_TYPE_VOID)
    return -1;
  return 0;
}

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

const CUPType *DTypes[UINT8_MAX];
//const size_t Dindexs[UINT8_MAX];
//static size_t DRType = 0;


size_t defTypeEx(CUPState *state, const Node *nodes, size_t *pos, const CUPType** value) {
  //printf("DRType = %ld\n", DRType);
  //if (DRType) {
  //  DRType--;
  //}
  if (!nodes || !state) {
    cup_error("defType state or nodes are NULL");
    return 0;
  }
  assert(pos != NULL);

  Node n = nodes[(*pos)++];
  size_t nv = 0;
  //const CUPType *left = NULL, *right = NULL;

  switch (n.type) {
  case T_ADD: case T_SUB: case T_MUL: case T_DIV: case T_MOD:
  {
    const CUPType *left = NULL;
    nv = defTypeEx(state, nodes, pos, &left);
    if (nv == 0 || nv >= 2) {
      cup_error("defType left has multiple values");
      return 0;
    }
    const CUPType *right = NULL;
    nv = defTypeEx(state, nodes, pos, &right);
    if (nv == 0 || nv >= 2) {
      cup_error("defType right has multiple values");
      return 0;
    }
    //left = defTypeEx(state, nodes, pos, NULL);
    //right = defTypeEx(state, nodes, pos, NULL);

    if (left && right && left->realtype == right->realtype) {
      *value = left;
      return 1;
    }
    //DRType++;
    cup_error("defType left and right are not compariable");
    return 0;
  }
  case T_EQ:
  //case N_ADDASSIGN:
  //case N_SUBASSIGN:
  //case N_MULASSIGN:
  //case N_DIVASSIGN:
  {
    //right = defTypeEx(state, nodes, pos, NULL);
    nv = defTypeEx(state, nodes, pos, value);
    if (nv == 0 || nv >= 2) {
      cup_error("defType assign right has multiple values");
      return 0;
    }
    char tname[256];
    cup_type_snname(tname, sizeof(tname), *value);
    printf("defType assign right type = %s\n", tname);
    return defTypeEx(state, nodes, pos, value);
  }
  case T_CALL: {
    const CUPType *left = NULL;
    nv = defTypeEx(state, nodes, pos, &left);
    if (nv == 0 || nv >= 2) {
      cup_error("defType call left has multiple values");
      return 0;
    }
    if (left == NULL) {
      cup_error("defType call NULL");
      return 0;
    }
    if (left->realtype == CUP_TYPE_FUNCTION) {
      const CUPType *right = NULL;
      nv = defTypeEx(state, nodes, pos, &right);
      if (nv == 0) {
        cup_error("defType call right has multiple values");
        return 0;
      }
      if (right == NULL) {
        cup_error("defType call args is NULL");
        return 0;
      }
      if (nv >= 2) {
        for (size_t i = 0; right->elements[i]; i++) {
          const CUPType *arg_type = right->elements[i];
          int t = cahngeType(arg_type, left->elements[i + 1]);
          if (t == 0) continue;
          if (t == -1) {
            left->elements[i + 1] = arg_type;
            continue;
          }
          if (t == 1) {
            arg_type = left->elements[i + 1];
            continue;
          }
          {
            char tname[256];
            cup_type_snname(tname, sizeof(tname), arg_type);
            char tname2[256];
            cup_type_snname(tname2, sizeof(tname2), left->elements[i + 1]);
            cup_errorf("defType(%i) call arg %ld type mismatch: get %s expected %s", t, i, tname2, tname);
            return 0;
          }
        }
      }
      *value = *left->elements;
      return 1;
    }
    cup_error("defType call not Function");
    return 0;
  }
  case N_BLOCK: {
    nv = nodes[(*pos)++].value;
    for (size_t i = 0; i < nv; i++) {
      const CUPType *left = NULL;
      size_t nvi = defTypeEx(state, nodes, pos, &left);
      if (nvi == 0) {
        cup_error("defType N_BLOCK 0");
        return 0;
      }
    }
    *value = &cup_type_void;
    return 1;
  }
  case T_COMMA: {
    nv = nodes[(*pos)++].value;
    if (nv >= UINT8_MAX) {
      cup_error("defType comma has strange count");
      return 0;
    }
    memset(DTypes, 0, sizeof(DTypes));
    CUPType t = {0, 0, CUP_TYPE_STRUCT, DTypes};
    for (size_t i = 0; i < nv; i++) {
      size_t nvi = defTypeEx(state, nodes, pos, &DTypes[i]);
      if (nvi == 0 || nvi >= 2) {
        cup_error("defType comma has multiple values for element");
        return 0;
      }
      if (DTypes[i] == NULL) return 0; 
      if (t.alignment < DTypes[i]->alignment)
        t.alignment = DTypes[i]->alignment;
      t.size += DTypes[i]->size;
    }
    *value = cup_type_put(state, &t);
    return nv;
  }
  case T_NUMBER: {
    nv = nodes[(*pos)++].value; //TODO: 
    (void)nv;
    *value = &cup_type_int;
    return 1;
  }
  case T_DOUBLE: {
    nv = nodes[(*pos)++].value; //TODO: 
    (void)nv;
    *value = &cup_type_double;
    return 1;
  }
  case T_STRING:
  case T_MSTRING: {
    assert(0 && "String literals not implemented");
    nv = nodes[(*pos)++].value; //TODO: 
    (void)nv; //TODO: array with size
    *value = &cup_type_void;
    return 1;
  }
  case N_VARIABLE: {
    size_t v = nodes[(*pos)++].value;
    if (v == SIZE_MAX) {
      cup_error("defType var not found");
      return 0;
    }
    CVariable* var = getVarScoped(state, v, CVARS_MAX);
    if (*value == NULL) {
      *value = var->type;
      return 1;
    }
    if (var->type != *value) {
      if (var->type) {
        cup_error("defType var type has value");
      }
      var->type = *value;
    }
    return 1;
  }
  case N_FUNCTION: {
    const CUPType *left = NULL;
    nv = defTypeEx(state, nodes, pos, &left);
    if (nv == 0 || nv >= 2) {
      cup_error("defType function left has multiple values");
      return 0;
    }
    const CUPType *right = NULL;
    nv = defTypeEx(state, nodes, pos, &right);
    //*value = left;
    //return nv;
    return 0;
  }
  case N_UNARY:
  case N_RETURN: {
    return defTypeEx(state, nodes, pos, value);
  }
  }

  cup_errorf("defType UNKNOWN node make for '%s'", token_names[n.type]);
  return 0;
}
size_t defType(CUPState *state, const Node *nodes, const CUPType** value) {
  size_t pos = 0;
  size_t r = defTypeEx(state, nodes, &pos, value);
  char tname[256];
  cup_type_snname(tname, sizeof(tname), *value);
  printf("defType result type = %s\n", tname);
  return r;
}

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
  switch (state->type) {
  case T_IMPORT: {
    getToken(state);
    if (state->type != T_STRING) {
      cup_errorf("Expected Module name[string] but got '%s'", token_names[state->type]);
      return left;
    }
    importModule(state);
    getToken(state);
    return left;
  }
  case T_EXTERNAL: {
    getToken(state);
    if (state->type != T_STRING) {
      cup_errorf("Expected .DLL/.SO name[string] but got '%s'", token_names[state->type]);
      return left;
    }
    externalModule(state);
    getToken(state);
    return left;
  }
  case T_AT: { // function
    Node n = state->node;
    getToken(state);

    if (state->type == T_AT) {
      getToken(state);
      cup_error("function macro for 'T_AT' not implemented yet");
      return left;
    }
    if (state->type != T_IDENTIFIER) {
      cup_errorf("Expected T_IDENTIFIER, but got %s", "");
      return left;
    }
    vector_pushT(left.vec, n);           // T_AT
    vector_pushT(left.vec, state->node); // T_IDENTIFIER
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

    state->scope++;
    Nodes args_nodes = statement(state);
    size_t argc = 1;
    if (args_nodes.data && args_nodes.data->type == T_COMMA)
      argc = args_nodes.data[1].value;
    memset(DTypes, 0, sizeof(DTypes));
    for (size_t i = 0; i < argc; i++)
      DTypes[i + 1] = &cup_type_void;
    CUPType fn_t = (CUPType){ sizeof(void *), (uint16_t)sizeof(void *), CUP_TYPE_FUNCTION, DTypes };
    const CUPType *type = cup_type_put(state, &fn_t);
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
    vector_pushT(left.vec, state->node);
    getToken(state);
    break;
  }
  case T_WHILE: {
    vector_pushT(left.vec, state->node);
    skipSpaces(state);
    push_vector(&left.vec, statement(state).vec);
    push_vector(&left.vec, statement(state).vec);
    return left;
  }
  case T_RETURN: {
    vector_pushT(left.vec, state->node);
    skipSpaces(state);
    push_vector(&left.vec, statement(state).vec);
    break;
  }
  case T_SUB: {
    state->type = N_UNARY;
    vector_pushT(left.vec, state->node);
    getToken(state);
    push_vector(&left.vec, primary(state, CUPDEFPOWER).vec);
    break;
  }
  case T_NOT: {
    vector_pushT(left.vec, state->node);
    getToken(state);
    push_vector(&left.vec, primary(state, CUPDEFPOWER).vec);
    break;
  }
  case T_IDENTIFIER: {
    vector_pushT(left.vec, state->node);
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
    if (state->type == T_OSB) {
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
    if (state->type == T_DOT) {
      n.node.type = T_DOUBLE;
      n.dvalue = (double)n.value;
      getToken(state);
      if (state->type == T_NUMBER) {
        if (state->nodes.value != 0)
          n.dvalue += (state->nodes.value / pow(10, floor(log10(state->nodes.value) + 1)));
      }
    }
    vector_pushT(left.vec, n);
    break;
  }
  case T_CALL: { // '('
    skipSpaces(state);
    if (state->type != T_CRB) {
      left = statement(state);
      expectAndNext(T_CRB);
    }
    else {
      Node2 n = state->nodes;
      n.node.type = T_COMMA;
      vector_pushT(left.vec, n);
      getToken(state); // T_CRB;
    }
    break;
  }
  case T_OCB: { // '{'
    state->type = N_BLOCK;
    vector_pushT(left.vec, state->nodes);
    skipSpaces(state);
    state->scope++;
    while (state->type != T_CCB) {
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
    state->type = N_ARRAY;
    vector_pushT(left.vec, state->nodes);
    skipSpaces(state);
    if (state->type != T_CSB) {
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
    vector_pushT(left.vec, state->node);
    skipSpaces(state);
    push_vector(&left.vec, statement(state).vec);
    push_vector(&left.vec, statement(state).vec);
    if (state->type == T_ELSE) {
      vector_fpushT(left.vec, state->node);
      skipSpaces(state);
      push_vector(&left.vec, statement(state).vec);
    }
    break;
  }
  case T_VARG: {
    vector_pushT(left.vec, state->node);
    getToken(state);
    break;
  }
  default: {
    cup_errorf("primary: Unexpected token '%s'", token_names[state->type]);
    exit(1);
    break;
  }
  }
  //if (state->type == T_EOF) {
  //  exit(1);
  //}

  while (1) {
    Node node = state->node;
    const uint8_t power = getPower(node.type);
#if DEBUG_LOG
    printf("power: %d > %s %d\n", mpower, token_names[node.type], power);
#endif
    if (power == UINT8_MAX) {
      cup_errorf("primary op1: Unexpected token %s", token_names[node.type]);
      exit(1);
    }
    if (power < mpower)
      return left;
    skipSpaces(state);
    Nodes right = {};
    int norb = node.type != T_CALL;
    if (state->type != T_CRB || norb)
      right = primary(state, norb ? power : CUPDEFPOWER);
    else {
      printf("N_COMMA 0\n");
      Node tempn = node;
      tempn.type = T_COMMA;
      vector_pushT(right.vec, tempn);
      tempn.value = 0;
      vector_pushT(right.vec, tempn);
    }

    switch (node.type) {
    case T_CALL: {
      if (state->type != T_CRB) {
        cup_errorf("Expected T_CRB, but got %s", token_names[state->type]);
        exit(1);
      }
      getToken(state);
      //expectAndNext(T_CRB);
      node.type = T_CALL;
      vector_fpushT(left.vec, node);
      Node *nptr = right.data;
      //cup_errorf("CALL: %s", token_names[nptr->type]);
      if (nptr->type != T_COMMA) {
        node.type = T_COMMA;
        vector_pushT(left.vec, node);
        node.value = 1;
        vector_pushT(left.vec, node);
      }
      break;
    }
    case T_EQ: {
      //defType(state, left.data,
        //defType(state, right.data, (CUPType*)0)
      //);
      vector_fpushT(right.vec, node);
      fpush_vector(&left.vec, right.vec);
      continue;
    }
    case T_COMMA: {
      if ((left.data)->type == T_COMMA)
        (left.data)[1].value++;
      else {
        Node n = {.value = 2};
        vector_fpushT(left.vec, n);
        vector_fpushT(left.vec, node);
      }
      break;
    }
    case T_ADD:
    case T_EQEQ:
    case T_MUL:
    case T_LESSEQ:
    case T_SUB: {
      vector_fpushT(left.vec, node);
      break;
    }
    default:
      cup_errorf("primary op: Unexpected token '%s'", token_names[node.type]);
      exit(1);
    }
    push_vector(&left.vec, right.vec);
  }
}