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
  CV_FUNCTION = CV_VARIABLE, /* callee produced by cup_gen_call: as.value = {function type, address} */

  CV_LIST,
  //CV_BLOCK
} CUPValueType;

typedef struct CUPValue {
  CUPValueType vtype;
  union as {
    struct {
      //const char* msg;
    } err;
    struct {
      const CUPType* type;
    } any;
    struct {
      const CUPType* type;
      size_t value;
    } value;
    struct {
      const CUPType* type;
      size_t* pvalue;
    } address;
    struct {
      CVariable *value;
      const char* name;
    } variable;
    struct {
      const CUPType** types;
      size_t* values;
    } list;
  } as;
} CUPValue;

typedef CUPValue (*CUPValueFor)(size_t* values, const CUPType** types);

struct CUPGenContext {
    CUPState* state;
    CUPValue value;
};

//static CUPValue toAddress(CUPValue var) {
//  return (CUPValue) {CV_ADDRESS, .as.value = {var.as.variable.value->typee, &var.as.variable.value->value}};
//}

static CUPValue inferTypes(CUPState *state, CVector* new, NRange* range, CUPValue* rtype, CUPValueFor fn);

static Node* getNode(NRange* range) {
  if (range->it >= range->end) {
    cup_error("Incorrect Nodes");
    exit(1);
  }
  return range->it++;
}


static CUPValue eq(CUPState *state, CVector* new, NRange* range) {
  CUPValue right = inferTypes(state, new, range, NULL, NULL);
  CUPValue left = inferTypes(state, new, range, NULL, NULL);
  switch (right.vtype) {
  case CV_ANY: {
    const CUPType* type = right.as.any.type;
    if (left.vtype == CV_VARIABLE && left.as.variable.value) {
      CVariable* target = left.as.variable.value;
      if (type && target->typee && target->typee != type) {
        cup_error("assignment type mismatch");
        return (CUPValue){CV_ERROR, {}};
      }
      if (type) target->typee = type;
      return right;
    }
    if (left.vtype == CV_ADDRESS) {
      if (type && left.as.address.type != type) {
        cup_error("assignment type mismatch");
        return (CUPValue){CV_ERROR, {}};
      }
      return right;
    }
    // TODO: rest
    assert(0);
  }
  case CV_VALUE: {
    const CUPType* type = right.as.value.type;
    if (left.vtype == CV_VARIABLE && left.as.variable.value) {
      CVariable* target = left.as.variable.value;
      if (type && target->typee && target->typee != type) {
        cup_error("assignment type mismatch");
        return (CUPValue){CV_ERROR, {}};
      }
      if (type) target->typee = type;
      return right;
    }
    if (left.vtype == CV_ADDRESS) {
      if (type && left.as.address.type != type) {
        cup_error("assignment type mismatch");
        return (CUPValue){CV_ERROR, {}};
      }
      return right;
    }
    // TODO: rest
    assert(0);
  }
  case CV_ADDRESS: {
    const CUPType* type = right.as.address.type;
    if (left.vtype == CV_VARIABLE && left.as.variable.value) {
      CVariable* target = left.as.variable.value;
      if (type && target->typee && target->typee != type) {
        cup_error("assignment type mismatch");
        return (CUPValue){CV_ERROR, {}};
      }
      if (type) target->typee = type;
      return right;
    }
    if (left.vtype == CV_ADDRESS) {
      if (type && left.as.address.type != type) {
        cup_error("assignment type mismatch");
        return (CUPValue){CV_ERROR, {}};
      }
      return right;
    }
    // TODO: rest
    assert(0);
  }
  case CV_VARIABLE: {
    if (right.as.variable.value == NULL) {
      // TODO: rest
      assert(0);
    }
    const CUPType* type = right.as.variable.value->typee;
    if (left.vtype == CV_VARIABLE && left.as.variable.value) {
      CVariable* target = left.as.variable.value;
      if (type && target->typee && target->typee != type) {
        cup_error("assignment type mismatch");
        return (CUPValue){CV_ERROR, {}};
      }
      if (type) target->typee = type;
      return right;
    }
    if (left.vtype == CV_ADDRESS) {
      if (type && left.as.address.type != type) {
        cup_error("assignment type mismatch");
        return (CUPValue){CV_ERROR, {}};
      }
      return right;
    }
    // TODO: rest
    assert(0);
  }
  case CV_LIST: {
    cup_error("right.vtype == CV_LIST");
    cup_free(right.as.list.values);
    break;
  }
  
  default: 
    // TODO: rest
    assert(0);
    break;
  }
  if (left.vtype == CV_LIST)
    cup_free(left.as.list.values);
  return (CUPValue){CV_ERROR, {}};
}

static void freeValue(CUPValue* v) {
  if (v->vtype == CV_LIST && v->as.list.values) {
    cup_free(v->as.list.values);
    v->as.list.values = NULL;
  }
}

static void pushNumber(CVector* new, Node tmpl, size_t value) {
  tmpl.token = T_NUMBER;
  vector_push(new, &tmpl, sizeof(Node));
  tmpl.value = value;
  vector_push(new, &tmpl, sizeof(Node));
}

/* Run a meta callback and translate its result into nodes in `new`.
 * On success the returned value is gc.value (CV_VALUE / CV_ADDRESS /
 * CV_FUNCTION); on failure CV_ERROR. */
static CUPValue metaApply(CUPState* state, CVariable* var, CUP_META_OP op, CUP_META meta, CUPGenContext* gc) {
  gc->state = state;
  gc->value = (CUPValue){CV_ERROR, {}};
  CUP_GEN_RESULT r = var->typee->metatype.gen(gc, op, (void*)var->value, meta);
  if (r == CUP_GEN_ERROR) {
    cup_errorf("%s: meta callback failed", __FILE__);
    return (CUPValue){CV_ERROR, {}};
  }
  if (r == CUP_GEN_CONTINUE) {
    cup_errorf("%s: CUP_GEN_CONTINUE is not supported", __FILE__);
    return (CUPValue){CV_ERROR, {}};
  }
  if (gc->value.vtype == CV_ERROR) {
    cup_errorf("%s: meta callback returned DONE without a result", __FILE__);
    return (CUPValue){CV_ERROR, {}};
  }
  return gc->value;
}

static CUPValue dot(CUPState *state, CVector* new, NRange* range, Node dotnode) {
  CVector leftvec = {};
  CUPValue left = inferTypes(state, &leftvec, range, NULL, NULL);
  cup_free(leftvec.data);
  /* member name is a raw identifier, not an expression: do not resolve it */
  Node* namenode = getNode(range);
  if (namenode->token != N_VARIABLE) {
    freeValue(&left);
    cup_error("dot: member name expected");
    return (CUPValue){CV_ERROR, {}};
  }
  Node* valnode = getNode(range);
  const char* name = valnode->svalue;
  if (left.vtype != CV_VARIABLE || left.as.variable.value == NULL) {
    freeValue(&left);
    cup_error("dot: left is not a variable");
    return (CUPValue){CV_ERROR, {}};
  }
  CVariable* var = left.as.variable.value;
  if (var->typee == NULL || var->typee->cuptype != CUP_TYPE_META) {
    cup_errorf("dot: member access '.%s' on a non-meta type", name);
    return (CUPValue){CV_ERROR, {}};
  }
  CUP_META m = {};
  m.member.name = name;
  m.member.len = strlen(name);
  CUPGenContext gc;
  CUPValue v = metaApply(state, var, CUP_MOP_MEMBER, m, &gc);
  switch (v.vtype) {
  case CV_VALUE:
  case CV_ADDRESS:
    pushNumber(new, dotnode, (size_t)v.as.value.value);
    return v;
  default:
    cup_errorf("dot: '.%s' did not produce a value or address", name);
    return (CUPValue){CV_ERROR, {}};
  }
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
      switch (value.vtype) {
      case CV_VARIABLE:
        vector_push(new, &callnode, sizeof(Node)); // T_CALL
        callnode.token = T_NUMBER;
        vector_push(new, &callnode, sizeof(Node)); // T_NUMBER
        callnode.value = (size_t)value.as.value.value;
        vector_push(new, &callnode, sizeof(Node)); // value
        push_vector(new, rightvec);
        break;
      case CV_VALUE: {
        callnode.token = T_NUMBER;
        vector_push(new, &callnode, sizeof(Node)); // T_NUMBER
        callnode.value = (size_t)value.as.value.value;
        vector_push(new, &callnode, sizeof(Node)); // value
        cup_free(right.as.list.values);
        return value;
      }
      default: {
        assert(0);
        CUPValue value = (CUPValue) {CV_ANY, .as.any.type = right.as.list.types[count]};
        cup_free(right.as.list.values);
        //vector_push(new, start, (char*)range->it - start);
        return value;
      }
      }
      value.as.value.value = (size_t)((char*)new->data + new->size - sizeof(Node));
      cup_free(right.as.list.values);
      return value;
    }
    cup_error("var->type->type is not function");
    return (CUPValue){CV_ERROR, {}};
  }
  assert(0);
}

static CUPValue osb(CUPState *state, CVector* new, NRange* range) {
  /* caller already pushed the T_OSB node */
  size_t mark = new->size - sizeof(Node);
  Node osbnode = *(Node*)((char*)new->data + mark);
  CUPValue left = inferTypes(state, new, range, NULL, NULL);
  CUPValue right = inferTypes(state, new, range, NULL, NULL);
  if (left.vtype == CV_VALUE) {
    if (right.vtype == CV_LIST)
      cup_free(right.as.list.values);
    cup_error("left.vtype == CV_VALUE");
    return (CUPValue){CV_ERROR, {}};
  }
  if (left.vtype == CV_VARIABLE && left.as.variable.value &&
      left.as.variable.value->typee &&
      left.as.variable.value->typee->cuptype == CUP_TYPE_META) {
    CVariable* var = left.as.variable.value;
    CUP_META m = {};
    if (right.vtype == CV_VALUE) {
      m.brack.indextype = right.as.value.type;
      m.brack.indexvalue = right.as.value.value;
    }
    CUPGenContext gc;
    CUPValue v = metaApply(state, var, CUP_MOP_BRACK, m, &gc);
    freeValue(&right);
    if (v.vtype != CV_VALUE && v.vtype != CV_ADDRESS) {
      cup_error("osb: meta callback did not produce a value or address");
      return (CUPValue){CV_ERROR, {}};
    }
    /* drop T_OSB + operand nodes already emitted, replace with the result */
    new->size = mark;
    pushNumber(new, osbnode, (size_t)v.as.value.value);
    return v;
  }
  /* array indexing: left is a variable or an address holding an array */
  const CUPType* arrtype = NULL;
  size_t base = 0;
  if (left.vtype == CV_VARIABLE && left.as.variable.value) {
    arrtype = left.as.variable.value->typee;
    base = left.as.variable.value->value;
  } else if (left.vtype == CV_ADDRESS && left.as.address.pvalue) {
    arrtype = left.as.address.type;
    base = *left.as.address.pvalue; /* slot holds the pointer to the array data */
  } else {
    freeValue(&right);
    cup_error("osb: left is not a variable or address");
    return (CUPValue){CV_ERROR, {}};
  }
  if (arrtype == NULL || arrtype->cuptype != CUP_TYPE_ARRAY || arrtype->arrtype.element == NULL) {
    freeValue(&right);
    cup_error("osb: left is not an array");
    return (CUPValue){CV_ERROR, {}};
  }
  if (right.vtype != CV_VALUE) {
    freeValue(&right);
    cup_error("osb: index is not a constant value");
    return (CUPValue){CV_ERROR, {}};
  }
  if (right.as.value.type != &cup_type_int) {
    cup_error("osb: index is not int");
    return (CUPValue){CV_ERROR, {}};
  }
  if (arrtype->arrtype.element == &cup_type_void) {
    cup_error("osb: array of void");
    return (CUPValue){CV_ERROR, {}};
  }
  if (right.as.value.value >= arrtype->size) {
    cup_errorf("osb: index %zu out of bounds (size %u)", right.as.value.value, arrtype->size);
    return (CUPValue){CV_ERROR, {}};
  }
  /* base == 0: array data is not known at compile time, the address is
   * computed at runtime from the nodes already emitted (pvalue = NULL).
   * Elements are size_t-sized slots (see codegen T_OSB). */
  return (CUPValue){CV_ADDRESS, .as.address = {arrtype->arrtype.element,
    base ? (size_t*)base + right.as.value.value : NULL}};
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

static CUPValue fnblock(size_t* values, const CUPType** types) {
  (void)types;
  cup_free(values);
  return (CUPValue){CV_VALUE, .as.value.type = &cup_type_void};
}

static CUPValue fncomma(size_t* values, const CUPType** types) {
  return (CUPValue){CV_LIST, .as.list = {types, values}};
}

static CUPValue fnarray(size_t* values, const CUPType** types) {
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

static CUPValue defFor(CUPState *state, CVector* new, NRange* range, CUPValue* rtype, size_t count, CUPValueFor fn) {
  size_t split = sizeof(void*) * (count + 1);
  size_t* values = (size_t*)cup_malloc(split * 2);
  memset(values, 0, split * 2);
  const CUPType** types = (const CUPType**)((char*)values + split);
  for (size_t j = 0; j < count; j++) {
    CUPValue item = inferTypes(state, new, range, rtype, NULL);
    if (item.vtype == CV_ERROR) {
      cup_free(values);
      return item;
    }
    if (item.vtype == CV_LIST) {
      assert(0);
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
      types[j] = item.as.variable.value->typee;
      values[j] = item.as.variable.value->value;
      continue;
    }
    if (item.vtype == CV_ADDRESS) {
      if (item.as.address.type == NULL) {
        assert(0);
      }
      types[j] = item.as.address.type;
      values[j] = item.as.address.pvalue ? *item.as.address.pvalue : 0;
      continue;
    }
    assert(0);
  }
  values[count] = count;
  types[count] = NULL;
  return fn(values, types);
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
    const CUPType* t = cup_type(state, CUP_TYPE_ARRAY, (uint16_t)count, (const void*)right.as.list.types[0]);
    CUPValue v = (CUPValue){CV_VALUE, .as.value = {.type = t, .value = (size_t)off } };
    cup_free(right.as.list.values);
    return v;
    }
  case T_NUMBER: {
    size_t value = getNode(range)->value;
    vector_push(new, start, (char*)range->it - start);
    return (CUPValue){CV_VALUE, .as.value = {.type = &cup_type_int, .value = value}};
  }
  case T_STRING: case T_MSTRING: {
    const uint8_t* str = getData(state, getNode(range)->value);
    vector_push(new, start, (char*)range->it - start);
    size_t count = strlen((char*)str) + 1;
    assert(count < UINT16_MAX);
    const CUPType* t = cup_type(state, CUP_TYPE_ARRAY, (uint16_t)count, (const void *){&cup_type_uint8});
    return (CUPValue){CV_VALUE, .as.value = {.type = t, .value = (size_t)str}};
  }
  case T_THIS: {
    CVariable *var = varLookup(state, "$", state->scope);
    if (var == NULL) {
      cup_error("var == NULL");
      return (CUPValue){CV_ERROR, {}};
    }
    vector_push(new, start, (char*)range->it - start);
    return (CUPValue){CV_VARIABLE, .as.variable = {var, "$"}};
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
    if (left.as.value.type == &cup_type_void) {
      assert(0);
    }
    if (rtype == NULL) {
      assert(0);
    }
    *rtype = left;
    return (CUPValue){CV_ANY, .as.value.type = &cup_type_void};
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
  ctx->value.as.address.pvalue  = (size_t*)lvalue;
  ctx->value.as.address.type = type;
}

void cup_gen_value(CUPGenContext *ctx, void *value, const CUPType* type) {
  ctx->value.vtype = CV_VALUE;
  ctx->value.as.value.value = (size_t)value;
  ctx->value.as.value.type = type;
}

void cup_gen_call(CUPGenContext *ctx, void *value, size_t max, const CUPType* const* types) {
  ctx->value.vtype = CV_FUNCTION;
  ctx->value.as.value.value = (size_t)value;
  ctx->value.as.value.type = cup_type(ctx->state, CUP_TYPE_FUNCTION, max, types);
}