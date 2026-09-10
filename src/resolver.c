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

typedef enum CUPValueType {
  CV_ERROR,
  //CV_ANY,
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
      const CUPType *type;
      size_t* value;
    } value;
    struct variable {
      //const CUPType *cuptype;
      CVariable *value;
    } variable;
    struct list {
      const CUPType** types;
      size_t** values;
    } list;
  } as;
} CUPValue;

typedef CUPValue (*CUPValueFor)(size_t** values, const CUPType** types);

static CUPValue toAddress(CUPValue var) {
  return (CUPValue) {CV_ADDRESS, .as.value = {var.as.variable.value->type, &var.as.variable.value->value}};
}

static CUPValue inferTypes(CUPState *state, CVector* new, NRange* range, void* env, CUPValueFor fn);

static CUPValue eq(CUPState *state, CVector* new, NRange* range, Node* start) {
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
      if (var->type) {
        left = toAddress(left);
        goto l3;
      }
      if (right.vtype == CV_VARIABLE) {
        right = toAddress(right);
        goto l4;
      }
      if (right.vtype == CV_VALUE || right.vtype == CV_ADDRESS) {
l4:    
        if (right.as.value.type == &cup_type_void) {
          cup_error("right.as.value.type == &cup_type_void");
          return (CUPValue){CV_ERROR, {}};
        }
        var->type = right.as.value.type;
        if (right.as.value.value) {
          var->value = *right.as.value.value;
        } else {
          assert(0);
        }
        vector_push(new, start, range->it - start);
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
        if (left.as.value.type != right.as.value.type) {
          cup_error("left.as.value.type != right.as.value.type");
          return (CUPValue){CV_ERROR, {}};
        } 
        return right;
      }
      cup_error("T_EQ return right");
      return right;
    }
    cup_error("T_EQ return left");
    return left;
}

static CUPValue call(CUPState *state, CVector* new, NRange* range, Node* start) {
  CUPValue left = inferTypes(state, new, range, NULL, NULL);
  CUPValue right = inferTypes(state, new, range, NULL, NULL);
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
    assert(var->type);
    if (right.vtype != CV_LIST) {
      cup_error("right.vtype != CV_LIST");
      return (CUPValue){CV_ERROR, {}};
    }
    size_t count = ((size_t*)right.as.list.types)[-1];
    if (var->type->type == CUP_TYPE_FUNCTION) {
      // TODO optimaize to value if posiable
      assert(0);
    }
    if (var->type->type == CUP_TYPE_GENERATOR) {
      CUPGenContext gc = {state};
      CUP_GEN_RESULT r = ((CUPTypeExt*)var->type)->gen(&gc,
        right.as.list.types, right.as.list.values, count);
      if (r == CUP_GEN_CONTINUE) {
        assert(0);
      }
      if (r == CUP_GEN_ERROR || right.as.list.types[count] == NULL) {
        cup_free(right.as.list.values);
        cup_error("r == CUP_GEN_ERROR");
        return (CUPValue){CV_ERROR, {}};
      }
      if (gc.node[0].value == 0) {
        cup_free(right.as.list.values);
        cup_error("gc.node[0].value");
        return (CUPValue){CV_ERROR, {}};
      }
      CUPValue value = (CUPValue) {CV_VALUE, .as.value = {right.as.list.types[count], NULL}};
      cup_free(right.as.list.values);
      vector_push(new, start, range->it - start);
      return value;
    }
    cup_error("var->type->type is not function");
    return (CUPValue){CV_ERROR, {}};
  }
  assert(0);
}

static CUPValue fnblock(size_t** values, const CUPType** types) {
  (void)values; (void)types;
  return (CUPValue){CV_VALUE, .as.value = { &cup_type_void, NULL }};
}

static CUPValue fncomma(size_t** values, const CUPType** types) {
  return (CUPValue){CV_LIST, .as.list = {types, values}};
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
  return (CUPValue){CV_LIST, .as.list = {types, values}};
}

static CUPValue defFor(CUPState *state, CVector* new, NRange* range, size_t count, CUPValueFor fn) {
  size_t split = sizeof(void*) * (count + 1);
  size_t** values = (size_t**)cup_malloc(split * 2);
  memset(values, 0, split * 2);
  const CUPType** types = (const CUPType**)((char*)values + split);
  for (size_t j = 0; j < count; j++) {
    CUPValue item = inferTypes(state, new, range, NULL, NULL);
    if (item.vtype == CV_ERROR) {
      cup_free(values);
      return item;
    }
    if (item.vtype == CV_VALUE) {
      if (item.as.value.type == NULL) {
        assert(0);
      }
      if (item.as.value.type != &cup_type_void) {
        types[j] = item.as.value.type;
        values[j] = item.as.value.value;
      }
      continue;
    }
    if (item.vtype == CV_VARIABLE) {
      types[j] = item.as.variable.value->type;
      values[j] = &item.as.variable.value->value;
      continue;
    }
    if (item.vtype == CV_LIST) {
      assert(0);
    }
    if (item.vtype == CV_ADDRESS) {
      if (item.as.value.type == NULL) {
        assert(0);
      }
      types[j] = item.as.value.type;
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

static CUPValue inferTypes(CUPState *state, CVector* new, NRange* range, void* env, CUPValueFor fn) {
  Node* start = range->it;
  Node n = *getNode(range);
  switch (n.token) {
  case N_BLOCK: {
    state->scope++;
    CUPValue v = defFor(state, new, range, getNode(range)->value, fnblock);
    vector_push(new, start, range->it - start);
    return v;
  }
  case T_COMMA: {
    CUPValue v = defFor(state, new, range, getNode(range)->value, fn ? fn : fncomma);
    vector_push(new, start, range->it - start);
    return v;
  }
  case N_ARRAY: {
    const uint8_t* off = getData(state, getNode(range)->value);
    CUPValue right = inferTypes(state, new, range, NULL, fnarray);
    if (right.vtype != CV_LIST) {
      assert(0);
    }
    size_t count = ((size_t*)right.as.list.types)[-1];
    const CUPType t = cup_type_array(right.as.list.types[0], count);
    CUPValue v = (CUPValue){CV_VALUE, .as.value = { cup_type_put(state, t), (size_t*)off } };
    cup_free(right.as.list.values);
    vector_push(new, start, range->it - start);
    return v;
    }
  case T_NUMBER: {
    size_t* value = &getNode(range)->value;
    vector_push(new, start, range->it - start);
    return (CUPValue){CV_VALUE, .as.value = {&cup_type_int, value}};
  }
  case T_STRING: case T_MSTRING: {
    const uint8_t* str = getData(state, getNode(range)->value);
    const CUPType t = cup_type_array(&cup_type_uint8, strlen((char*)str) + 1);
    vector_push(new, start, range->it - start);
    return (CUPValue){CV_VALUE, .as.value = {cup_type_put(state, t), (size_t*)str}};
  }
  case N_VARIABLE: {
    CVariable *var = getVars(state, getNode(range)->value, state->scope);
    if (var == NULL) {
      cup_error("var == NULL");
      return (CUPValue){CV_ERROR, {}};
    }
    vector_push(new, start, range->it - start);
    return (CUPValue){CV_VARIABLE, .as.variable = {var}};
  }
  case T_EQ: {
    return eq(state, new, range, start);
  }
  case T_CALL: {
    return call(state, new, range, start);
  }
  default:
    break;
  }
  return (CUPValue){CV_ERROR, {}};
}

int resolve(CUPState *state, Nodes* nodes) {
  NRange range = (NRange){nodes->data, (Node *)((char *)nodes->data + nodes->size)};
  CVector new = {};
  CUPValue v = inferTypes(state, &new, &range, NULL, NULL);
  //newDecls = []
  //for decl in ast.decls:
  //      (decl', _) = inferTypes(decl, env, regenerator)
  //      newDecls.push(decl')
  return 0;
}

void cup_gen_value(CUPGenContext *ctx, const CUPType *type, void *value) {
  ctx->node[0].token = N_VARIABLE;
  ctx->node[1].value = (size_t)value;
  ctx->node[2].vtype = type;
}
void cup_gen_call(CUPGenContext *ctx, const CUPType *rtype, void *value) {
  ctx->node[0].token = T_CALL;
  ctx->node[1].value = (size_t)value;
  ctx->node[2].vtype = rtype;
}
//void* cup_gen_get_value(CUPGenContext *ctx) {
//
//}