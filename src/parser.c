#include "../include/cup.h"

//#include <threads.h>
//#include <sys/stat.h>
//#include <sys/mman.h>

//#include <dlfcn.h>
//#include <libelf.h>

#define DEBUG_LOG 0

#define pushNode(nodes, node) \
  assert(node);\
  vector_fpushT(nodes.vec, node)
#define pushNode2(nodes, node2) \
  assert(node2);\
  vector_fpushT(nodes.vec, node2)

void* cup_var_value(const CVariable* var) {
  if (var == NULL)
    return NULL;
  char tname[256];
  cup_type_snname(tname, sizeof(tname), var->typee);
  cup_dbgf("cup_var_value: %p %s", var, tname);
  return (void*)var->value;
}
const CUPType* cup_var_type(const CVariable* var) {
  if (var == NULL)
    return NULL;
  return var->typee;
}

Nodes primary(CUPState *state, uint8_t mpower);

Nodes statement(CUPState *state) {
  Nodes n = primary(state, CUPDEFPOWER);
  if (state->nodes.token == T_NL)
    getToken(state, 0);
  return n;
}

static void scope_ensure_root(CUPState *state) {
  if (state->scopes.size == 0) {
    size_t this_scope = SIZE_MAX;
    vector_pushT(state->scopes.vec, this_scope);
    state->scope = 0;
  }
}

size_t scope_enter(CUPState *state) {
  scope_ensure_root(state);

  size_t parent_to_this_scope = state->scope;
  size_t this_scope = state->scopes.size / sizeof(size_t);

  vector_pushT(state->scopes.vec, parent_to_this_scope);
  state->scope = this_scope;

  return this_scope;
}

void scope_leave(CUPState *state) {
  assert(state->scopes.data);
  assert(state->scope < state->scopes.size / sizeof(size_t));

  size_t parent = state->scopes.data[state->scope];
  assert(parent != SIZE_MAX); /* do not leave root scope */

  state->scope = parent;
}

static size_t hash_key(const char *s, size_t scope) {
    size_t h = 1469598103934665603ULL ^ (scope * 0x9E3779B97F4A7C15ULL);
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ULL; }
    return h;
}

static void varInsert(CUPState *state, size_t id) {
  size_t mask = (state->vars.vec.capacity/sizeof(CVariable) - 1);
  size_t i = hash_key(state->vars.data[id].name, state->vars.data[id].scope) & mask;
  while(state->vars_map[i]) i = (i + 1) & mask;
  state->vars_map[i] = id + 1;
}

void putVar(CUPState *state, const char* name, const CUPType* type, size_t value, size_t scope) {
  CVariable var = {name, type, value, scope};
  size_t cap = state->vars.vec.capacity;
  vector_push(&state->vars.vec, &var, sizeof(var));
  size_t pos = state->vars.size/sizeof(var);
  size_t ncap = state->vars.vec.capacity/sizeof(var);
  if (state->vars.vec.capacity != cap) {
    cup_free(state->vars_map);
    size_t nsize = ncap * sizeof(size_t);
    state->vars_map = cup_malloc(nsize);
    memset(state->vars_map, 0, nsize);
    for (size_t i = 0; i < pos; i++) 
      varInsert(state, i);
  } else {
    varInsert(state, pos - 1);
  }
  //map_put(&state->symmap, name, &var, SIZE_MAX);
  //cup_dbgf("putVar %s %zu | %zu / %zu", getString(state, name), var.scope, state->symmap.count, state->symmap.capacity);
}

CVariable *varFind(CUPState *state, const char* name, size_t scope) {
  if (state->vars.vec.capacity == 0) return NULL;
  size_t ncap = state->vars.vec.capacity/sizeof(CVariable);
  size_t mask = (ncap - 1);
  size_t i = hash_key(name, scope) & mask;
  for(size_t n = 0; n < ncap; n++) {
    size_t slot = state->vars_map[i];
    if (!slot) break;
    CVariable* v = &state->vars.data[slot - 1];
    if (v->scope == scope && !strcmp(v->name, name)) return v;
    i = (i + 1) & mask;
  }
  return NULL;
}

CVariable *varLookup(CUPState *state, const char* name, size_t scope) {
  for (;;) {
    CVariable *v = varFind(state, name, scope);
    if (v) return v;
    if (scope == 0) return NULL;
    scope = state->scopes.data[scope];
  }
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
    getToken(state, 1);                                                           \
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
  case T_OCB:
    return 1;
  case T_OSB:
    return CUPDEFPOWER;
  }
}

Nodes primary(CUPState *state, uint8_t mpower) {
  Nodes left = {};
  switch (state->nodes.token) {
  case T_IMPORT: {
    getToken(state, 1);
    if (state->nodes.token != T_STRING) {
      cup_errorf("Expected Module name[string] but got '%s'", token_names[state->nodes.token]);
      return left;
    }
    importModule(state);
    getToken(state, 1);
    return left;
  }
  case T_EXTERNAL: {
    getToken(state, 1);
    if (state->nodes.token != T_STRING) {
      cup_errorf("Expected .DLL/.SO name[string] but got '%s'", token_names[state->nodes.token]);
      return left;
    }
    externalModule(state);
    getToken(state, 1);
    while (state->nodes.token == T_IDENTIFIER) {
      const char* name = state->nodes.svalue;
      getToken(state, 1);
      if (state->nodes.token != T_STRING) {
        cup_errorf("external: expected signature string after name, got '%s'", token_names[state->nodes.token]);
        return left;
      }
      externalSymbol(state, name, state->nodes.value);
      getToken(state, 1);
    }
    return left;
  }
  case T_AT: { // function
    char tname[256] = {};
    Node n = state->nodes.node;
    getToken(state, 1);

    if (state->nodes.token == T_AT) {
      getToken(state, 1);
      cup_error("function macro for 'T_AT' not implemented yet");
      return left;
    }
    if (state->nodes.token != T_IDENTIFIER) {
      cup_errorf("Expected T_IDENTIFIER, but got %s", "");
      return left;
    }
    vector_pushT(left.vec, n);           // T_AT
    vector_pushT(left.vec, state->nodes); // T_IDENTIFIER
    const char* name = state->nodes.svalue;
    CVariable* vvv = varLookup(state, name, state->scope);
    if (vvv == NULL) {
      putVar(state, name, NULL, 0, state->scope);
    } else {
      if (vvv->typee && vvv->typee != &cup_type_void) {
        cup_type_snname(tname, sizeof(tname), vvv->typee);
        cup_dbgf("defType_ function args type = %s\n", tname);
        cup_error("redefined variable function");
        exit(1);      
      }
    }
    getToken(state, 1);

    scope_enter(state);

    Node2 comma = state->nodes;
    Nodes args_nodes = statement(state);
    size_t argc = 1;
    if (args_nodes.data && args_nodes.data->token == T_COMMA)
      argc = args_nodes.data[1].value;
    else {
      comma.token = T_COMMA;
      comma.value = 1;
      vector_pushT(left.vec, comma);
    }
    

    const void ** types = (const void **)cup_talloc(sizeof(const CUPType*) * (argc + 2));
    /* Function layout: [return, arg0, ..., argN-1, void-sentinel].
     * NULL argument slots mean unknown/any. */
    types[argc + 1] = &cup_type_void;
    const CUPType *type = cup_type(state, CUP_TYPE_FUNCTION, argc + 1, (const void *)types);
    cup_treset(0);
    cup_type_snname(tname, sizeof(tname), type);
    cup_dbgf("defType function type = %s\n", tname);
    vvv = varLookup(state, name, state->scope);
    assert(vvv);
    vvv->typee = type;
    
    push_vector(&left.vec, args_nodes.vec);

    Nodes block = statement(state);
    push_vector(&left.vec, block.vec);

    scope_leave(state);

    return left;
  }
  case T_BREAK: case T_CONTINUE: {
    vector_pushT(left.vec, state->nodes.node);
    getToken(state, 1);
    break;
  }
  case T_WHILE: {
    vector_pushT(left.vec, state->nodes.node);
    getToken(state, 0);
    push_vector(&left.vec, statement(state).vec);
    push_vector(&left.vec, statement(state).vec);
    return left;
  }
  case T_RETURN: {
    vector_pushT(left.vec, state->nodes.node);
    getToken(state, 0);
    push_vector(&left.vec, statement(state).vec);
    break;
  }
  case T_SUB: {
    state->nodes.token = N_UNARY;
    vector_pushT(left.vec, state->nodes.node);
    getToken(state, 1);
    push_vector(&left.vec, primary(state, CUPDEFPOWER).vec);
    break;
  }
  case T_NOT: {
    vector_pushT(left.vec, state->nodes.node);
    getToken(state, 1);
    push_vector(&left.vec, primary(state, CUPDEFPOWER).vec);
    break;
  }
  case T_IDENTIFIER: {
    vector_pushT(left.vec, state->nodes);
    const char* name = state->nodes.svalue;
    if (state->scope) {
      CVariable* v = varLookup(state, name, state->scope);
      if (v == NULL)
        putVar(state, name, NULL, 0, state->scope);
    } else {
      cup_dbg("primary: T_IDENTIFIER state->scope == 0");
    }
    getToken(state, 1);
    break;
  }
  case T_STRING: {
    vector_pushT(left.vec, state->nodes);
    getToken(state, 1);
    break;
  }
  case T_NUMBER: {
    Node2 n = state->nodes;
    getToken(state, 1);
    if (state->nodes.token == T_DOT) {
      n.token = T_DOUBLE;
      n.dvalue = (double)n.value;
      getToken(state, 1);
      if (state->nodes.token == T_NUMBER) {
        n.dvalue += frac(state->nodes.value);
        getToken(state, 1);
      }
    }
    vector_pushT(left.vec, n);
    break;
  }
  case T_THIS: {
    vector_pushT(left.vec, state->nodes.node);
    getToken(state, 1);
    break;
  }
  case T_CALL: { // '('
    getToken(state, 0);
    if (state->nodes.token != T_CRB) {
      left = statement(state);
      expectAndNext(T_CRB);
    }
    else {
      Node2 n = state->nodes;
      n.token = T_COMMA;
      vector_pushT(left.vec, n);
      getToken(state, 1); // T_CRB;
    }
    break;
  }
  case T_OCB: { // '{'
    state->nodes.token = N_BLOCK;
    vector_pushT(left.vec, state->nodes);
    getToken(state, 0);
    
    scope_enter(state);

    while (state->nodes.token != T_CCB) {
      {
        Nodes block = statement(state);
        if (block.data) left.data[1].value++;
        push_vector(&left.vec, block.vec);
      }
    }
    getToken(state, 1); // T_CCB
    
    scope_leave(state);

    return left;
  }
  case T_OSB: { // '['
    state->nodes.token = N_ARRAY;
    vector_pushT(left.vec, state->nodes);
    getToken(state, 0);
    if (state->nodes.token != T_CSB) {
      Nodes body = primary(state, CUPDEFPOWER);
      if (body.data) {
        left.data[1].value = (body.data->token == T_COMMA)
          ? body.data[1].value : 1;
        push_vector(&left.vec, body.vec);
      } else {
        Node2 tempn = {.node = left.data[0]};
        tempn.token = T_COMMA;
        tempn.value = 0;
        vector_pushT(left.vec, tempn);
      }
    } else {
      Node2 tempn = {.node = left.data[0]};
      tempn.token = T_COMMA;
      tempn.value = 0;
      vector_pushT(left.vec, tempn);
    }
    {
      size_t len = left.data[1].value;
      len *= sizeof(size_t);
      left.data[1].value = state->data_size;
      state->data = (uint8_t*)cup_realloc(state->data, state->data_size + len);
      memset(state->data + state->data_size, 0, len);
      state->data_size += len;
    }
    expectAndNext(T_CSB);
    break;
  }
  case T_IF: {
    vector_pushT(left.vec, state->nodes.node);
    getToken(state, 0);
    push_vector(&left.vec, statement(state).vec);
    push_vector(&left.vec, statement(state).vec);
    if (state->nodes.token == T_ELSE) {
      vector_fpushT(left.vec, state->nodes.node);
      getToken(state, 0);
      push_vector(&left.vec, statement(state).vec);
    }
    break;
  }
  case T_VARG: {
    vector_pushT(left.vec, state->nodes.node);
    getToken(state, 1);
    break;
  }
  case T_AND: {
    assert(0);
  }
  default: {
    cup_errorf("primary expr: Unexpected token '%s'", token_names[state->nodes.token]);
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
    cup_dbgf("power: %d > %s %d", mpower, token_names[opnode.token], power);
    if (power == UINT8_MAX) {
      cup_errorf("primary op1: Unexpected token %s", token_names[opnode.token]);
      exit(1);
    }
    if (power < mpower)
      return left;
    getToken(state, 0);
    Nodes right = {};
    int norb = opnode.token == T_CALL || opnode.token == T_OSB;
    if ((opnode.token == T_CALL && state->nodes.token == T_CRB)
     || (opnode.token == T_OSB && state->nodes.token == T_CSB)) {
      Node2 tempn = opnode;
      tempn.token = T_COMMA;
      tempn.value = 0;
      vector_pushT(right.vec, tempn);
    } else {
      const uint8_t rec = norb ? CUPDEFPOWER : power;
      cup_dbgf("rec: %d", rec);
      if (rec == 16) { // T_DOT
        size_t saves = state->scope;
        state->scope = 0;
        right = primary(state, 16);
        state->scope = saves;
      } else {
        right = primary(state, rec);
      }
    }

    switch (opnode.token) {
    case T_DOT: {
      vector_fpushT(left.vec, opnode.node);
      break;
    }
    case T_CALL: {
      if (state->nodes.token != T_CRB) {
        cup_errorf("Expected T_CRB, but got %s", token_names[state->nodes.token]);
        exit(1);
      }
      getToken(state, 1);
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
    case T_OSB: {
      if (state->nodes.token != T_CSB) {
        cup_errorf("Expected T_CSB, but got %s", token_names[state->nodes.token]);
        exit(1);
      }
      getToken(state, 1);
      //expectAndNext(T_CSB);
      vector_fpushT(left.vec, opnode.node);
      break;
    }
    case T_ADD: case T_ADDEQ: case T_EQEQ: case T_LESS: case T_MUL: case T_LESSEQ: case T_SUB: {
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