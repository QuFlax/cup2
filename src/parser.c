#define CUP_ERROR_H
#include "../include/cup.h"
#include <threads.h>
#include <stdio.h>
#include <string.h>

#define CUPDEFPOWER 2

#ifndef CUPMAXTYPES
#define CUPMAXTYPES 1024
#endif

const char* node_name(Node2 n) {
  return "";
}

CUPModule* getModule(CUPState* state, const char* path) {
  for (CUPModule* m = state->module; m; m = m->next) {
    if (m->path == path) return m;
  }
  return NULL;
}

void importModule(CUPState* state) {
  const char* input_stream = state->input_stream;

  state->input_stream = input_stream;
}

void externalModule(CUPState* state) {

}

// vector_push_node
// vector_pushT

#define push_vector(a, b) push_vector(&a, b, sizeof(Node))
#define fpush_vector(a, b) fpush_vector(&a, b, sizeof(Node))

CVector primary(CUPState *state, const CUPType **return_type, uint8_t mpower) {
  CVector left = {};
  switch (state->type) {
  case T_IMPORT: {
    getToken(state);
    if (state->type != T_STRING) {
      cup_errorf(state, "Expected Module name after '%s'", node_name(state->nodes));
      return left;
    }
    importModule(state);
    getToken(state);
    return left;
  }
  case T_EXTERNAL: {
    getToken(state);
    if (state->type != T_STRING) {
      cup_errorf(state, "Expected .DLL/.SO name after '%s'", node_name(state->nodes));
      return left;
    }
    externalModule(state);
    getToken(state);
    return left;
  }
  case T_AT: { // function
    vector_pushT(&left, state->node);
    getToken(state);

    if (state->type == T_AT) {
      getToken(state);
      cup_error(state, "function macro for 'T_AT' not implemented yet");
      free(left.data);
      return (CVector){};
    }
    if (state->type != T_IDENTIFIER) {
      cup_errorf(state, "Expected T_IDENTIFIER, but got %s", "");
      exit(1); 
    }
    vector_pushT(&left, state->node);
    size_t v = getScopeVar(state, state->nodes.value);
    if (v) {
      cup_error(state, "redefined variable function");
      exit(1);
    }
    v = addScopeVar(state, state->nodes.value, NULL, 0);
    vector_pushT(&left, v);
    getToken(state);
    CVector vars = {state->vars.size, state->vars.size, NULL};
    vars.data = malloc(state->vars.size);
    state->vars.size = 0;
    CVector args_nodes = primary(state, NULL, CUPDEFPOWER);
    const CUPType *args_type;
    {
      size_t pos = 0;
      args_type = defType(state, (Node *)args_nodes.data, &pos, NULL);
    }
    cup_error(state, "TODO: AT function");
    exit(1);
    if (state->type == T_NL)
      skipSpaces(state);

    ((CVariable *)vars.data)[v].type =
    cup_type_get_function(state, state->target, args_type, NULL);

    //TODO: chgange  fpush_vector to add var
    //fpush_vector(&state->vars, vars);

    push_vector(left, args_nodes);
    { // statement
      const CUPType *fn_t = (const CUPType *)hashmap_getv(state->types, state->vars.data[v].type);
      push_vector(left, primary(state, (const CUPType **)&fn_t->types[1], CUPDEFPOWER));
      if (state->node.type == T_NL)
        skipSpaces(state);
    }
    return left;
  }
  case T_BREAK:
  case T_CONTINUE: {
    vector_pushT(&left, state->node);
    getToken(state);
    break;
  }
  case T_WHILE: {
    vector_pushT(&left, state->node);
    skipSpaces(state);
    push_vector(left, primary(state, return_type, CUPDEFPOWER));
    if (state->type == T_NL)
      skipSpaces(state);
    { // statement
      push_vector(left, primary(state, return_type, CUPDEFPOWER));
      if (state->type == T_NL)
        skipSpaces(state);
      if (state->type == T_EOF)
        cup_error(state, "Expected newline after while statement");
    }
    return left;
  }
  case T_RETURN: {
    vector_pushT(&left, state->node);
    skipSpaces(state);
    CVector right = primary(state, NULL, CUPDEFPOWER);
    if (return_type) {
      size_t pos = 0;
      *return_type = defType(state, (Node *)right.data, &pos, NULL);
      // cup_error(state, "Not valid return expression");
      // cup_free(left.data);
      // left.data = nullptr;
      // left.capacity = 0;
      // left.size = 0;
      // return {};
    } else {
      cup_error(state, "Return in not returnable expression");
      exit(1);
    }
    push_vector(left, right);

    if (return_type) {
      // const CType* type = defType(state, right);
      // if (*return_type && *return_type != type) {
      //   cup_error(state, "Return type mismatch");
      //	exit(1);
      // }
      // else
      //*return_type = type;
    }
    break;
  }
  case T_SUB: {
    state->type = N_UNARY;
    vector_pushT(&left, state->node);
    getToken(state);
    push_vector(left, primary(state, return_type, CUPDEFPOWER));
    break;
  }
  case T_NOT: {
    vector_pushT(&left, state->node);
    getToken(state);
    push_vector(left, primary(state, return_type, CUPDEFPOWER));
    break;
  }
  case T_IDENTIFIER: {
    vector_pushT(&left, state->node);

    size_t v = getScopeVar(state, state->nodes.value);
    if (v != SIZE_MAX) {
      // TODO: check is it conflict name and type
      const char *s = CType_name(((CVariable *)state->vars.data)[v].type);
      cup_errorf(state, "TODO: check is it conflict name and type %s", s);
    } else {
      v = addScopeVar(state, state->nodes.value, NULL, 0);
    }
    state->nodes.value = v;
    vector_pushT(&left, state->nodes);
    getToken(state);
    if (state->type == T_OSB) {
      cup_error(state, "Subscripts not implemented");
      exit(1);
    }
    break;
  }
  case T_STRING: {
    vector_pushT(&left, state->nodes);
    getToken(state);
    break;
  }
  case T_NUMBER: {
    Node2 n = state->nodes;
    getToken(state);
    if (state->type == T_DOT) {
      n.node.type = N_DOUBLE;
      n.node2.dvalue = (double)n.value;
      getToken(state);
      if (state->type == T_NUMBER) {
        if (state->nodes.value != 0)
          n.node2.dvalue += (state->nodes.value / pow(10, floor(log10(state->nodes.value) + 1)));
      }
    }
    vector_pushT(&left, n);
    break;
  }
  case T_ORB: { // '('
    state->type = N_COMMA;
    vector_pushT(&left, state->nodes);
    skipSpaces(state);
    if (state->type != T_CRB) {
      push_vector(left, primary(state, return_type, CUPDEFPOWER));
      ((Node *)left.data)[1].value = 1;
    }
    if (state->type == T_NL)
      skipSpaces(state);
    expectAndNext(T_CRB);
    break;
  }
  case T_OCB: { // '{'
    state->type = N_BLOCK;
    vector_pushT(&left, state->nodes);
    skipSpaces(state); // skip T_OCB {
    while (state->type != T_CCB) {
      { // statement
        CVector block = primary(state, return_type, CUPDEFPOWER);
        push_vector(left, block);
        if (state->type == T_NL)
          skipSpaces(state);
        if (state->type == T_EOF)
          cup_error(state, "Expected '}' but got EOF");
        if (block.data)
          ((Node *)left.data)[1].value++;
      }
    }
    if (state->node.type == T_NL)
      skipSpaces(state);
    expectAndNext(T_CCB);
    return left;
  }
  case T_OSB: { // '['
    state->node.type = N_ARRAY;
    vector_pushT(&left, state->nodes);
    skipSpaces(state);
    if (state->type != T_CSB) {
      CVector body = primary(state, return_type, CUPDEFPOWER);
      if (body.data)
        ((Node *)left.data)[1].value++;
      push_vector(left, body);
    }
    if (state->node.type == T_NL)
      skipSpaces(state);
    expectAndNext(T_CSB);
    break;
  }
  case T_IF: {
    vector_pushT(&left, state->node);
    skipSpaces(state);
    push_vector(left, primary(state, return_type, CUPDEFPOWER));
    if (state->node.type == T_NL)
      skipSpaces(state);
    { // statement
      push_vector(left, primary(state, return_type, CUPDEFPOWER));
      if (state->node.type == T_NL)
        skipSpaces(state);
      if (state->node.type == T_EOF)
        cup_error(state, "Expected newline after while statement");
    }
    if (state->node.type == T_ELSE) {
      vector_fpush_node(&left, &state->node);
      skipSpaces(state);
      { // statement
        push_vector(left, primary(state, return_type, CUPDEFPOWER));
        if (state->node.type == T_NL)
          skipSpaces(state);
        if (state->node.type == T_EOF)
          cup_error(state, "Expected newline after while statement");
      }
    }
    break;
  }
  case T_VARG: {
    vector_pushT(&left, state->node);
    getToken(state);
    break;
  }
  default: {
    //cup_errorf(state, "primary: Unexpected token " NODEFMT,
    //           NODEFMTV(state->nodes));
    exit(1);
    break;
  }
  }

  while (1) {
    Node2 t = state->nodes;
    const uint8_t power = getPower(t.node.type);
    printf("power: %d %s %d\n", mpower, CTType_name(t.node.type), power);
    if (power == UINT8_MAX) {
      cup_errorf(state, "primary op1: Unexpected token %s",
                 CTType_name(t.node.type));
      exit(1);
    }
    printf("%d < %d\n", power, mpower);
    if (power < mpower)
      return left;
    skipSpaces(state);
    CVector right = {};
    int norb = t.node.type != T_ORB;
    if (state->type != T_CRB || norb)
      right = primary(state, return_type, norb ? power : CUPDEFPOWER);
    else {
      Node2 tempn = t;
      tempn.node.type = N_COMMA;
      tempn.value = 0;
      vector_pushT(&right, tempn);
    }

    switch (t.node.type) {
    case T_ORB: {
      expectAndNext(T_CRB);
      t.node.type = N_CALL;
      vector_fpush_node(&left, &t);
      Node *nptr = (Node *)right.data;
      //cup_errorf(state, "CALL: " NODEFMT, NODEFMTV(*nptr));
      if (nptr->type != N_COMMA) {
        t.node.type = N_COMMA;
        t.value = 1;
        vector_pushT(&left, t);
      }
      break;
    }
    case T_EQ: {
      assert(right.count >= 1 && "Right size is small");
      {
        size_t pos = 0;
        const CUPType *rv = defType(state, (Node *)right.data, &pos, NULL);
        pos = 0;
        defType(state, (Node *)left.data, &pos, rv);
      }
      vector_fpush_node(&right, &t);
      fpush_vector(left, right);
      continue;
    }
    case T_COMMA: {
      if (((Node *)left.data)->type == N_COMMA)
        ((Node *)left.data)[1].value++;
      else {
        Node n = {.value = 2};
        vector_fpush_node(&left, &n);
        vector_fpush_node(&left, &t);
      }
      break;
    }
    case T_ADD:
    case T_EQEQ:
    case T_MUL:
    case T_LESSEQ:
    case T_SUB: {
      vector_fpush_node(&left, &t);
      break;
    }
    default:
      //cup_errorf(state, "primary op2: Unexpected token " NODEFMT, NODEFMTV(t));
      exit(1);
    }
    push_vector(left, right);
  }
}

void parse(CUPState *state, const char* buf, const char* file) {
  CUPModule* m = getModule(state, file);
  if (m == NULL) {
    callocT(m, CUPModule);
    m->path = file;
    m->next = state->module;
    state->module = m;
  }
  state->input_stream = buf;
  state->loc.col = 1;
  state->loc.line = 1;

  CVector nodes = {};
  state->nodes = (Node2){{N_BLOCK, 1, 1},{0}};
  vector_pushT(&nodes, state->nodes);

  const CUPType *type = NULL;
  skipSpaces(state);
  while (state->type != T_EOF) {
    CVector n = primary(state, &type, CUPDEFPOWER);
    size_t pos = 0;
    defType(state, (Node *)n.data, &pos, NULL);
    push_vector(nodes, n);

    if (state->type == T_NL)
      skipSpaces(state);
    if (n.data)
      ((Node *)nodes.data)[1].value++;
  }
  if (type) {
    printf("type = %s\n", CType_name(type));
  }

  if (1) {
    Node *ptr = (Node *)nodes.data;
    size_t count = nodes.size / sizeof(Node);
    printNodes(state, ptr, count);
    count = state->vars.size / sizeof(CVariable);
    printf("\nCVariable size = %ld\n", count);
    while (count--) {
      CVariable v = ((CVariable *)state->vars.data)[count];
      size_t s = cup_type_size(v.type);
      printf("Var: [%ld]%s type= %s, ptr= %ld, size = %ld\n", v.name,
             getString(state, v.name), CType_name(v.type), (size_t)v.value, s);
    }
    printf("--\n");
    // codegen
    size_t init = 0;
    CBuffer_ buf = {};
    Nodes nnodes = {(Node *)nodes.data, nodes.size / sizeof(Node), 0};
    codegen_func(&buf, &nnodes, &init, 0);
    void *page = allocMemory(buf.size);
    memcpy(page, buf.data, buf.size);
    free(buf.data);
    uint8_t *prr = (uint8_t *)page;
    printf("prr = %p\n", prr);
    CRealloc *rptr = buf.reallocs;
    for (size_t i = 0; i < buf.r_count; i++) {
      CRealloc rr = rptr[i];
      if (prr[rr.ptr] == 0x55) {
        printf("BR: %ld\n", *rr.var);
        *rr.var = (size_t)(prr + rr.ptr);
        printf("AR: %ld\n", *rr.var);
      }
      if (prr[rr.ptr] == 0) {
        memcpy(prr + rr.ptr, rr.var, sizeof(size_t));
      }
      printf("[%ld] %ld %p (%02X) value = %ld\n", i, rr.ptr, rr.var,
             prr[rr.ptr], *rr.var);
    }
    for (size_t i = 0; i < buf.size; i++) {
      fprintf(stdout, "%02X", ((uint8_t *)page)[i]);
    }

    printf("\n------\n");
    typedef size_t (*JitFunc)();
    JitFunc f = (JitFunc)(init);
    f();

    //   cup_error(state, "Failed to codegen");
    // }
  }
}

CUPState *cup_new(int target) {
  (void)target;
  CUPState* state;
  callocT(state, CUPState);
  callocT(state->names, uint16_t);
  state->names += 2;
  return state;
}
void cup_delete(CUPState *state) {
  //TODO: chech free all
  if (state == NULL)
    return;
  for (CUPModule* m = state->module; m; m = m->next) {
    freeMemory(m->code, m->capacity);
  }
  free(state->vars.data);
  free(state->data);
  free(state);
}
int cup_compile_string(CUPState *state, const char *buf) {
  if (state == NULL)
    return CUP_ERR_STATE;
  if (buf == NULL || *buf == '\0') {
    cup_error(state, "Incorrect input stream");
    return CUP_ERR_STATE;
  }
  parse(state, buf, NULL);
  return 0;
}
int cup_compile_file(CUPState *state, const char *filename) {
 if (state == NULL)
    return CUP_ERR_STATE;
  if (filename == NULL || *filename == '\0') {
    cup_error(state, "Incorrect input file path");
    return CUP_ERR_STATE;
  }
  FILE *f = fopen(filename, "rb");
  if (f == NULL) {
    cup_errorf(state, "File '%s' not found", filename);
    return CUP_ERR_FILE_NOT_FOUND;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = (char*)malloc(size + 1);
  buf[size] = '\0';
  fread(buf, size, 1, f);
  fclose(f);
  parse(state, buf, filename);
  free(buf);
  return 0;
}
int cup_add_symbol(CUPState *state, const char *name, void *val, const CUPType *type) {
  // TODO: add check safe type
  if (state == NULL)
    return -1;
  if (name == NULL || *name == '\0')
    return -1;
  size_t len = strlen(name);
  idToken(state, name, len);

  size_t v = getScopeVar(state, state->node.value);
  if (v != SIZE_MAX) {
    cup_errorf(state, "Cannot add symbol(%s) because it already exist", name);
    return -1;
  }
  v = addScopeVar(state, state->node.value, type, (size_t)val);
  return 0;
}
void *cup_get_symbol(CUPState *state, const char *name) {
  if (state == NULL)
    return NULL;
  if (name == NULL || *name == '\0')
    return NULL;
  size_t len = strlen(name);
  idToken(state, name, len);

  size_t v = getScopeVar(state, state->node.value);
  if (v == SIZE_MAX)
    return NULL;
  return &(((CVariable *)state->vars.data)[v].value);
}

const CUPType* cup_type_geti(CUPState *state, const size_t key) {
  if (state == NULL) return CUP_ERR_CTYPE;
  size_t index = hash(key) % state->types.maxlen;
  if (index == SIZE_MAX) {
    if (state->types.count == state->types.maxlen) {
      size_t old_capacity = map->capacity;
      Entry **old_buckets = map->buckets;
  map->capacity *= 2;
  map->buckets = cup_realloc(map->buckets, map->capacity * sizeof(Entry*));
    }
    index = state->types.count++;
  }
  size_t entry = state->types.indexs[index];
  return &state->types.types[entry];
}
void cup_type_put(CUPState *state, const size_t key, CUPType value) {
  if (state == NULL) return;
  if (key >= CUPMAXTYPES) return;
  if (key >= state->types.maxlen) {
    state->types.maxlen *= 2;
    state->types.types = cup_realloc(state->types.types,
      state->types.maxlen * sizeof(*state->types.types));

     size_t old_capacity = map->capacity;
  Entry **old_buckets = map->buckets;
  map->capacity *= 2;
  map->buckets = cup_realloc(map->buckets, map->capacity * sizeof(Entry*));
  memset(&map->buckets[old_capacity], 0, old_capacity);  
  // Rehash all entries
  Entry *entries_to_rehash[map->size];
  size_t entry_count = 0;
    
  // Collect all entries
  for (size_t i = 0; i < old_capacity; i++) {
    Entry *entry = map->buckets[i];
    map->buckets[i] = NULL;  // Clear old bucket
    while (entry) {
      Entry *next = entry->next;
      entries_to_rehash[entry_count++] = entry;
      entry = next;
    }
  }
    
  // Reinsert all entries
  for (size_t i = 0; i < entry_count; i++) {
    Entry *entry = entries_to_rehash[i];
    size_t index = hash(entry->key) % map->capacity;
    entry->next = map->buckets[index];
    map->buckets[index] = entry;
  }

  }
  state->types.types[key];
  const CUPType* t = cup_type_get(state, key);

  hashmap_put(state->types, key, value);
}
static size_t type_name(HashMapType* map, CUPType* t, char* buf) {
#define setb(x) {if(buf) memcpy(buf, x, sizeof(x)-1); return (sizeof(x)-1);}
  if (!t) goto end;
  switch (t->realtype) {
  case CUP_TYPE_VOID: setb("void")
  case CUP_TYPE_INT: setb("int")
	case CUP_TYPE_FLOAT: setb("float")
	case CUP_TYPE_DOUBLE: setb("double")
	case CUP_TYPE_UINT8: setb("uint8");
	case CUP_TYPE_SINT8: setb("int8");
	case CUP_TYPE_UINT16: setb("uint16");
	case CUP_TYPE_SINT16: setb("int16");
	case CUP_TYPE_UINT32: setb("uint32");
	case CUP_TYPE_SINT32: setb("int32");
	case CUP_TYPE_UINT64: setb("uint64");
	case CUP_TYPE_SINT64: setb("int64");
	case CUP_TYPE_STRUCT: {
    char* name = "name";
    size_t len = strlen(name);
    memcpy(buf, name, len);
    buf += len;
    memchr(buf, "{", 1);
    for (CUPType** i = t->types, ) {

    }
    if(buf) {
      memcpy(buf+r, "[XXX]", 2+3);
    }
    return r + 2 + 3;
  }
	case CUP_TYPE_POINTER: {
    size_t r = type_name(map, t->type, buf); // internal
    if(buf) memcpy(buf+r, "*", 1);
    return r + 1;
  }
  case CUP_TYPE_ARRAY: {
    size_t r = type_name(map, t->type, buf); // internal
    if(buf) {
      memcpy(buf+r, "[XXX]", 2+3);
    }
    return r + 2 + 3;
  }
  case CUP_TYPE_FUNCTION: return NULL;
  }
end:
  setb("NULL")
}
size_t cup_type_put_pointer(CUPState *state, CUPType value) {
  assert(state != NULL);
  assert(value.realtype == CUP_TYPE_POINTER);
  char* key = NULL;
  size_t size = 1 + 1;
  for (CUPType* t = value.type; ;) {
    char* n = type_name(t);
    if (n)
      size += strlen(n);
    else {
      if (t->realtype == CUP_TYPE_POINTER) {
        cup_type_put_pointer(state, *t);
      }
	case CUP_TYPE_STRUCT: return NULL;
	case CUP_TYPE_POINTER: return NULL;
  case CUP_TYPE_ARRAY: return NULL;
  case CUP_TYPE_FUNCTION: return NULL;

    }
  }
  
  hashmap_put(state->types, key, value);
}
void cup_type_put_array(CUPState *state, CUPType value) {
  if (state == NULL) return;
  assert(value.realtype == CUP_TYPE_ARRAY);
  char* key = NULL;
  size_t size = 1 + 1;
  hashmap_put(state->types, key, value);
}
void cup_type_put_function(CUPState *state, CUPType value) {
  if (state == NULL) return;
  assert(value.realtype == CUP_TYPE_FUNCTION);
  char* key = NULL;
  size_t size = 1 + 1;
  hashmap_put(state->types, key, value);
}
