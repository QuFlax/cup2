#include "../include/cup.h"

#ifndef CUP_DEFTYPE_MAX_PASSES
#define CUP_DEFTYPE_MAX_PASSES 64
#endif

typedef struct defCTX {
  //CUPState* state;
  NRange* nodes;
  //CVector* new;
  //size_t extra;
} defCTX; //websdr

typedef enum CUPValueType : uint8_t {
  CV_ERROR,
  CV_ANY,
  CV_VALUE,
  CV_ADDRESS,
  CV_VARIABLE,

  CV_LIST,
  //CV_BLOCK
} CUPValueType;

typedef struct CUPValue {
  CUPValueType vtype;
  union as {
    //size_t *vvalue;
    struct value {
      const CUPType* typee;
      size_t* value;
    } value;
    struct variable {
      CVariable *value;
      const char* name;
    } variable;
    struct list {
      const void** types;
      size_t** values;
    } list;
  } as;
} CUPValue;

typedef CUPValue (*CUPValueFor)(size_t** values, const CUPType** types);

struct CUPGenContext {
    CUPState* state;
    CUPValue value;
};

static CUPValue toAddress(CUPValue var) {
  return (CUPValue) {CV_ADDRESS, .as.value = {var.as.variable.value->typee, &var.as.variable.value->value}};
}

static CUPValue inferTypes(CUPState *state, CVector* new, NRange* range, CUPValue* rtype, CUPValueFor fn);

static CUPValue eq(CUPState *state, CVector* new, NRange* range) {
  CUPValue right = inferTypes(state, new, range, NULL, NULL);
  CUPValue left = inferTypes(state, new, range, NULL, NULL);
  if (left.vtype == CV_LIST) {
    cup_free(left.as.list.values);
    goto l1;
  }
  if (left.vtype == CV_VALUE) {
l1:
    if (right.vtype == CV_LIST)
      cup_free(right.as.list.values);
    cup_error("left.vtype != lvalue");
    return (CUPValue){CV_ERROR, {}};
  }
  if (left.vtype == CV_VARIABLE) {
    if (right.vtype == CV_LIST) {
      cup_free(right.as.list.values);
      cup_error("right.vtype == CV_LIST");
      return (CUPValue){CV_ERROR, {}};
    }
    CVariable* var = left.as.variable.value;
    if (var->typee) {
      left = toAddress(left);
      goto l3;
    }
    char tname[256] = {};
    char tname2[256] = {};
    if (right.vtype == CV_VARIABLE) {
      right = toAddress(right);
      goto l4;
    }
    if (right.vtype == CV_ADDRESS) {
l4:
      cup_type_snname(tname, sizeof(tname), right.as.value.typee);
      cup_type_snname(tname2, sizeof(tname2), left.as.value.typee);
      cup_dbgf("eq address %s : %ld --- %s %ld", tname2, left.as.value.value,
        tname, right.as.value.value);
      var->typee = right.as.value.typee;
      if (var->typee->cuptype == CUP_TYPE_ARRAY) {
        var->value = (size_t)right.as.value.value;
      } else if (right.as.value.value) {
        var->value = *right.as.value.value;
      } else {
        assert(0);
      }
      //vector_push(new, start, (char*)range->it - start);
      return right;
    }
    if (right.vtype == CV_VALUE) { 
      if (right.as.value.typee == &cup_type_void) {
        cup_error("right.as.value.type == &cup_type_void.cuptype");
        return (CUPValue){CV_ERROR, {}};
      }
      cup_type_snname(tname, sizeof(tname), right.as.value.typee);
      cup_type_snname(tname2, sizeof(tname2), left.as.value.typee);
      cup_dbgf("eq value %s : %ld --- %s %ld", tname2, left.as.value.value,
        tname, right.as.value.value);
      var->typee = right.as.value.typee;
      if (var->typee->cuptype == CUP_TYPE_ARRAY) {
        var->value = (size_t)right.as.value.value;
      } else if (right.as.value.value) {
        var->value = *right.as.value.value;
      } else {
        assert(0);
      }
      //vector_push(new, start, (char*)range->it - start);
      return right;
    }
    cup_error("T_EQ return right");
    return right;
  }
  if (left.vtype == CV_ADDRESS) {
l3:
      if (right.vtype == CV_LIST) {
        cup_free(right.as.list.values);
        cup_error("right.vtype == CV_LIST");
        return (CUPValue){CV_ERROR, {}};
      }
      if (right.vtype == CV_VARIABLE) {
        right = toAddress(right);
        goto l2;
      }
      if (right.vtype == CV_VALUE || right.vtype == CV_ADDRESS) {
l2:
        if (left.as.value.typee != right.as.value.typee) {
          cup_error("left.as.value.type != right.as.value.type");
          return (CUPValue){CV_ERROR, {}};
        }
        char tname[256] = {};
        char tname2[256] = {};
        cup_type_snname(tname, sizeof(tname), right.as.value.typee);
        cup_type_snname(tname2, sizeof(tname2), left.as.value.typee);
        cup_dbgf("eq left address %s : %ld --- %s %ld", tname2, left.as.value.value,
          tname, right.as.value.value);
        // *add = value
        return right;
      }
      cup_error("T_EQ return right");
      return right;
    }
    cup_error("T_EQ return left");
    return left;
}

static CUPValue dot(CUPState *state, CVector* new, NRange* range, Node dotnode) {
  CVector leftvec = {};
  CUPValue left = inferTypes(state, &leftvec, range, NULL, NULL);
  if (left.vtype != CV_VARIABLE) {
    cup_error("left.vtype != CV_VARIABLE");
    return (CUPValue){CV_ERROR, {}};
  }
  CVector rightvec = {};
  CUPValue right = inferTypes(state, &rightvec, range, NULL, NULL);
  CVariable* var = left.as.variable.value;
  if (right.vtype != CV_VARIABLE) {
    cup_error("right.vtype != CV_VARIABLE");
    return (CUPValue){CV_ERROR, {}};
  }
  if (var->typee->cuptype == CUP_TYPE_META) {
    cup_dbg("CV_VARIABLE meta");
    CUPGenContext gc = {state, {}};
    CUP_META m = {};
    m.member.name = right.as.variable.name;
    CUP_GEN_RESULT r = var->typee->metatype.gen(&gc, CUP_MOP_MEMBER, (void*)var->value, m);
    if (r == CUP_GEN_ERROR) {
      cup_free(right.as.list.values);
      cup_errorf("%s: r == CUP_GEN_ERROR", __FILE__);
      return (CUPValue){CV_ERROR, {}};
    }
    if (r == CUP_GEN_CONTINUE) {
      assert(0);
    }
    

  } else {

  }
  assert(0);
  return (CUPValue){CV_ERROR, {}}; 
}

static CUPValue call(CUPState *state, CVector* new, NRange* range, Node callnode) {
  CVector leftvec = {};
  CUPValue left = inferTypes(state, &leftvec, range, NULL, NULL);
  CVector rightvec = {};
  CUPValue right = inferTypes(state, &rightvec, range, NULL, NULL);
  if (left.vtype == CV_LIST) {
    cup_free(left.as.list.values);
    goto l1;
  }
  if (left.vtype == CV_VALUE) {
l1:
    if (right.vtype == CV_LIST)
      cup_free(right.as.list.values);
    cup_error("left.vtype != lvalue");
    return (CUPValue){CV_ERROR, {}};
  }
  if (left.vtype == CV_VARIABLE) {
    CVariable* var = left.as.variable.value;
    assert(var->typee);
    if (right.vtype != CV_LIST) {
      cup_error("right.vtype != CV_LIST");
      return (CUPValue){CV_ERROR, {}};
    }
    size_t count = ((size_t*)right.as.list.types)[-1];
    if (var->typee->cuptype == CUP_TYPE_FUNCTION) {
      // TODO optimaize to value if posiable
      assert(0);
    }
    if (var->typee->cuptype == CUP_TYPE_META) {
      CUPGenContext gc = {state, {}};
      CUP_META m = {};
      m.call.argtypes = (const CUPType**)right.as.list.types; //(const CUPType **)right.as.list.types,
      m.call.argvalues = right.as.list.values;
      m.call.max = count;
      CUP_GEN_RESULT r = var->typee->metatype.gen(&gc, CUP_MOP_CALL, (void*)var->value, m);
      if (r == CUP_GEN_ERROR) {
        cup_free(right.as.list.values);
        cup_errorf("%s: r == CUP_GEN_ERROR", __FILE__);
        return (CUPValue){CV_ERROR, {}};
      }
      if (r == CUP_GEN_CONTINUE) {
        assert(0);
      }
      CUPValue value = gc.value;
      Node2 nod = {};
      switch (value.vtype) {
      case CV_VARIABLE:
        vector_push(new, &callnode, sizeof(Node)); // T_CALL
        callnode.token = T_NUMBER;
        vector_push(new, &callnode, sizeof(Node)); // T_NUMBER
        callnode.value = (size_t)value.as.value.value;
        vector_push(new, &callnode, sizeof(Node)); // value
        push_vector(new, rightvec);
        break;
      case CV_VALUE:
        callnode.token = T_NUMBER;
        vector_push(new, &callnode, sizeof(Node)); // T_NUMBER
        callnode.value = (size_t)value.as.value.value;
        vector_push(new, &callnode, sizeof(Node)); // value
        break;
      default: {
        assert(0);
        CUPValue value = (CUPValue) {CV_VALUE, .as.value.typee = (const CUPType *)(right.as.list.types[count])};
        cup_free(right.as.list.values);
        //vector_push(new, start, (char*)range->it - start);
        return value;
      }
      }
      value.as.value.value = (size_t*)((char*)new->data + new->size - sizeof(Node));
      cup_free(right.as.list.values);
      return value;
    }
    cup_error("var->type->type is not function");
    return (CUPValue){CV_ERROR, {}};
  }
  assert(0);
}

static CUPValue osb(CUPState *state, CVector* new, NRange* range) {
  CUPValue left = inferTypes(state, new, range, NULL, NULL);
  CUPValue right = inferTypes(state, new, range, NULL, NULL);
  if (left.vtype == CV_VALUE) {
    if (right.vtype == CV_LIST)
      cup_free(right.as.list.values);
    cup_error("left.vtype == CV_VALUE");
    return (CUPValue){CV_ERROR, {}};
  }
  if (left.vtype == CV_VARIABLE) {
    CVariable* var = left.as.variable.value;
    if (right.vtype == CV_VALUE) {
      if (right.as.value.typee == &cup_type_void) {
        cup_error("right.as.value.type == &cup_type_void.cuptype");
        return (CUPValue){CV_ERROR, {}};
      }
      if (var->typee == NULL)
        assert(0);
      if (var->typee->cuptype != CUP_TYPE_ARRAY)
        assert(0);
      if (var->typee->arrtype.element == NULL)
        assert(0);
      char tname[256] = {};
      char tname2[256] = {};
      cup_type_snname(tname, sizeof(tname), right.as.value.typee);
      cup_type_snname(tname2, sizeof(tname2), var->typee->arrtype.element );
      cup_dbgf("osb %s : %ld / %s", tname, *right.as.value.value, tname2);
      if (var->typee->arrtype.element != right.as.value.typee) {
        assert(0);
      }
      // right.as.rtype == var-type
      if (right.as.value.typee == &cup_type_int) {
        return (CUPValue){ CV_ADDRESS, .as.value = {right.as.value.typee,
          (size_t*)(var->value + *right.as.value.value) }};
      } else {
        assert(0);
      }
      return (CUPValue){ CV_ADDRESS, .as.value = {.typee = var->typee->arrtype.element, .value = &var->value }};
    }
    assert(0);
  }
  assert(0);
}

static CUPValue at(CUPState *state, CVector* new, NRange* range) {
  CUPValue left = inferTypes(state, new, range, NULL, NULL);
  state->scope++;
  CUPValue args = inferTypes(state, new, range, NULL, NULL);
  if (args.vtype == CV_LIST) {
    cup_free(args.as.list.values);
  }
  CUPValue ret = {};
  CUPValue body = inferTypes(state, new, range, &ret, NULL);
  (void)body;
  return left;
}

static CUPValue fnblock(size_t** values, const CUPType** types) {
  (void)types;
  cup_free(values);
  return (CUPValue){CV_VALUE, .as.value.typee = &cup_type_void};
}

static CUPValue fncomma(size_t** values, const CUPType** types) {
  return (CUPValue){CV_LIST, .as.list = {(const void**)types, values}};
}

static CUPValue fnarray(size_t** values, const CUPType** types) {
  const CUPType* type = NULL;
  size_t count = ((size_t*)types)[-1];
  for (size_t i = 0; i < count; i++) {
    if (types[i] == NULL) continue;
    if (type && type != types[i]) {
      cup_free(values);
      return (CUPValue){CV_ERROR, {}};
    }
    type = types[i];
  }
  return (CUPValue){CV_LIST, .as.list = {(const void**)types, values}};
}

static CUPValue defFor(CUPState *state, CVector* new, NRange* range, CUPValue* rtype, size_t count, CUPValueFor fn) {
  size_t split = sizeof(void*) * (count + 1);
  size_t** values = (size_t**)cup_malloc(split * 2);
  memset(values, 0, split * 2);
  const CUPType** types = (const CUPType**)((char*)values + split);
  for (size_t j = 0; j < count; j++) {
    CUPValue item = inferTypes(state, new, range, rtype, NULL);
    if (item.vtype == CV_ERROR) {
      cup_free(values);
      return item;
    }
    if (item.vtype == CV_VALUE) {
      if (item.as.value.typee == NULL) {
        assert(0);
      }
      if (item.as.value.typee != &cup_type_void) {
        types[j] = item.as.value.typee;
        values[j] = item.as.value.value;
      }
      continue;
    }
    if (item.vtype == CV_VARIABLE) {
      types[j] = item.as.variable.value->typee;
      values[j] = &item.as.variable.value->value;
      continue;
    }
    if (item.vtype == CV_LIST) {
      assert(0);
    }
    if (item.vtype == CV_ADDRESS) {
      if (item.as.value.typee == NULL) {
        assert(0);
      }
      types[j] = item.as.value.typee;
      values[j] = item.as.value.value;
      continue;
    }
    assert(0);
  }
  values[count] = (size_t*)count;
  types[count] = NULL;
  return fn(values, types);
}

static Node* getNode(NRange* range) {
  if (range->it >= range->end) {
    cup_error("Incorrect Nodes");
    exit(1);
  }
  return range->it++;
}

static CUPValue inferTypes(CUPState *state, CVector* new, NRange* range, CUPValue* rtype, CUPValueFor fn) {
  char* start = (char*)range->it;
  Node n = *getNode(range);
  switch (n.token) {
  case N_BLOCK: {
    state->scope++;
    size_t count = getNode(range)->value;
    vector_push(new, start, (char*)range->it - start);
    return defFor(state, new, range, rtype, count, fnblock);
  }
  case T_COMMA: {
    size_t count = getNode(range)->value;
    vector_push(new, start, (char*)range->it - start);
    return defFor(state, new, range, rtype, count, fn ? fn : fncomma);
  }
  case N_ARRAY: {
    const uint8_t* off = getData(state, getNode(range)->value);
    vector_push(new, start, (char*)range->it - start);
    CUPValue right = inferTypes(state, new, range, NULL, fnarray);
    if (right.vtype != CV_LIST) {
      assert(0);
    }
    size_t count = ((size_t*)right.as.list.types)[-1];
    //const CUPType t = cup_type_array(right.as.list.types[0], count);
    assert(count < UINT16_MAX);
    const CUPType* t = cup_type(state, CUP_TYPE_ARRAY, (uint16_t)count, right.as.list.types);
    CUPValue v = (CUPValue){CV_VALUE, .as.value = {.typee = t, .value = (size_t*)off } };
    cup_free(right.as.list.values);
    return v;
    }
  case T_NUMBER: {
    size_t* value = &getNode(range)->value;
    vector_push(new, start, (char*)range->it - start);
    return (CUPValue){CV_VALUE, .as.value = {.typee = &cup_type_int, .value = value}};
  }
  case T_STRING: case T_MSTRING: {
    const uint8_t* str = getData(state, getNode(range)->value);
    vector_push(new, start, (char*)range->it - start);
    size_t count = strlen((char*)str) + 1;
    assert(count < UINT16_MAX);
    const CUPType* t = cup_type(state, CUP_TYPE_ARRAY, (uint16_t)count, (const void *){&cup_type_uint8});
    return (CUPValue){CV_VALUE, .as.value = {.typee = t, .value = (size_t*)str}};
  }
  case T_THIS: {
    CVariable *var = varLookup(state, "$", state->scope);
    if (var == NULL) {
      cup_error("var == NULL");
      return (CUPValue){CV_ERROR, {}};
    }
    vector_push(new, start, (char*)range->it - start);
    return (CUPValue){CV_VARIABLE, .as.variable = {var}};
  }
  case N_VARIABLE: {
    const char* name = getNode(range)->svalue;
    CVariable *var = varLookup(state, name, state->scope);
    if (var == NULL) {
      cup_error("var == NULL");
      vector_push(new, start, (char*)range->it - start);
      return (CUPValue){CV_VARIABLE, .as.variable = {NULL, name}};
    }
    vector_push(new, start, (char*)range->it - start);
    return (CUPValue){CV_VARIABLE, .as.variable = {var, name}};
  }
  case T_DOT: {
    return dot(state, new, range, *(Node*)start);
  }
  case T_EQ: {
    vector_push(new, start, (char*)range->it - start);
    return eq(state, new, range);
  }
  case T_CALL: {
    //vector_push(new, start, (char*)range->it - start);
    return call(state, new, range, *(Node*)start);
  }
  case T_OSB: {
    vector_push(new, start, (char*)range->it - start);
    return osb(state, new, range);
  }
  case T_AT: {
    vector_push(new, start, (char*)range->it - start);
    return at(state, new, range);
  }
  case T_RETURN: {
    vector_push(new, start, (char*)range->it - start);
    CUPValue left = inferTypes(state, new, range, NULL, NULL);
    if (left.vtype != CV_VALUE) {
      assert(0);
    }
    if (left.as.value.typee == &cup_type_void) {
      assert(0);
    }
    if (rtype == NULL) {
      assert(0);
    }
    *rtype = left;
    return (CUPValue){CV_VALUE, .as.value.typee = &cup_type_void};
  }
  default:
    cup_errorf("defType UNKNOWN node make for '%s'", token_names[n.token]); 
    break;
  }
  return (CUPValue){CV_ERROR, {}};
}

int resolve(CUPState *state, Nodes* nodes) {
  int err = 0;
  NRange range = (NRange){nodes->data, (Node *)((char *)nodes->data + nodes->size)};
  CVector new = {};
  vector_new(&new, nodes->size);
  CUPValue v = inferTypes(state, &new, &range, NULL, NULL);
  switch (v.vtype) {
  case CV_ERROR:
    err = 1;
    // TODO clear all
    break;
  case CV_LIST:
    cup_free(v.as.list.values);
    break;
  case CV_ADDRESS: break;
  case CV_VALUE: break;
  case CV_VARIABLE: break;
  
  default: assert(0);
  }
  if (nodes->data) cup_free(nodes->data);
  nodes->vec = new;
  //puts("OOOOOOOOOOOOOOOO");
  //newDecls = []
  //for decl in ast.decls:
  //      (decl', _) = inferTypes(decl, env, regenerator)
  //      newDecls.push(decl')
  return err;
}

void cup_gen_address(CUPGenContext *ctx, void *lvalue, const CUPType* type) {
  ctx->value.vtype = CV_ADDRESS;
  ctx->value.as.value.value = (size_t*)lvalue;
  ctx->value.as.value.typee = type;
  //ctx->node[0].token = T_NUMBER;
  //ctx->node[1].value = (size_t)lvalue;
  //ctx->node[2].vtype = cup_type(ctx->state, type);
  //ctx->node[2].vtype = type;
}

void cup_gen_value(CUPGenContext *ctx, void *value, const CUPType* type) {
  ctx->value.vtype = CV_VALUE;
  ctx->value.as.value.value = (size_t*)value;
  ctx->value.as.value.typee = type;
  //ctx->node[0].token = T_NUMBER;
  //ctx->node[1].value = (size_t)value;
  //ctx->node[2].vtype = cup_type(ctx->state, type);
  //ctx->node[2].vtype = type;
}

void cup_gen_call(CUPGenContext *ctx, void *value, size_t max, const void** types) {
  ctx->value.vtype = CV_VARIABLE;
  ctx->value.as.value.value = (size_t*)value;
  ctx->value.as.value.typee = cup_type(ctx->state, CUP_TYPE_FUNCTION, max, types);
  //ctx->node[0].token = T_CALL;
  //ctx->node[1].value = (size_t)value;
  //ctx->node[2].vtype = cup_type(ctx->state, CUP_TYPE_FUNCTION, max, types);
  //ctx->node[2].vtype = rtype;
}

//void* cup_gen_get_value(CUPGenContext *ctx) {
//
//}