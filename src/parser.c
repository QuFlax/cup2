#include "../include/cup.h"
//#include <threads.h>
#include <math.h> // for pow, floor, log10
#include <sys/stat.h>
#include <sys/mman.h>

//#include <dlfcn.h>
//#include <libelf.h>

#define DEBUG_LOG 0

void* allocMemory(size_t size) {
#ifdef _WIN32
  void* ptr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (!ptr) {
    fprintf(stderr, "VirtualAlloc failed: %lu\n", GetLastError());
    exit(1);
  }
  return ptr;
#else
  void* mem = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem != MAP_FAILED)
    return mem;
  perror("mmap failed");
  exit(1);
#endif
}

static int samefile(const char* a, const char* b) {
#if 0
  //TODO: windows stuf
  return 0;
#else
  if (!a || a[0] == '0' || !b || *b == '0') return 0;
  struct stat sa, sb;
  if (stat(a, &sa) != 0 || stat(b, &sb) != 0) return 0;
  return (sa.st_ino == sb.st_ino) && (sa.st_dev == sb.st_dev);
#endif
}

#define CUPDEFPOWER 2

#ifndef CUPMAXTYPES
#define CUPMAXTYPES 256
#endif


void* cup_var_value(CVariable* var) {
  if (var == NULL)
    return NULL;
  return (void*)var->value;
}
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
  return cup_type_put(state, (CUPType*)var->type);
}

Nodes primary(CUPState *state, const CUPType **return_type, uint8_t mpower);

Nodes statement(CUPState *state, const CUPType** rtype) {
  Nodes n = primary(state, rtype, CUPDEFPOWER);
  if (state->type == T_NL)
    skipSpaces(state);
  return n;
}

static CVariable* getScopeVar(CUPState *state, size_t name) {
  //if (name == SIZE_MAX) return SIZE_MAX;
  return symmap_get(&state->symmap, name);
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

void importModule(CUPState* state) {
  //CUPState copy;
  //memcpy(&copy, state, sizeof(CUPState));
  const char* name = getString(state, state->nodes.value);
  if (cup_compile_file(state, name))
    cup_errorf("Load module '%s'", name);
    
  //vector_pushT(left, state->nodes);
  //getToken(state);

  //state->input_stream = input_stream;
}

void externalModule(CUPState* state) {
  const char* name = getString(state, state->nodes.value);
  char temp[2048] = {0};
  strncpy(temp, name, sizeof(temp));
  char* dot = strrchr(temp, '.');
  if (!dot || (dot && strcmp(dot, ".so")))
      strcat(temp, ".so");

  /*void *handle = dlopen(temp, RTLD_LAZY); // "./libmylib.so"
  if (!handle) {
    printf("Error: %s\n", dlerror());
    return 1;
  }
  dlerror(); // Clear any existing error

  int (*add_func)(int, int) = dlsym(handle, "add");
  printf("2 + 3 = %d\n", add_func(2, 3));

  dlclose(handle);*/
  return;
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
static size_t DRType = 0;

size_t defTypeEx(CUPState *state, const Node *nodes, size_t *pos, const CUPType** value) {
  printf("DRType = %ld\n", DRType);
  if (DRType) {
    DRType--;
  }
  if (!nodes || !state) {
    cup_error("defType state or nodes are NULL");
    return 0;
  }
  assert(pos != NULL);

  Node n = nodes[(*pos)++];
  size_t nv = 0;
  const CUPType *left = NULL, *right = NULL;

  switch (n.type) {
  case T_ADD: case T_SUB: case T_MUL: case T_DIV: case T_MOD:
  {
    nv = defTypeEx(state, nodes, pos, &left);
    if (nv == 0 || nv >= 2) {
      cup_error("defType left has multiple values");
      return 0;
    }
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
    DRType++;
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
    //left = defTypeEx(state, nodes, pos, NULL);
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
      //right = defTypeEx(state, nodes, pos, NULL);
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
          CUP_TYPE t = commonType(*arg_type, *left->elements[i + 1]);
          if (t != CUP_TYPECOUNT) {
            char tname[256];
            cup_type_snname(tname, sizeof(tname), arg_type);
            cup_errorf("defType call arg %ld type mismatch: expected %s", i, tname);
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
  case T_COMMA: {
    nv = nodes[(*pos)++].value;
    if (nv >= UINT8_MAX) {
      cup_error("defType comma has strange count");
      return 0;
    }
    memset(DTypes, 0, sizeof(DTypes));
    CUPType t = {0, 0, CUP_TYPE_STRUCT, DTypes};
    for (size_t i = 0; i < nv; i++) {
      //DTypes[i] = defTypeEx(state, nodes, pos, NULL);
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
    CVariable* var = symmap_get(&state->symmap, v);
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
    nv = defTypeEx(state, nodes, pos, &left);
    if (nv == 0 || nv >= 2) {
      cup_error("defType function left has multiple values");
      return 0;
    }
    *value = left;
    return nv;
  }
  case N_UNARY:
  case N_RETURN: {
    nv = defTypeEx(state, nodes, pos, &left);
    if (nv == 0 || nv >= 2) {
      cup_error("defType function left has multiple values");
      return 0;
    }
    *value = left;
    return nv;
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

Nodes primary(CUPState *state, const CUPType **return_type, uint8_t mpower) {
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
    if (getScopeVar(state, name)) {
      cup_error("redefined variable function");
      exit(1);
    }
    symmap_put(&state->symmap, name, NULL, 0, state->scope);
    vector_pushT(left.vec, name);
    getToken(state);

    //CVector vars = {cup_malloc(state->vars_size), state->vars_size, state->vars_size};
    //state->vars_size = 0;

    state->scope++;
    Nodes args_nodes = statement(state, NULL);
    const CUPType *type;
    size_t nv = defType(state, args_nodes.data, &type);
    char tname[256];
    cup_type_snname(tname, sizeof(tname), type);
    printf("defType(%ld) function args type = %s\n", nv, tname);
    push_vector(&left.vec, args_nodes.vec);

    CUPType fn_t = (CUPType){ sizeof(void *), (uint16_t)sizeof(void *), CUP_TYPE_FUNCTION, DTypes };
    memmove(DTypes + 1, DTypes, sizeof(*DTypes) * nv);
    DTypes[0]      = NULL;
    DTypes[nv + 1] = NULL;

    //CUPType fn_t = cup_type_function(NULL, type);
    type = cup_type_put(state, &fn_t);
    //const CUPType *fn_t = cup_type_put(state, cup_type_function(NULL, args_type));
    cup_type_snname(tname, sizeof(tname), type);
    printf("defType function type = %s\n", tname);
    getScopeVar(state, name)->type = type;
    //((CVariable *)vars.data)[v].type = fn_t;

    //TODO: change fpush_vector to add vars
    //fpush_vector(&state->vars_vec, vars);

    Nodes block = statement(state, type->elements);
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
    push_vector(&left.vec, statement(state, return_type).vec);
    push_vector(&left.vec, statement(state, return_type).vec);
    return left;
  }
  case T_RETURN: {
    vector_pushT(left.vec, state->node);
    skipSpaces(state);
    Nodes right = primary(state, NULL, CUPDEFPOWER);
    if (return_type) {
      defType(state, right.data, return_type);
      // cup_error(state, "Not valid return expression");
      // cup_cup_free(left.data);
      // left.data = nullptr;
      // left.capacity = 0;
      // left.size = 0;
      // return {};
    } else {
      cup_error("Return in not returnable expression");
      exit(1);
    }
    push_vector(&left.vec, right.vec);

    //if (return_type) {
      // const CType* type = defType(state, right);
      // if (*return_type && *return_type != type) {
      //   cup_error(state, "Return type mismatch");
      //	exit(1);
      // }
      // else
      //*return_type = type;
    //}
    break;
  }
  case T_SUB: {
    state->type = N_UNARY;
    vector_pushT(left.vec, state->node);
    getToken(state);
    push_vector(&left.vec, primary(state, return_type, CUPDEFPOWER).vec);
    break;
  }
  case T_NOT: {
    vector_pushT(left.vec, state->node);
    getToken(state);
    push_vector(&left.vec, primary(state, return_type, CUPDEFPOWER).vec);
    break;
  }
  case T_IDENTIFIER: {
    vector_pushT(left.vec, state->node);
    size_t name = state->nodes.value;
    CVariable* v = symmap_get(&state->symmap, name);
    if (v) {
      // TODO: check is it conflict name and type
      //const char *s = "cup_type_name(((CVariable *)state->vars.data)[v].type)";
      //cup_errorf("TODO: check is it conflict name and type %s", cup_type_name(state->vars[v].type));
    } else {
      v = symmap_put(&state->symmap, name, NULL, 0, state->scope);
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
    Node2 tempn = state->nodes;
    (void)tempn;
    //state->type = T_COMMA;
    //state->nodes.value = 1;
    //vector_pushT(left.vec, state->nodes);
    skipSpaces(state);
    if (state->type != T_CRB) {
      Nodes block = statement(state, return_type);
      if (block.data->type != T_COMMA) {
        //tempn.node.loc.type = T_COMMA;
        //tempn.value = 1;
        //vector_pushT(left.vec, tempn);
      }
      push_vector(&left.vec, block.vec);
      //push_vector(&left.vec, primary(state, return_type, CUPDEFPOWER).vec);
      //(left.data)[1].value = 1;
    }
    expectAndNext(T_CRB);
    break;
  }
  case T_OCB: { // '{'
    state->type = N_BLOCK;
    vector_pushT(left.vec, state->nodes);
    skipSpaces(state); // skip T_OCB {
    state->scope++;
    while (state->type != T_CCB) {
      { // statement
        Nodes block = statement(state, return_type);
        if (block.data)
          (left.data)[1].value++;
        push_vector(&left.vec, block.vec);
      }
    }
    state->scope--;
    //if (state->type == T_NL)
    //  skipSpaces(state);
    expectAndNext(T_CCB);
    return left;
  }
  case T_OSB: { // '['
    state->type = N_ARRAY;
    vector_pushT(left.vec, state->nodes);
    skipSpaces(state);
    if (state->type != T_CSB) {
      Nodes body = primary(state, return_type, CUPDEFPOWER);
      if (body.data)
        ((Node*)left.data)[1].value++;
      push_vector(&left.vec, body.vec);
    }
    if (state->type == T_NL)
      skipSpaces(state);
    expectAndNext(T_CSB);
    break;
  }
  case T_IF: {
    vector_pushT(left.vec, state->node);
    skipSpaces(state);
    push_vector(&left.vec, statement(state, return_type).vec);
    push_vector(&left.vec, statement(state, return_type).vec);
    if (state->type == T_ELSE) {
      vector_fpushT(left.vec, state->node);
      skipSpaces(state);
      push_vector(&left.vec, statement(state, return_type).vec);
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
      right = primary(state, return_type, norb ? power : CUPDEFPOWER);
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
      //cup_errorf(state, "primary op2: Unexpected token " NODEFMT, NODEFMTV(t));
      exit(1);
    }
    push_vector(&left.vec, right.vec);
  }
}

void printNodes(CUPModule* buf) {
  for (Node* it = buf->it; it != buf->end; it++) {
    if (it->type == N_BLOCK || it->type == T_NUMBER || it->type == T_COMMA) {
      printf("%s - ", token_names[it->type]);
      printf("%ld\n", (++it)->value);
    } else if (it->type == T_IDENTIFIER) {
      printf("%s - ", token_names[it->type]);
      //printf("%p, %ld\n", buf->state, (++it)->value);
      printf("%s\n", getString(buf->state, (++it)->value));
    } else if (it->type == T_EQ || it->type == T_CALL) {
      printf("%s\n", token_names[it->type]);
    } else {
      printf("%s (%ld)\n", token_names[it->type], it->value);
    }
  }
}

void printVar(void* ctx, const CVariable* v) {
  CUPState* state = (CUPState*) ctx;
  const char* str = getString(state, v->name);
  char tname[256];
  cup_type_snname(tname, sizeof(tname), v->type);
  printf("Var: [%ld]%s type= %s, ptr= %ld, %p, scope= %ld\n",
            v->name, str, tname, v->value, (void*)v->value, v->scope);
}

void parse(CUPState *state, const char* buf, const char* file) {
  if (file) {
    for (CUPModuleList* it = state->modules; it; it = it->next)
      if (samefile(it->path, file))
        return;
  }
  CUPModule* m = (CUPModule*)cup_malloc(sizeof(CUPModuleList));
  memset(m, 0, sizeof(CUPModuleList));
  ((CUPModuleList*)m)->path = file;
  ((CUPModuleList*)m)->next = state->modules;
  state->modules = (CUPModuleList*)m;

  const char* input_stream = state->input_stream;
  const char* priv_stream = state->priv_stream;
  Node2 ncopy = state->nodes;

  state->priv_stream = NULL;
  state->input_stream = buf;
  state->loc.col = 1;
  state->loc.line = 1;
  m->state = state;

  Nodes nodes = {};
  //Loc ttt = {N_BLOCK, 1, 1};
  state->nodes = (Node2){};
  state->loc = (Loc){N_BLOCK, 1, 1};
  //state->nodes = (Node2){ {{N_BLOCK}, {}, 1}, 0 };
  //state->nodes = (Node2){{N_BLOCK, 1, 1},{0}};
  vector_pushT(nodes.vec, state->nodes);

  const CUPType *type = NULL;
  char tname[256];
  skipSpaces(state);
  while (state->type != T_EOF) {
    Nodes n = statement(state, &type);
    if (n.data)
      (nodes.data)[1].value++;
    const CUPType *t = NULL;
    defType(state, n.data, &t);
    cup_type_snname(tname, sizeof(tname), t);
    printf("type = %s\n", tname);
    push_vector(&nodes.vec, n.vec);
  }
  
  if (type) {
    cup_type_snname(tname, sizeof(tname), type);
    printf("type = %s\n", tname);
  }
  m->it = nodes.data;
  m->end = (Node*)((char*)m->it + nodes.size);

  printNodes(m);
  state->input_stream = input_stream;
  state->nodes = ncopy;
  //return;

  //symmap_iter(&state->symmap, state, printVar);
  printf("--\n");
  size_t init = 0;
  codegen_func(m, &init, 0);

  void *page = allocMemory(m->size);
  memcpy(page, m->data, m->size);
  cup_free(m->data);
  
  CRealloc *rptr = m->reallocs.data;
  for (size_t i = m->reallocs.size / sizeof(CRealloc); i--;) {
    CRealloc rr = rptr[i];
    *rr.var = (size_t)(page + rr.ptr);
    //if (m->data[rr.ptr] == 0x55) {
      //printf("BR: %ld\n", *rr.var);
      //*rr.var = (size_t)(m->data + rr.ptr);
      //printf("AR: %ld\n", *rr.var);
    //}
    //if (m->data[rr.ptr] == 0) {
      //memcpy(m->data + rr.ptr, rr.var, sizeof(size_t));
    //}
    printf("[%ld] %ld %p (%02X) value = %zx\n", i, rr.ptr, rr.var, m->data[rr.ptr], *rr.var);
  }
  printf("\n------\n");
  for (size_t i = 0; i < m->size; i++) {
    printf("%02X", m->data[i]);
  }
  printf("\n------\n");
  //printf("init = %zx\n", init);

  symmap_iter(&state->symmap, state, printVar);

  printf("page = %p, init = %p\n", page, (void*)init);
  typedef void (*JitFunc)();
  ((JitFunc)init)();
  
  state->priv_stream = priv_stream;
  state->input_stream = input_stream;
  state->nodes = ncopy;
}

void emit_error(CUPModule *buf, size_t size) {
  size_t capacity = PAGESIZE;
  if (buf == NULL) {
    cup_error("emit_error: buf is NULL");
    exit(1);
  }
  if (buf->data == NULL)
    buf->data = (uint8_t *)cup_malloc(capacity);
  buf->size += size;
  if (buf->size > capacity) {
    do { capacity += PAGESIZE; } while (buf->size > capacity);
    buf->data = (uint8_t *)cup_realloc(buf->data, capacity);
  }
}
void emit(CUPModule *buf, const void *value, size_t size) {
  if (buf == NULL)
    return;
  size_t offset = buf->size;
  emit_error(buf, size);
  memcpy(buf->data + offset, value, size);
}


CUPState *cup_new(int target) {
  (void)target;
  #define DCHART uint16_t
  CUPState* state;
  DCHART* names;
  cup_alloc(state, CUPState);
  cup_alloc(names, DCHART);
  state->names = (uint8_t*)(names + 1);
  //cup_calloc(state, sizeof(CUPState));
  //cup_calloc(state->names, sizeof(uint8_t) * 2);
  //state->names += 2;
  symmap_init(&state->symmap);
  return state;
}
void cup_delete(CUPState *state) {
  //TODO: chech cup_free all
  if (state == NULL)
    return;
  for (CUPModuleList* m = state->modules; m; m = m->next) {
    //cup_freeMemory(m->code, m->capacity);
  }
  symmap_free(&state->symmap);
  //cup_free(state->vars);
  cup_free(state->data);
  cup_free(state);
}
int cup_compile_string(CUPState *state, const char *buf) {
  if (state == NULL || buf == NULL || *buf == '\0')
    return CUP_ERR_ARGUMENTS;
  parse(state, buf, "");
  return 0;
}
int cup_compile_file(CUPState *state, const char *filename) {
 if (state == NULL || filename == NULL || *filename == '\0')
    return CUP_ERR_ARGUMENTS;
  FILE *f = fopen(filename, "rb");
  if (f == NULL)
    return CUP_ERR_FILE_NOT_FOUND;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = (char *)cup_malloc(size + 1);
  buf[size] = '\0';
  fread(buf, size, 1, f);
  fclose(f);
  parse(state, buf, filename);
  cup_free(buf);
  return 0;
}
int cup_add_symbol(CUPState *state, const char *name, void *val, const CUPType type) {
  // TODO: add check safe type
  if (state == NULL || name == NULL || *name == '\0') {
    cup_error("Cannot add symbol(%s) because state == NULL || name == NULL");
    return -1;
  }

  size_t len = strlen(name);
  if (!idToken(state, name, len)) {
    cup_errorf("Cannot add symbol(%s) because it is a keyword", token_names[state->type]);
    return -2;
  }

  CVariable* v = symmap_get(&state->symmap, state->nodes.value);
  if (v) {
    cup_errorf("Cannot add symbol(%s) because it already exist", name);
    return -3;
  }
  const CUPType* t = cup_type_put(state, (CUPType*)&type);
  symmap_put(&state->symmap, state->nodes.value, t, (size_t)val, 0);
  return 0;
}
CVariable *cup_get_symbol(CUPState *state, const char *name) {
  if (state == NULL)
    return NULL;
  if (name == NULL || *name == '\0')
    return NULL;
  size_t len = strlen(name);
  idToken(state, name, len);
  if (state->nodes.value == SIZE_MAX)
    return NULL;
  return symmap_get(&state->symmap, state->nodes.value);
}
void cup_list_symbols(CUPState *state, void *ctx, cup_list_symbols_callback cb) {
  if (state == NULL)
    return;
  if (!state->symmap.slots) return;
  for (size_t i = 0; i < state->symmap.capacity; i++) {
    SymEntry se = state->symmap.slots[i];
    if (!slot_empty(&se) && se.vars[0].scope == 0) {
      cb(ctx, getString(state, se.vars[0].name), se.vars[0].value, se.vars[0].type);
    }
  }
}
