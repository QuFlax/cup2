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

CValue defTypeEx(CUPState *state, NRange* nodes);

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

static const CUPType *bindFunctionArgs(
    CUPState *state,
    NRange *nodes,
    const CUPType *fn,
    size_t argc
) {
  for (size_t i = 0; i < argc; i++) {
    const CUPType *parameter_type = fn->elements[i + 1];

    if (parameter_type == &cup_type_void) {
      cup_error("defType: function parameter count mismatch");
      return &cup_count;
    }

    /*
     * Function arguments currently need to be variables.
     * defTypeEx(N_VARIABLE) sets state->variable.
     */
    state->variable = NULL;
    CValue parameter = defTypeEx(state, nodes);
    CVariable *variable = state->variable;

    if (variable == NULL) {
      cup_errorf("defType: function argument %zu is not a variable", i);
      return &cup_count;
    }

    /*
     * Signature already knows the type:
     *
     *     function: int(int, int)
     *     variable: b = NULL
     *
     * Propagate int into b.
     */
    if (parameter_type != NULL) {
      if (variable->type == NULL) {
        variable->type = parameter_type;
      } else if (promote(variable->type, parameter_type) == NULL) {
        char actual_name[256];
        char expected_name[256];

        cup_type_snname(
            actual_name,
            sizeof(actual_name),
            variable->type
        );

        cup_type_snname(
            expected_name,
            sizeof(expected_name),
            parameter_type
        );

        cup_errorf(
            "defType function arg %zu type mismatch: get %s expected %s",
            i,
            actual_name,
            expected_name
        );

        return &cup_count;
      }

      continue;
    }

    /*
     * Signature parameter is unknown/any.
     * If the variable already has a type, propagate it back.
     */
    if (variable->type != NULL)
      fn->elements[i + 1] = variable->type;

    (void)parameter;
  }

  if (fn->elements[argc + 1] != &cup_type_void) {
    cup_error("defType: function parameter count mismatch");
    return &cup_count;
  }

  return fn->elements[0];
}

const CUPType* args(CUPState* state, NRange* nodes, const CUPType* fn, size_t max) {
  CValues values = {};
  const CUPType *result = &cup_count;

  for (size_t i = 0; i < max; i++) {
    CValue arg_type = defTypeEx(state, nodes);
    if (arg_type.type && arg_type.type->realtype == CUP_TYPE_COUNT)
      goto done;
    vector_pushT(values.vec, arg_type);
  }

  if (fn->realtype == CUP_TYPE_GENERATOR) {
    CValue v = fn->gen(state, values.data, max);
    if (v.type != NULL)
      result = v.type;
    goto done;
  }

  for (size_t i = 0; i < max; i++) {
    const CUPType *expected = fn->elements[i + 1];
    const CUPType *actual = values.data[i].type;

    if (expected == &cup_type_void) {
      cup_error("defType: call argument count mismatch");
      goto done;
    }

    /* NULL means an unknown/any parameter slot. Infer it from a known
     * argument, but leave it NULL when the argument is unresolved. */
    if (expected == NULL) {
      if (actual != NULL) {
        fn->elements[i + 1] = actual;
        cup_type_changes++;
      }
      continue;
    }

    if (actual == NULL || promote(actual, expected) == NULL) {
      char tname[256], tname2[256];
      cup_type_snname(tname, sizeof(tname), expected);
      cup_type_snname(tname2, sizeof(tname2), actual);
      cup_errorf("defType arg %zu type mismatch: get %s expected %s",
                 i, tname2, tname);
    }
  }

  if (fn->elements[max + 1] != &cup_type_void)
    cup_error("defType: call argument count mismatch");

  result = fn->elements[0];

done:
  cup_free(values.data);
  return result;
}

static CValue defTypeCount(size_t size) {
  cup_count.size = size;
  return (CValue){&cup_count, NULL};
}

//const CUPType* args_types[256] = {};
//int defTypeArgs = 0;

CValue defTypeEx(CUPState *state, NRange* nodes) {
  if (!nodes || !state) {
    cup_error("defType state or nodes are NULL");
    return defTypeCount(0);
  }
  Node n = *getNode(nodes);
  switch (n.token) {
  case N_UNARY:
    return defTypeEx(state, nodes);
  case N_BLOCK: case T_COMMA:
  {
    return defTypeCount(getNode(nodes)->value);
  }
  case T_NUMBER:
    return (CValue){&cup_type_int, &(getNode(nodes)->value)};
  case T_DOUBLE:
    return (CValue){&cup_type_double, &(getNode(nodes)->value)};
  case T_STRING:
  case T_MSTRING: {
    size_t* value = &(getNode(nodes)->value);
    return (CValue){cup_type_put(state,
      cup_type_array(&cup_type_uint8, strlen((char*)getData(state, *value)) + 1)
    ), value};
  }
  case T_ADD: case T_SUB: case T_MUL: case T_DIV: case T_MOD:
  {
    CValue left  = defTypeEx(state, nodes);
    CValue right = defTypeEx(state, nodes);
    if (left.type == NULL || right.type == NULL)
      return (CValue){};
    const CUPType* type = promote(left.type, right.type);
    if (type == NULL)
      return (CValue){};
    return (CValue){type, &cup_count.size};
  }
  case T_EQEQ: case T_NOTEQ:
  case T_LESS: case T_LESSEQ: case T_GREAT: case T_GREATEQ:
  {
    CValue left  = defTypeEx(state, nodes);
    CValue right = defTypeEx(state, nodes);
    if (left.type == NULL || right.type == NULL ||
        promote(left.type, right.type) == NULL)
      return (CValue){};
    return (CValue){&cup_type_int, &cup_count.size};
  }
  case T_EQ:
  {
    CValue right = defTypeEx(state, nodes);
    CValue left  = defTypeEx(state, nodes);

    //if (!lhs.isLValue)
    //    error(...);
    if (state->variable == NULL) {
      cup_error("!lhs.isLValue");
      return defTypeCount(0);
    }
    if (right.type == NULL)
      return (CValue){};
    if (left.type == NULL) {
      state->variable->type = right.type;
      return right;
    }

    //if (!canConvert(rhs.type, lhs.type))
    //    error(...);
    if (left.type != right.type) {
      cup_error("!canConvert(rhs.type, lhs.type)");
      return defTypeCount(0);
    }
    return right;
  }
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
  case T_BREAK:
  case T_CONTINUE:
    return (CValue){&cup_type_void, NULL};
  case T_RETURN: {
    CValue left = defTypeEx(state, nodes);

    if (left.type == NULL)
      return (CValue){};
    if (state->nodes.vtype == NULL) {
      state->nodes.vtype = left.type;
      return left;
    }

    //if (!canConvert(value.type, currentFunction->returnType))
    //    error(...);
    if (state->nodes.vtype != left.type) {
      cup_error("!canConvert(value.type, currentFunction->returnType)");
      return defTypeCount(0);
    }
    return left;
  }
  case N_VARIABLE: {
    CVariable* var = getVarScoped(state, getNode(nodes)->value, CVARS_MAX);
    if (var == NULL) {
      cup_error("getVarScoped(state, getNode(nodes).value, CVARS_MAX)");
      exit(22);
    }
    state->variable = var;
    //if (defTypeArgs && var->type == NULL) {
    //  var->type = args_types[(defTypeArgs++) - 1];
    //}
    return (CValue){var->type, &var->value};
  }
  case T_AT: {
    CValue left = defTypeEx(state, nodes);
    if (state->variable == NULL) {
      cup_error("defType: function name is not a variable");
      return defTypeCount(0);
    }
    CValue argc = defTypeEx(state, nodes);
    if (left.type == NULL || left.type->realtype != CUP_TYPE_FUNCTION ||
      argc.type == NULL || argc.type->realtype != CUP_TYPE_COUNT) {
      cup_error("defType: invalid function definition");
      return defTypeCount(0);
    }
    const CUPType *r = bindFunctionArgs(state, nodes, left.type, argc.type->size);
    if (r == &cup_count)
      return defTypeCount(0);
    const CUPType *saved_return = state->nodes.vtype;
    state->nodes.vtype = NULL;
    CValue body = defTypeEx(state, nodes);
    if (body.type == NULL || body.type->realtype != CUP_TYPE_COUNT) {
      state->nodes.vtype = saved_return;
      cup_error("defType: invalid function body");
      return defTypeCount(0);
    }
    size_t count = body.type->size;
    int unresolved = 0;
    for (size_t i = 0; i < count; i++) {
      CValue expression = defTypeEx(state, nodes);
      if (expression.type == NULL) {
        unresolved = 1;
        continue;
      }
      if (expression.type->realtype == CUP_TYPE_COUNT) {
        if (expression.type->size == 0) {
          state->nodes.vtype = saved_return;
          return defTypeCount(0);
        }
        count += expression.type->size;
      }
    }
    if (state->nodes.vtype != NULL)
      left.type->elements[0] = state->nodes.vtype;
    const CUPType *return_type = left.type->elements[0];
    state->nodes.vtype = saved_return;
    if (unresolved || return_type == NULL)
      return (CValue){};
    return left;
  }
  case T_CALL: {
    CValue left = defTypeEx(state, nodes);
    if (state->variable == NULL || left.type == NULL) {
      cup_error("!fn");
      return defTypeCount(0);
    }
    //CVariable* var = state->variable;

    CValue right = defTypeEx(state, nodes);
    if (right.type == NULL || right.type->realtype != CUP_TYPE_COUNT) {
      cup_error("!fn(args)");
      return defTypeCount(0);
    }
    if (left.type->realtype != CUP_TYPE_FUNCTION && left.type->realtype != CUP_TYPE_GENERATOR) {
      char tname[256];
      cup_type_snname(tname, sizeof(tname), left.type);
      cup_errorf("defType call not Function %s", tname);
      return defTypeCount(0);
    }
    return (CValue) {args(state, nodes, left.type, right.type->size), &cup_count.size};
  }

  default:
    cup_errorf("defType UNKNOWN node make for '%s'", token_names[n.token]);
    break;
  }
  return defTypeCount(0);
}

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
        CValue value = defTypeEx(state, pending.data);
        state->variable = NULL;
        if (value.type == NULL) {
          vector_pushT(next.vec, begin);
          continue;
        }
        if (progress != 2) progress = 1;
        if (value.type->realtype == CUP_TYPE_COUNT) {
          if (value.type->size == 0)
            progress = 2;
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

/*

// defTypeErr() NULL
static int sdds(CValue left, CValue right, size_t i) {
  if (left.type->elements[i + 1] == &cup_type_void) {
    left.type->elements[i + 1] = right.type;
    return 0;
  }
  int t = cahngeType(right.type, left.type->elements[i + 1]);
  if (t == 0)
    return 0;
  char tname[256];
  char tname2[256];
  cup_type_snname(tname, sizeof(tname), left.type->elements[i + 1]);
  cup_type_snname(tname2, sizeof(tname2), right.type);
  cup_errorf("defType(%i) arg %ld type mismatch: get %s expected %s", t, i, tname2, tname);
  return -1;
}


static CValue dsd(CUPState* state, NRange* nodes, CVariable* var, CValue left, CValue right) {
    // if (t < 0)
    //   left.type->elements[i + 1] = arg_type.type;
    // else
    //   arg_type.type = left.type->elements[i + 1];

  size_t i = 0;
  assert(left.type);
  assert(right.type);
  const CUPType* begin = left.type->elements[0];
  const CUPType* end = left.type->elements[1 + i];
  if (right.type->realtype != CUP_TYPE_COUNT) {
    if (left.type->realtype == CUP_TYPE_GENERATOR) {
      begin = left.type->gen(state, var, right, i, 1);
      if (begin)
        return (CValue){begin, NULL};
      return defTypeErr();
    }
    if (sdds(left, right, i++))
      return defTypeErr();
    goto endG;
  }
  if (right.type->size == 0)
    return defTypeErr();
  size_t max = right.type->size;
  for (; i < max; i++) {
    CValue arg_type = defTypeEx(state, nodes);
    if (left.type->realtype == CUP_TYPE_GENERATOR) {
      begin = left.type->gen(state, var, arg_type, i, max);
      if (begin == NULL)
        return defTypeErr();
    } else if (sdds(left, arg_type, i))
      return defTypeErr();
  }
  end = (left.type->realtype != CUP_TYPE_GENERATOR) ? left.type->elements[1 + i]:  NULL;
endG:
  if (end) {
    cup_error("defType call args count");
    return defTypeErr();
  }
  return (CValue){begin, NULL};
}

CValue defType(CUPState *state, NRange nodes) {
  for (size_t count = 0; count < 10; count++) {
    size_t c = 0;
    state->value_ = (CValue){};
    defTypeArgs = 0;
    for (size_t i = 1; i > 0; i--) {
      CValue v = defTypeEx(state, &nodes);
      if (v.type == NULL) {
        c = 1;
        continue;
      }
      if (v.type->realtype != CUP_TYPE_COUNT)
        continue;
      if (v.type->size == 0) {
        cup_error("defType END");
        exit(23);
      }
      i += v.type->size;
    }
    if (c == 0)
      return state->value_;
  }
  cup_error("defType MAX");
  exit(24);
 
  * Revisit passes: re-resolve every previously-deferred sub-call until
   * either the pending list is empty or a full pass resolves nothing
   * (fixpoint with no progress => genuine, reportable error). 
  for (int pass = 0; ctx.pending.size > 0 && pass < CUP_DEFTYPE_MAX_PASSES; pass++) {
    CVector carry = {0};
    ctx.progressed = 0;
    ctx.revisiting = 1;
 
    size_t count = ctx.pending.size / sizeof(DTPending);
    DTPending *items = (DTPending *)ctx.pending.data;
    for (size_t i = 0; i < count; i++) {
      size_t rpos = items[i].pos;
      const CUPType *rvalue = NULL;
      size_t rv = defTypeEx(state, items[i].nodes, &rpos, &rvalue);
      if (rv == 0) {
         Still unresolved -- carry it into the next pass as-is. Any
         * new deferrals it triggered this round already landed in
         * ctx.pending past `count` (appended live); they get folded
         * into `carry` below alongside this one so nothing is lost. 
        vector_pushT(carry, items[i]);
      } else {
        if (items[i].value) *items[i].value = rvalue;
        ctx.progressed = 1;
      }
    }
 
     Anything freshly deferred *during this pass* (by the retries
     * above, beyond the original `count` items) also needs to survive
     * into the next pass. 
    if (ctx.pending.size / sizeof(DTPending) > count) {
      DTPending *fresh = items + count;
      size_t fresh_n = ctx.pending.size / sizeof(DTPending) - count;
      for (size_t i = 0; i < fresh_n; i++)
        vector_pushT(carry, fresh[i]);
    }
 
    if (!ctx.progressed) {
       No progress this pass: re-run once more in "loud" mode so the
       * genuinely unresolved sites get reported, then stop. 
      ctx.revisiting = 2;
      for (size_t i = 0; i < carry.size / sizeof(DTPending); i++) {
        DTPending *it = ((DTPending *)carry.data) + i;
        size_t rpos = it->pos;
        const CUPType *rvalue = NULL;
        size_t rv = defTypeEx(state, it->nodes, &rpos, &rvalue);
        if (rv == 0) {
          cup_errorf("defType: could not resolve type after %d passes "
                     "(unresolved forward reference or undefined symbol)",
                     pass + 1);
        } else if (it->value) {
          *it->value = rvalue;
        }
      }
      cup_free(ctx.pending.data);
      cup_free(carry.data);
      ctx.pending = (CVector){0};
      break;
    }
 
    cup_free(ctx.pending.data);
    ctx.pending = carry;
  }
 
  if (ctx.pending.data) cup_free(ctx.pending.data);
  g_dtctx = prev_ctx;
 
  if (*value) {
    char tname[256];
    cup_type_snname(tname, sizeof(tname), *value);
    printf("defType result type = %s\n", tname);
  }
  return r;
  
}



size_t _defType(CUPState *state, const Node *nodes, const CUPType** value) {
  size_t pos = 0;
  size_t r = defTypeEx(state, nodes, &pos, value);
  char tname[256];
  cup_type_snname(tname, sizeof(tname), *value);
  printf("defType result type = %s\n", tname);
  return r;
}
*/
