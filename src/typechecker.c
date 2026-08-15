#include "../include/cup.h"

#ifndef CUP_DEFTYPE_MAX_PASSES
#define CUP_DEFTYPE_MAX_PASSES 64
#endif

static CUPType cup_count = { 0, 0, CUP_TYPE_COUNT, {} };
static size_t cup_type_changes = 0;

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
  return (CValue){&cup_count, NULL};
}

int defTypeU(CUPState *state, NRange *nodes, CValue* value);

static const CUPType* argsgen(CUPState* state, NRange* nodes, CValue left, size_t max) {
  const CUPType* fn = left.type;
  const CUPType *result = NULL;
  size_t i = 0;
  do {
    CValue arg_type = {};
    if (i) {
      if (defTypeU(state, nodes, &arg_type))
        return &cup_count;
      if (arg_type.type == NULL)
        continue;
      if (arg_type.type->realtype == CUP_TYPE_COUNT) {
        cup_error("args: call argument type is CUP_TYPE_COUNT");
        return &cup_count;
      }
    }
    CValue value = fn->gen(state, arg_type, i++, max);
    if (value.type == NULL) {
      cup_error("args: call fngen returned NULL type");
      return &cup_count;
    }
    if (result == NULL) {
      result = value.type;
      continue;
    }
    if (promote(value.type, result) == NULL) {
      char tname[256], tname2[256];
      cup_type_snname(tname, sizeof(tname), result);
      cup_type_snname(tname2, sizeof(tname2), value.type);
      cup_errorf("argsgen return type mismatch: get %s expected %s", tname2, tname);
      return &cup_count;
    }
  } while(i <= max);
  return result;
}
static const CUPType* argsfn(CUPState* state, NRange* nodes, const CUPType* fn, size_t max) {
  size_t i = 0;
  while (i < max) {
    state->variable = NULL;
    CValue arg_type = {};
    if (defTypeU(state, nodes, &arg_type))
      return &cup_count;
    if (arg_type.type == NULL)
      goto next;
    if (arg_type.type->realtype == CUP_TYPE_COUNT) {
      cup_error("args: argument type is CUP_TYPE_COUNT");
      return &cup_count;
    }
    if (fn->elements[i + 1] == &cup_type_void)
      goto next;

    if (fn->elements[i + 1] == NULL) {
      fn->elements[i + 1] = arg_type.type;
      //if (state->variable && state->variable->type == NULL)
      //  state->variable->type = fn->elements[i + 1];
      goto next;
    }
    //if (state->variable && state->variable->type == NULL)
    //  state->variable->type = fn->elements[i + 1];

    if (promote(fn->elements[i + 1], arg_type.type) == NULL) {
      char tname[256], tname2[256];
      cup_type_snname(tname, sizeof(tname), fn->elements[i + 1]);
      cup_type_snname(tname2, sizeof(tname2), arg_type.type);
      cup_errorf("argsfn %zu type mismatch: get %s expected %s", i, tname2, tname);
      return &cup_count;
    }
next:
    if (fn->elements[i + 1] == &cup_type_void) {
      cup_error("args: argument count mismatch");
      return &cup_count;
    }
    if (state->variable && state->variable->type == NULL && fn->elements[i + 1] != NULL)
      state->variable->type = fn->elements[i + 1];
    i++;
  }
  if (fn->elements[max + 1] != &cup_type_void) {
    cup_error("args: argument count mismatch");
    return &cup_count;
  }
  return fn->elements[0];
}

int defTypeU(CUPState *state, NRange *nodes, CValue* value) {
  if (!nodes || !state) {
    cup_error("defType state or nodes are NULL");
    return 1;
  }
  Node n = *getNode(nodes);
  switch (n.token) {
  case N_BLOCK: case T_COMMA: {
    *value = defTypeCount(getNode(nodes)->value);
    return 0;
  }
  case T_NUMBER: {
    *value = (CValue){&cup_type_int, &(getNode(nodes)->value)};
    return 0;
  }
  case T_DOUBLE: {
    *value = (CValue){&cup_type_double, &(getNode(nodes)->value)};
    return 0;
  }
  case T_STRING: case T_MSTRING: {
    size_t* id = &(getNode(nodes)->value);
    *value = (CValue){cup_type_put(state,
      cup_type_array(&cup_type_uint8, strlen((char*)getData(state, *id)) + 1)
    ), id};
    return 0;
  }
  case T_ADD: case T_SUB: case T_MUL: case T_DIV: case T_MOD: {
    CValue left = {};
    if(defTypeU(state, nodes, &left))
      return 1;
    CValue right = {};
    if(defTypeU(state, nodes, &right))
      return 1;
    if (left.type == NULL || right.type == NULL)
      return 0;
    if (left.type->realtype == CUP_TYPE_COUNT || right.type->realtype == CUP_TYPE_COUNT)
      return 1;
    const CUPType* type = promote(left.type, right.type);
    if (type == NULL)
      return 0;
    *value = (CValue){type, cup_any};
    return 0;
  }
  case T_EQEQ: case T_NOTEQ: case T_LESS: case T_LESSEQ: case T_GREAT: case T_GREATEQ: {
    CValue left = {};
    if(defTypeU(state, nodes, &left))
      return 1;
    CValue right = {};
    if(defTypeU(state, nodes, &right))
      return 1;
    if (left.type == NULL || right.type == NULL)
      return 0;
    if (left.type->realtype == CUP_TYPE_COUNT || right.type->realtype == CUP_TYPE_COUNT)
      return 1;
    const CUPType* type = promote(left.type, right.type);
    if (type == NULL)
      return 0;
    *value = (CValue){&cup_type_int, cup_any};
    return 0;
  }
  case T_EQ: {
    CValue right = {};
    if(defTypeU(state, nodes, &right))
      return 1;
    CValue left = {};
    if(defTypeU(state, nodes, &left))
      return 1;
    if (state->variable == NULL) {
      cup_error("!lhs.isLValue");
      return 1;
    }
    if (right.type == NULL)
      return 0;
    if (right.type->realtype == CUP_TYPE_COUNT)
      return 1;
    if (left.type == NULL) {
      state->variable->type = right.type;
      *value = right;
      return 0;
    }
    if (left.type->realtype == CUP_TYPE_COUNT)
      return 1;
    if (left.type != right.type) {
      cup_error("!canConvert(rhs.type, lhs.type)");
      return 1;
    }
    *value = right;
    return 0;
  }
  case T_RETURN: {
    CValue left = {};
    if(defTypeU(state, nodes, &left))
      return 1;

    if (left.type == NULL)
      return 0;
    if (left.type->realtype == CUP_TYPE_COUNT)
      return 1;
    if (state->nodes.vtype == NULL) {
      state->nodes.vtype = left.type;
      *value = left;
      return 0;
    }

    //if (!canConvert(value.type, currentFunction->returnType))
    //    error(...);
    if (state->nodes.vtype != left.type) {
      cup_error("!canConvert(value.type, currentFunction->returnType)");
      return 1;
    }
    *value = left;
    return 0;
  }
  case N_VARIABLE: {
    CVariable* var = getVarScoped(state, getNode(nodes)->value, CVARS_MAX);
    if (var == NULL) {
      cup_error("getVarScoped(state, getNode(nodes).value, CVARS_MAX)");
      return 1;
    }
    state->variable = var;
    *value = (CValue){var->type, &var->value};
    return 0;
  }
  case T_AT: {
    // TODO: do something with return because we need to consume all nodes range
    CValue left = {};
    if(defTypeU(state, nodes, &left))
      return 1;
    CVariable* var = state->variable;
    CValue right = {};
    if(defTypeU(state, nodes, &right))
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
    const CUPType *r = argsfn(state, nodes, left.type, right.type->size);
    if (r == &cup_count)
      return 1;
    const CUPType *saved_return = state->nodes.vtype;
    state->nodes.vtype = NULL;
    CValue result = left;
    size_t max = 1;
    for (size_t i = 0; i < max; i++) {
      CValue item = {};
      if (defTypeU(state, nodes, &item))
        return 1;
      if (item.type == NULL) {
        result = item;
        continue;
      }
      if (item.type->realtype == CUP_TYPE_COUNT) {
        max += item.type->size;
        continue;
      }
    }
    if (state->nodes.vtype != NULL) {
      // TODO: promote
      left.type->elements[0] = state->nodes.vtype;
    }
    state->nodes.vtype = saved_return;
    if (left.type->elements[0] == NULL)
      return 0;
    *value = result;
    return 0;
    /*
    if (body.type == NULL || body.type->realtype != CUP_TYPE_COUNT) {
      state->nodes.vtype = saved_return;
      cup_error("defType: invalid function body");
      return 1;
    }
    if (state->nodes.vtype != NULL) {
      // TODO: promote
      left.type->elements[0] = state->nodes.vtype;
    }
    state->nodes.vtype = saved_return;
    if (left.type->elements[0] == NULL)
      return 0;
    *value = left;
    return 0;
    */
  }
  case T_CALL: {
    CValue left = {};
    if(defTypeU(state, nodes, &left))
      return 1;
    CVariable* var = state->variable;
    CValue right = {};
    if(defTypeU(state, nodes, &right))
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
      type = argsgen(state, nodes, left, right.type->size);
    } else if (left.type->realtype == CUP_TYPE_FUNCTION) {
      type = argsfn(state, nodes, left.type, right.type->size);
    } else {
      char tname[256];
      cup_type_snname(tname, sizeof(tname), left.type);
      cup_errorf("defType call not Function or Generator get %s", tname);
      return 1;
    }
    if (type == &cup_count)
      return 1;
    *value = (CValue) {type, &cup_count.size};
    return 0;
  }
  case T_BREAK: case T_CONTINUE: {
    *value = (CValue){&cup_type_void, NULL};
    return 0;
  }
  /*
  case T_IF: {
    CValue condition = defTypeTree(state, nodes);
    if (condition.type == NULL)
      return (CValue){};
    CValue body = defTypeTree(state, nodes);
    if (body.type == NULL)
      return (CValue){};
    return (CValue){&cup_type_void, NULL};
  }
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

static void defTypeErr(CUPState *state, NRanges pending, const char* err) {
  size_t unresolved_count = pending.size / sizeof(*pending.data);
  cup_errorf(err, unresolved_count, unresolved_count == 1 ? "" : "s");
  cup_free(pending.data);
  state->variable = NULL;
}

int defType(CUPState *state, NRange nodes) {
  if (state == NULL) {
    cup_error("defType: state is NULL");
    return 1;
  }
  NRanges pending = {};
  vector_pushT(pending.vec, nodes);
  //CValue result = {};
  int result = 1;
  size_t pass = 0;

  do {
    size_t changes_before = cup_type_changes;
    NRanges next = {};
    int progress = 0;
    while (pending.size) {
      //defTypeRange(state, *pending.data, &next.vec, &result, &progress);
      size_t count = 1;
      for (size_t i = 0; i < count; i++) {
        NRange begin = *pending.data;
        // It is temporary state belonging to the expression currently being inspected.
        state->variable = NULL;
        CValue value = {};
        int r = defTypeU(state, pending.data, &value);
        state->variable = NULL;
        if (value.type == NULL) {
          vector_pushT(next.vec, begin);
          continue;
        }
        if (progress != 2) progress = 1;
        if (r) {
          progress = 2;
          continue;
        }
        if (value.type->realtype == CUP_TYPE_COUNT) {
          count += value.type->size;
          continue;
        }
        //result = value;
        result = 0;
      }
      vector_fpopT(pending);
    }
    pending = next;

    /* Inferring a parameter or return slot is progress even when the
     * expression itself must remain pending until the next pass. */
    if (cup_type_changes != changes_before && progress == 0)
      progress = 1;

    if (progress == 0 || progress == 2) {
      defTypeErr(state, pending,
        "defType: could not resolve %zu expression%s "
        "(undefined symbol or cyclic type dependency)");
      return 1;
    }
    if (pass++ >= CUP_DEFTYPE_MAX_PASSES) {
      defTypeErr(state, pending,
        "defType: maximum inference passes reached; "
        "%zu expression%s remain unresolved");
      return 1;
    }
  } while(pending.size != 0);
  state->variable = NULL;
  return result;
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