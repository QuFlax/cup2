#include "../include/cup.h"

#ifndef CUP_DEFTYPE_MAX_PASSES
#define CUP_DEFTYPE_MAX_PASSES 64
#endif

CVecT(CVariable);


static int is_in_function = 0;
static CUPType cup_count = { 0, 0, CUP_TYPE_COUNT, {} };
//static size_t cup_type_changes = 0;

const CUPType* promote(const CUPType* left, const CUPType* right) {
  if (left->realtype != right->realtype)
    return NULL;
  if (left->realtype != CUP_TYPE_FUNCTION)
    return left;
  if (left->size != right->size) {
    cup_error("promote: function size mismatch");
    return NULL;
  }
  for (size_t i = 0;; i++) {
    const CUPType *l = left->elements[i];
    const CUPType *r = right->elements[i];
    if (i > 0 && (l == &cup_type_void || r == &cup_type_void)) {
      if (l != &cup_type_void || r != &cup_type_void) {
        cup_error("promote: function argument count mismatch");
        return NULL;
      }
      break;
    }
    if (l == NULL || r == NULL)
      continue;
    if (promote(l, r) == NULL)
      return NULL;
  }
  return left;
}

static void* cup_any = &cup_count.size;
static CValue defTypeCount(size_t size) {
  cup_count.size = size;
  return (CValue){&cup_count, NULL, NULL};
}

int defTypeU(CUPState *state, Nodes nodes, size_t* i, CValue* rvalue, const CUPType** rtype);

static const CUPType* argsgen(CUPState* state, Nodes nodes, size_t* i, CValue left, size_t max) {
  CValue* vs = cup_malloc(sizeof(CValue) * (max + 1));
  vs[0] = left;
  memset(vs + 1, 0, sizeof(CValue) * max);
  for (size_t j = 1; j <= max; j++) {
    if (defTypeU(state, nodes, i, vs + j, NULL))
      goto err;
    if (vs[j].type == NULL)
      continue;
    if (vs[j].type->realtype == CUP_TYPE_COUNT) {
      cup_error("args: call argument type is CUP_TYPE_COUNT");
      goto err;
    }
  }
  CValue value = {};
  CUP_GEN_RESULT r = left.type->gen(state, vs, max, &value);
  if (r == CUP_GEN_ERROR || r == CUP_GEN_CONTINUE || value.type == NULL) {
    cup_error("args: call fngen error");
    goto err;
  }
  // DOTO change left value
  cup_free(vs);
  return value.type;
err:
  cup_free(vs);
  return &cup_count;
}
static const CUPType* argsfn(CUPState* state, Nodes nodes, size_t* i, const CUPType* fn, size_t max) {
  size_t j = 0;
  while (j < max) {
    CValue arg_type = {};
    if (defTypeU(state, nodes, i, &arg_type, NULL))
      return &cup_count;
    if (arg_type.type == NULL)
      goto next;
    if (arg_type.type->realtype == CUP_TYPE_COUNT) {
      cup_error("args: argument type is CUP_TYPE_COUNT");
      return &cup_count;
    }
    if (fn->elements[j + 1] == &cup_type_void)
      goto next;

    if (fn->elements[j + 1] == NULL) {
      fn->elements[j + 1] = arg_type.type;
      goto next;
    }

    if (promote(fn->elements[j + 1], arg_type.type) == NULL) {
      char tname[256], tname2[256];
      cup_type_snname(tname, sizeof(tname), fn->elements[j + 1]);
      cup_type_snname(tname2, sizeof(tname2), arg_type.type);
      cup_errorf("argsfn %zu type mismatch: get %s expected %s", j, tname2, tname);
      return &cup_count;
    }
next:
    if (fn->elements[j + 1] == &cup_type_void) {
      cup_error("args: argument count mismatch");
      return &cup_count;
    }
    if (arg_type.variable && arg_type.variable->type == NULL && fn->elements[j + 1] != NULL)
      arg_type.variable->type = fn->elements[j + 1];
    j++;
  }
  if (fn->elements[max + 1] != &cup_type_void) {
    cup_error("args: argument count mismatch");
    return &cup_count;
  }
  return fn->elements[0];
}

static inline Node* getNode(Nodes nodes, size_t* i) {
  if (*i >= (nodes.size / sizeof(*nodes.data))) {
    cup_error("Incorrect Nodes");
    exit(1);
  }
  return nodes.data + (*i)++;
}

int defTypeUR(CUPState *state, Nodes nodes, size_t* i, CValue* value, const CUPType** rtype);

int defTypeU(CUPState *state, Nodes nodes, size_t* i, CValue* rvalue, const CUPType** rtype) {
  if (!state) {
    cup_error("defType state are NULL");
    return 1;
  }
  Node n = *getNode(nodes, i);
  switch (n.token) {
  case N_BLOCK:
    state->scope++;
  case T_COMMA:
    *rvalue = defTypeCount(getNode(nodes, i)->value);
    return 0;
  case N_ARRAY: {
    CValue left = {&cup_type_void, NULL, NULL};
    size_t* id = &(getNode(nodes, i)->value);
    if (*id) {
      if(defTypeU(state, nodes, i, &left, NULL))
        return 1;
    }
    *rvalue = (CValue){cup_type_put(state,
        cup_type_array(left.type, *id)
    ), id, NULL};
    return 0;
  }
  case T_NUMBER: {
    *rvalue = (CValue){&cup_type_int, &(getNode(nodes, i)->value), NULL};
    return 0;
  }
  case T_DOUBLE: {
    *rvalue = (CValue){&cup_type_double, &(getNode(nodes, i)->value), NULL};
    return 0;
  }
  case T_STRING: case T_MSTRING: {
    size_t* id = &(getNode(nodes, i)->value);
    *rvalue = (CValue){cup_type_put(state,
      cup_type_array(&cup_type_uint8, strlen((char*)getData(state, *id)) + 1)
    ), id, NULL};
    return 0;
  }
  case T_ADD: case T_SUB: case T_MUL: case T_DIV: case T_MOD: {
    CValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;
    CValue right = {};
    if(defTypeU(state, nodes, i, &right, NULL))
      return 1;
    if (left.type == NULL || right.type == NULL)
      return 0;
    if (left.type->realtype == CUP_TYPE_COUNT || right.type->realtype == CUP_TYPE_COUNT)
      return 1;
    const CUPType* type = promote(left.type, right.type);
    if (type == NULL)
      return 0;
    *rvalue = (CValue){type, cup_any, NULL};
    return 0;
  }
  case T_EQEQ: case T_NOTEQ: case T_LESS: case T_LESSEQ: case T_GREAT: case T_GREATEQ: {
    CValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;
    CValue right = {};
    if(defTypeU(state, nodes, i, &right, NULL))
      return 1;
    if (left.type == NULL || right.type == NULL)
      return 0;
    if (left.type->realtype == CUP_TYPE_COUNT || right.type->realtype == CUP_TYPE_COUNT)
      return 1;
    const CUPType* type = promote(left.type, right.type);
    if (type == NULL)
      return 0;
    *rvalue = (CValue){&cup_type_int, cup_any, NULL};
    return 0;
  }
  case T_ADDEQ: case T_EQ: {
    CValue right = {};
    if(defTypeU(state, nodes, i, &right, NULL))
      return 1;
    CValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;
    if (left.variable == NULL) {
      cup_error("!lhs.isLValue");
      return 1;
    }
    if (right.type == NULL)
      return 0;
    if (right.type->realtype == CUP_TYPE_COUNT)
      return 1;
    if (left.type == NULL) {
      left.variable->type = right.type;
      *rvalue = right;
      return 0;
    }
    if (left.type->realtype == CUP_TYPE_COUNT)
      return 1;
    if (left.type != right.type) {
      cup_error("!canConvert(rhs.type, lhs.type)");
      return 1;
    }
    *rvalue = right;
    return 0;
  }
  case T_RETURN: {
    CValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;

    if (left.type == NULL)
      return 0;
    if (left.type->realtype == CUP_TYPE_COUNT)
      return 1;
    if (rtype == NULL) {
      cup_error("RETURN IN NOT THERE");
      return 1;
    }
    if (*rtype == NULL) {
      *rtype = left.type;
      *rvalue = left;
      return 0;
    }

    //if (!canConvert(value.type, currentFunction->returnType))
    //    error(...);
    if (*rtype != left.type) {
      cup_error("!canConvert(value.type, currentFunction->returnType)");
      return 1;
    }
    *rvalue = left;
    return 0;
  }
  case N_VARIABLE: {
    CVariable *var = getVars(state, getNode(nodes, i)->value, state->scope);
    if (var == NULL) {
      cup_error("getVarScoped(state, getNode(nodes, i).value, SIZE_MAX)");
      return 1;
    }
    *rvalue = (CValue){var->type, &var->value, var};
    return 0;
  }
  case T_AT: {
    // TODO: do something with return because we need to consume all nodes range
    CValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;
    CVariable* var = left.variable;
    state->scope++;
    CValue right = {};
    if(defTypeU(state, nodes, i, &right, NULL))
      return 1;
    if (var == NULL || left.type == NULL) {
      cup_error("!fn");
      return 1;
    }
    if (right.type == NULL || right.type->realtype != CUP_TYPE_COUNT) {
      cup_error("!fn(args)");
      return 1;
    }
    if (left.type->realtype != CUP_TYPE_FUNCTION
      //&& left.type->realtype != CUP_TYPE_GENERATOR // AT not needed
    ) {
      char tname[256];
      cup_type_snname(tname, sizeof(tname), left.type);
      cup_errorf("defType call not Function %s", tname);
      return 1;
    }
    //const CUPType *r = bindFunctionArgs(state, nodes, left.type, right.type->size);
    const CUPType *r = argsfn(state, nodes, i, left.type, right.type->size);
    if (r == &cup_count)
      return 1;
    const CUPType *type = NULL;
    if (defTypeUR(state, nodes, i, NULL, &type))
        return 1;
    if (type != NULL) {
      // TODO: promote
      left.type->elements[0] = type;
    }
    if (left.type->elements[0] == NULL)
      return 0;
    *rvalue = left;
    return 0;
  }
  case T_OSB: {
    CValue left = {};
    if (defTypeU(state, nodes, i, &left, NULL))
      return 1;
    CValue index = {};
    if (defTypeU(state, nodes, i, &index, NULL))
      return 1;
    if (left.type == NULL || index.type == NULL)
      return 0;
    if (left.type->realtype == CUP_TYPE_COUNT ||
      index.type->realtype == CUP_TYPE_COUNT)
      return 1;
    if (left.type->realtype == CUP_TYPE_ARRAY) {
      if (left.type->elements == NULL || left.type->elements[0] == NULL) {
        cup_error("T_OSB: array has no element type");
        return 1;
      }
      *rvalue = (CValue){ left.type->elements[0], cup_any, NULL };
      return 0;
    } else if (left.type->realtype == CUP_TYPE_POINTER) {
      if (left.type->elements == NULL || left.type->elements[0] == NULL) {
        cup_error("T_OSB: pointer has no pointee type");
        return 1;
      }
      *rvalue = (CValue){ left.type->elements[0], cup_any, NULL };
      return 0;
    } else {
      char tname[256];
      cup_type_snname(tname, sizeof(tname), left.type);
      cup_errorf("T_OSB: cannot index type %s", tname);
      return 1;
    }
  }
  case T_CALL: {
    CValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;
    CVariable* var = left.variable;
    CValue right = {};
    if(defTypeU(state, nodes, i, &right, NULL))
      return 1;
    if (var == NULL || left.type == NULL) {
      cup_error("!fn");
      return 1;
    }
    if (right.type == NULL || right.type->realtype != CUP_TYPE_COUNT) {
      cup_error("!fn(args)");
      return 1;
    }
    const CUPType* type = NULL;
    if (left.type->realtype == CUP_TYPE_GENERATOR) {
      type = argsgen(state, nodes, i, left, right.type->size);
    } else if (left.type->realtype == CUP_TYPE_FUNCTION) {
      type = argsfn(state, nodes, i, left.type, right.type->size);
    } else {
      char tname[256];
      cup_type_snname(tname, sizeof(tname), left.type);
      cup_errorf("defType call not Function or Generator get %s", tname);
      return 1;
    }
    if (type == &cup_count)
      return 1;
    *rvalue = (CValue) {type, &cup_count.size, NULL};
    return 0;
  }
  case T_BREAK: case T_CONTINUE: {
    *rvalue = (CValue){&cup_type_void, NULL, NULL};
    return 0;
  }
  case T_IF: {
    CValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;
    if (left.type && left.type->realtype == CUP_TYPE_COUNT)
      return 1;
    CValue result = (CValue){};
    if(defTypeUR(state, nodes, i, &result, rtype))
      return 1;
    *rvalue = result;
    return 0;
  }
  case T_WHILE: {
    CValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;
    if (left.type && left.type->realtype == CUP_TYPE_COUNT)
      return 1;
    CValue result = (CValue){};
    if(defTypeUR(state, nodes, i, &result, rtype))
      return 1;
    *rvalue = result;
    return 0;
  }
  /*
  case T_ELSE: {
    if (nodes->it >= nodes->end || nodes->it->token != T_IF) {
      cup_error("T_ELSE is not followed by T_IF");
      return defTypeCount(0);
    }
    (void)getNode(nodes);
    CValue condition = defTypeTree(state, nodes);
    CValue then_body = defTypeTree(state, nodes);
    CValue else_body = defTypeTree(state, nodes);
    if (condition.type == NULL || then_body.type == NULL || else_body.type == NULL)
      return (CValue){};
    return (CValue){&cup_type_void, NULL};
  }
  case T_WHILE: {
    CValue condition = defTypeTree(state, nodes);
    CValue body = defTypeTree(state, nodes);
    if (condition.type == NULL || body.type == NULL)
      return (CValue){};
    return (CValue){&cup_type_void, NULL};
  }
  */
  default:
    cup_errorf("defType UNKNOWN node make for '%s'", token_names[n.token]);
    break;
  }
  return 1;
}

int defTypeUR(CUPState *state, Nodes nodes, size_t* i, CValue* value, const CUPType** rtype) {
  if (!state) {
    cup_error("defType state are NULL");
    return 1;
  }
  size_t max = 1;
  for (size_t j = 0; j < max; j++) {
    CValue item = {};
    if (defTypeU(state, nodes, i, &item, rtype))
      return 1;
    if (item.type == NULL) {
      continue;
    }
    if (item.type->realtype == CUP_TYPE_COUNT) {
      max += item.type->size;
      continue;
    }
    if (value && value->type == NULL) {
      *value = item;
      continue;
    }
  }
  return 0;
}

/*
static CValue defTypeTree(CUPState *state, NRange *nodes) {
  if (!nodes || nodes->it >= nodes->end)
    return (CValue){};
  CTType token = nodes->it->token;
  CValue value = defTypeEx(state, nodes);
  if (token == N_BLOCK && value.type &&
      value.type->realtype == CUP_TYPE_COUNT) {
    size_t count = value.type->size;
    CValue last = {&cup_type_void, NULL};
    for (size_t i = 0; i < count; i++) {
      CValue item = defTypeTree(state, nodes);
      if (item.type == NULL)
        return item;
      last = item;
    }
    return last;
  }
  return value;
}
  */

//const CUPType* args_types[256] = {};
//int defTypeArgs = 0;

/*
static void defTypeRange(CUPState *state, NRange range,
    CVector *unresolved, CValue *result, int *progress) {
  size_t count = 1;

  for (size_t i = 0; i < count; i++) {
    NRange begin = range;

    // It is temporary state belonging to the expression currently being inspected.
    state->variable = NULL;
    CValue value = defTypeEx(state, &range);
    state->variable = NULL;

    if (value.type == NULL) {
      // Save the range before defTypeEx advanced it. The whole expression will be retried during the next inference pass.
      
      vector_pushT(*unresolved, begin);
      continue;
    }

    *progress = 1;

    if (value.type->realtype == CUP_TYPE_COUNT) {
      count += value.type->size;
      continue;
    }

    *result = value;
  }
}
*/

static void defTypeErr(CVector pending, const char* err) {
  size_t unresolved_count = pending.size / sizeof(*pending.data);
  cup_errorf(err, unresolved_count, unresolved_count == 1 ? "" : "s");
  cup_free(pending.data);
}

typedef struct SSC {
  size_t index;
  size_t scope;
} SSC;


int defType(CUPState *state, Nodes* nodes) {
  if (state == NULL) {
    cup_error("defType: state is NULL");
    return 1;
  }
  SSC index = {0, 0};
  CVector pending = {};
  vector_pushT(pending, index);
  size_t pass = 0;
  do {
    size_t count = 1;
    SSC begin = *(SSC*)pending.data;
    state->scope = begin.scope;
    for (size_t i = 0; i < count; i++) {
      CValue value = {};
      SSC save = begin;
      if (defTypeU(state, *nodes, &begin.index, &value, NULL)) {
        defTypeErr(pending,
          "defType: could not resolve %zu expression%s "
          "(undefined symbol or cyclic type dependency)");
        return 1;
      }

      if (value.type == NULL) {
        vector_pushT(pending, save);
        continue;
      }
      if (value.type->realtype == CUP_TYPE_COUNT) {
        count += value.type->size;
        continue;
      }

    }
    vector_fpop(&pending, sizeof(SSC));
    if (pass++ >= CUP_DEFTYPE_MAX_PASSES) {
      defTypeErr(pending,
        "defType: maximum inference passes reached; "
        "%zu expression%s remain unresolved");
      return 1;
    }
  } while(pending.size != 0);
  return 0;
}

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
          return INT_MAX;
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