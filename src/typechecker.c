#include "../include/cup.h"

#ifndef CUP_DEFTYPE_MAX_PASSES
#define CUP_DEFTYPE_MAX_PASSES 64
#endif

void cup_gen_value(CUPGenContext *ctx, const CUPType *type, void *value) {

}

void cup_gen_call(CUPGenContext *ctx, const CUPType *rtype, void *value) {
  (void)ctx;
  (void)rtype;
  (void)value;
  assert(0);
}

static inline Node* getNode(Nodes nodes, size_t* i) {
  if (*i >= (nodes.size / sizeof(*nodes.data))) {
    cup_error("Incorrect Nodes");
    exit(1);
  }
  return nodes.data + (*i)++;
}

typedef enum CUPValueType {
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
    size_t *vvalue;
    struct c1 {
      const CUPType *cuptype;
      size_t* svalue;
    } c1;
    struct c2 {
      //const CUPType *cuptype;
      CVariable *variable;
    } c2;
    struct c3 {
      const CUPType** types;
      size_t** values;
    } c3;
  } as;
} CUPValue;

#define cup_type_is(cvlaue, ctype) cvlaue.cuptype->type == ctype 

//static CUPType cup_count = { 0, 0, CUP_TYPE_COUNT, {} };
//static void* cup_any = &cup_count.size;

const CUPType* promote(const CUPType* left, const CUPType* right) {
  if (left->type != right->type)
    return NULL;
  if (left->type != CUP_TYPE_FUNCTION)
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

/*
static CUP_GEN_RESULT fffs(CUPState* state, const CUPType **argtypes, void **argvalues, size_t max) {
  (void)state;
  const CUPType* type = NULL;
  size_t arr_id = *(size_t *)argvalues[max];
  printf("arr_id = %ld\n", arr_id);
  size_t* arr = (size_t*)getData(state, arr_id);
  for (size_t i = 0; i < max; i++) {
    if (argtypes[i] == NULL) continue;
    arr[i] = *(size_t*)argvalues[i];
    if (type == NULL) {
      type = argtypes[i];
      continue;
    }
    if (type != argtypes[i])
      return CUP_GEN_ERROR;
  }
  //argtypes[0] = type;
  return CUP_GEN_DONE;
}
*/

int defTypeU(CUPState *state, Nodes nodes, size_t* i, CUPValue* rvalue, const CUPType** rtype);

typedef struct ArgsData {
  const CUPType** types;
  void** values;
} ArgsData;

/*

static const CUPType* argsfn(CUPState* state, Nodes nodes, size_t* i, const CUPType* fn, size_t max) {
  size_t j = 0;
  while (j < max) {
    CUPValue arg_type = {};
    if (defTypeU(state, nodes, i, &arg_type, NULL))
      return &cup_count;
    if (arg_type.cuptype == NULL)
      goto next;
    if (arg_type.cuptype->type == CUP_TYPE_COUNT) {
      cup_error("args: argument type is CUP_TYPE_COUNT");
      return &cup_count;
    }
    if (fn->elements[j + 1] == &cup_type_void)
      goto next;

    if (fn->elements[j + 1] == NULL) {
      fn->elements[j + 1] = arg_type.cuptype;
      goto next;
    }

    if (promote(fn->elements[j + 1], arg_type.cuptype) == NULL) {
      char tname[256], tname2[256];
      cup_type_snname(tname, sizeof(tname), fn->elements[j + 1]);
      cup_type_snname(tname2, sizeof(tname2), arg_type.cuptype);
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

int defTypeUR(CUPState *state, Nodes nodes, size_t* i, CUPValue* value, const CUPType** rtype);

int defTypeU(CUPState *state, Nodes nodes, size_t* i, CUPValue* rvalue, const CUPType** rtype) {
  if (!state) {
    cup_error("defType state are NULL");
    return 1;
  }
  Node n = *getNode(nodes, i);
  switch (n.token) {
  case N_BLOCK:
    state->scope++;
  case T_COMMA:
    cup_count.size = getNode(nodes, i)->value;
    *rvalue = (CUPValue){&cup_count, NULL, NULL, 0};
    return 0;
  case N_ARRAY: {
    //CValue left = {&cup_type_void, NULL, NULL};
    size_t* id = &(getNode(nodes, i)->value);
    CUPValue right = {};
    if(defTypeU(state, nodes, i, &right, NULL))
      return 1;
    if (right.type == NULL || right.type->type != CUP_TYPE_COUNT) {
      cup_error("!arr[args]");
      return 1;
    }
    const CUPType gent = cup_type_generator(fffs);
    ArgsData data = {};
    if (argsgen2(state, nodes, i, cup_type_put(state, gent), id, right.type->size, &data))
      return 1;
    *rvalue = (CUPValue){cup_type_put(state,
        cup_type_array(data.types[0], right.type->size)
    ), id, NULL, 0};
    // DOTO change left value
    // data.values[0];
    cup_free(data.types);
    return 0;
  }
  case T_NUMBER: {
    *rvalue = (CUPValue){&cup_type_int, &(getNode(nodes, i)->value), NULL, 0};
    return 0;
  }
  case T_DOUBLE: {
    *rvalue = (CUPValue){&cup_type_double, &(getNode(nodes, i)->value), NULL, 0};
    return 0;
  }
  case T_STRING: case T_MSTRING: {
    size_t* id = &(getNode(nodes, i)->value);
    *rvalue = (CUPValue){cup_type_put(state,
      cup_type_array(&cup_type_uint8, strlen((char*)getData(state, *id)) + 1)
    ), id, NULL, 0};
    return 0;
  }
  case T_ADD: case T_SUB: case T_MUL: case T_DIV: case T_MOD: {
    CUPValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;
    CUPValue right = {};
    if(defTypeU(state, nodes, i, &right, NULL))
      return 1;
    if (left.type == NULL || right.type == NULL)
      return 0;
    if (left.type->type == CUP_TYPE_COUNT || right.type->type == CUP_TYPE_COUNT)
      return 1;
    const CUPType* type = promote(left.type, right.type);
    if (type == NULL)
      return 0;
    *rvalue = (CUPValue){type, cup_any, NULL, 0};
    return 0;
  }
  case T_EQEQ: case T_NOTEQ: case T_LESS: case T_LESSEQ: case T_GREAT: case T_GREATEQ: {
    CUPValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;
    CUPValue right = {};
    if(defTypeU(state, nodes, i, &right, NULL))
      return 1;
    if (left.type == NULL || right.type == NULL)
      return 0;
    if (left.type->type == CUP_TYPE_COUNT || right.type->type == CUP_TYPE_COUNT)
      return 1;
    const CUPType* type = promote(left.type, right.type);
    if (type == NULL)
      return 0;
    *rvalue = (CUPValue){&cup_type_int, cup_any, NULL, 0};
    return 0;
  }
  case T_ADDEQ: case T_EQ: {
    CUPValue right = {};
    if(defTypeU(state, nodes, i, &right, NULL))
      return 1;
    CUPValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;
    if (left.variable == NULL && left.isLValueArr == 0) {
      cup_error("!lhs.isLValue");
      return 1;
    }
    if (right.type == NULL)
      return 0;
    if (right.type->type == CUP_TYPE_COUNT)
      return 1;
    if (left.type == NULL) {
      left.variable->type = right.type;
      *rvalue = right;
      return 0;
    }
    if (left.type->type == CUP_TYPE_COUNT)
      return 1;
    if (left.type != right.type) {
      cup_error("!canConvert(rhs.type, lhs.type)");
      return 1;
    }
    *rvalue = right;
    return 0;
  }
  case T_RETURN: {
    CUPValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;

    if (left.type == NULL)
      return 0;
    if (left.type->type == CUP_TYPE_COUNT)
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
    *rvalue = (CUPValue){var->type, &var->value, var, 1};
    return 0;
  }
  case T_AT: {
    // TODO: do something with return because we need to consume all nodes range
    CUPValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;
    //CVariable* var = left.variable;
    state->scope++;
    CUPValue right = {};
    if(defTypeU(state, nodes, i, &right, NULL))
      return 1;
    if (left.variable == NULL || left.type == NULL) {
      cup_error("!fn");
      return 1;
    }
    if (right.type == NULL || right.type->type != CUP_TYPE_COUNT) {
      cup_error("!fn(args)");
      return 1;
    }
    if (left.type->type != CUP_TYPE_FUNCTION
      //&& left.type->type != CUP_TYPE_GENERATOR // AT not needed
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
    CUPValue left = {};
    if (defTypeU(state, nodes, i, &left, NULL))
      return 1;
    CUPValue index = {};
    if (defTypeU(state, nodes, i, &index, NULL))
      return 1;
    if (left.type == NULL || index.type == NULL)
      return 0;
    if (cup_type_is(left, CUP_TYPE_COUNT) ||
        cup_type_is(index, CUP_TYPE_COUNT))
      return 1;
    if (cup_type_is(left, CUP_TYPE_ARRAY)) {
      if (left.type->elements == NULL || left.type->elements[0] == NULL) {
        cup_error("T_OSB: array has no element type");
        return 1;
      }
      *rvalue = (CUPValue){ left.type->elements[0], cup_any, NULL, 1 };
      return 0;
    } else if (left.type->type == CUP_TYPE_POINTER) {
      if (left.type->elements == NULL || left.type->elements[0] == NULL) {
        cup_error("T_OSB: pointer has no pointee type");
        return 1;
      }
      *rvalue = (CUPValue){ left.type->elements[0], cup_any, NULL, 1 };
      return 0;
    } else {
      char tname[256];
      cup_type_snname(tname, sizeof(tname), left.type);
      cup_errorf("T_OSB: cannot index type %s", tname);
      return 1;
    }
  }
  case T_CALL: {
    size_t save = *i;
    CUPValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;
    //CVariable* var = left.variable;
    CUPValue right = {};
    if(defTypeU(state, nodes, i, &right, NULL))
      return 1;
    if (left.variable == NULL || left.type == NULL) {
      cup_error("!fn");
      return 1;
    }
    if (right.type == NULL || right.type->type != CUP_TYPE_COUNT) {
      cup_error("!fn(args)");
      return 1;
    }
    const CUPType* type = NULL;
    if (cup_type_is(left, CUP_TYPE_GENERATOR)) {
      ArgsData data = {};
      if (argsgen2(state, nodes, i, left.type, left.value, right.type->size, &data))
        return 1;
      type = data.types[0];
      if (left.value != data.values[0]) {
        printf("change ");
        
      }
      // DOTO change left value
      // data.values[0];
      cup_free(data.types);
    } else if (cup_type_is(left, CUP_TYPE_FUNCTION)) {
      type = argsfn(state, nodes, i, left.type, right.type->size);
    } else {
      char tname[256];
      cup_type_snname(tname, sizeof(tname), left.type);
      cup_errorf("defType call not Function or Generator get %s", tname);
      return 1;
    }
    if (type == &cup_count)
      return 1;
    *rvalue = (CUPValue) {type, &cup_count.size, NULL, 0};
    return 0;
  }
  case T_BREAK: case T_CONTINUE: {
    *rvalue = (CUPValue){&cup_type_void, NULL, NULL, 0};
    return 0;
  }
  case T_IF: {
    CUPValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;
    if (left.type && left.type->type == CUP_TYPE_COUNT)
      return 1;
    CUPValue result = {};
    if(defTypeUR(state, nodes, i, &result, rtype))
      return 1;
    *rvalue = result;
    return 0;
  }
  case T_WHILE: {
    CUPValue left = {};
    if(defTypeU(state, nodes, i, &left, NULL))
      return 1;
    if (left.type && left.type->type == CUP_TYPE_COUNT)
      return 1;
    CUPValue result = {};
    if(defTypeUR(state, nodes, i, &result, rtype))
      return 1;
    *rvalue = result;
    return 0;
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


  default:
    cup_errorf("defType UNKNOWN node make for '%s'", token_names[n.token]);
    break;
  }
  return 1;
}

int defTypeUR(CUPState *state, Nodes nodes, size_t* i, CUPValue* value, const CUPType** rtype) {
  if (!state) {
    cup_error("defType state are NULL");
    return 1;
  }
  size_t max = 1;
  for (size_t j = 0; j < max; j++) {
    CUPValue item = {};
    if (defTypeU(state, nodes, i, &item, rtype))
      return 1;
    if (item.type == NULL) {
      continue;
    }
    if (item.type->type == CUP_TYPE_COUNT) {
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

*/

typedef struct defCTX {
  CUPState* state;
  Nodes* nodes;
  Nodes* new;
  //size_t* i;
  //size_t extra;
} defCTX; //websdr

typedef CUPValue (*CUPValueFor)(defCTX ctx, size_t** values, const CUPType** types);

static CUPValue fnblock(defCTX ctx, size_t** values, const CUPType** types) {
  (void)ctx; (void)values; (void)types;
  return (CUPValue){CV_VALUE, .as.c1 = { &cup_type_void, NULL }};
}

static CUPValue fncomma(defCTX ctx, size_t** values, const CUPType** types) {
  (void)ctx;
  return (CUPValue){CV_LIST, .as.c3 = {types, values}};
}

static CUPValue fnarray(defCTX ctx, size_t** values, const CUPType** types) {
  (void)ctx;
  const CUPType* type = NULL;
  size_t count = ((size_t*)types)[-1];
  for (size_t i = 0; i < count; i++) {
    if (types[i] == NULL) continue;
    if (type && type != types[i]) {
      cup_free(values);
      return (CUPValue){CV_ERROR, {NULL}};
    }
    type = types[i];
  }
  return (CUPValue){CV_LIST, .as.c3 = {types, values}};
}

static CUPValue defTypeUU(defCTX ctx, size_t* i, CUPValueFor fn);

static CUPValue defFor(defCTX ctx, size_t* i, size_t count, CUPValueFor fn) {
  assert(fn);
  size_t split = sizeof(void*) * (count + 1);
  size_t** values = (size_t**)cup_malloc(split * 2);
  memset(values, 0, split * 2);
  const CUPType** types = (const CUPType**)((char*)values + split);
  for (size_t j = 0; j < count; j++) {
    CUPValue item = defTypeUU(ctx, i, NULL);
    if (item.vtype == CV_ERROR) {
      cup_free(values);
      return item;
    }
    if (item.vtype == CV_VALUE) {
      if (item.as.c1.cuptype == NULL) {
        assert(0);
      }
      if (item.as.c1.cuptype != &cup_type_void) {
        types[j] = item.as.c1.cuptype;
        values[j] = item.as.c1.svalue;
      }
      continue;
    }
    if (item.vtype == CV_VARIABLE) {
      types[j] = item.as.c2.variable->type;
      values[j] = &item.as.c2.variable->value;
      continue;
    }
    if (item.vtype == CV_LIST) {
      assert(0);
    }
    if (item.vtype == CV_ADDRESS) {
      if (item.as.c1.cuptype == NULL) {
        assert(0);
      }
      types[j] = item.as.c1.cuptype;
      values[j] = item.as.c1.svalue;
      continue;
    }
    assert(0);
  }
  values[count] = (size_t*)count;
  types[count] = NULL;
  return fn(ctx, values, types);
}

static CUPValue defTypeUU(defCTX ctx, size_t* i, CUPValueFor fn) {
  (void)fn;
  CUPValue rvalue;
  Node n = *getNode(*ctx.nodes, i);
  switch (n.token) {
  case T_NUMBER:
    rvalue = (CUPValue){CV_VALUE, .as.c1 = {&cup_type_int, &getNode(*ctx.nodes, i)->value } };
    break;
  case T_STRING: case T_MSTRING: {
    const uint8_t* str = getData(ctx.state, getNode(*ctx.nodes, i)->value);
    const CUPType t = cup_type_array(&cup_type_uint8, strlen((char*)str) + 1);
    rvalue = (CUPValue){CV_VALUE, .as.c1 = {cup_type_put(ctx.state, t), (size_t*)str} };
    }
    break;
  case N_VARIABLE: {
    CVariable *var = getVars(ctx.state, getNode(*ctx.nodes, i)->value, ctx.state->scope);
    if (var == NULL) {
      assert(0);
    }
    return (CUPValue){CV_VARIABLE, .as.c2.variable = var};
    }
    break;
  case N_BLOCK:
    ctx.state->scope++;
    return defFor(ctx, i, getNode(*ctx.nodes, i)->value, fnblock);
  case T_COMMA:
    return defFor(ctx, i, getNode(*ctx.nodes, i)->value, fn ? fn : fncomma);
  case N_ARRAY: {
    size_t* id = &getNode(*ctx.nodes, i)->value;
    CUPValue right = defTypeUU(ctx, i, fnarray);
    if (right.vtype == CV_ERROR) {
      return right;
    }
    if (right.vtype != CV_LIST) {
      assert(0);
    }
    size_t count = ((size_t*)right.as.c3.types)[-1];
    const CUPType t = cup_type_array(right.as.c3.types[0], count);
    CUPValue value = (CUPValue){CV_VALUE, .as.c1 = { cup_type_put(ctx.state, t), id } };
    cup_free(right.as.c3.values);
    return value;
    }
  case T_EQ: {
    CUPValue right = defTypeUU(ctx, i, NULL);
    CUPValue left = defTypeUU(ctx, i, NULL);
    if (left.vtype == CV_ERROR || right.vtype == CV_ERROR) {
      return left;
    }
    if (left.vtype == CV_VALUE) {
      if (right.vtype == CV_LIST) {
        cup_free(right.as.c3.values);
      }
      return (CUPValue){CV_ERROR, {i}};
    }
    if (left.vtype == CV_LIST) {
      cup_free(left.as.c3.values);
      if (right.vtype == CV_LIST) {
        cup_free(right.as.c3.values);
      }
      return (CUPValue){CV_ERROR, {i}};
    }
    if (left.vtype == CV_ADDRESS) {
      if (right.vtype == CV_LIST) {
        cup_free(right.as.c3.values);
        return (CUPValue){CV_ERROR, {i}};
      }
      if (right.vtype == CV_VALUE) {
        if (right.as.c1.cuptype == &cup_type_void) {
          return (CUPValue){CV_ERROR, {i}};
        }
        if (left.as.c1.cuptype && left.as.c1.cuptype != right.as.c1.cuptype) {
          return (CUPValue){CV_ERROR, {i}};
        }
        // TODO set address to value (*add = val)
        rvalue = right;
        break;
      }
      if (right.vtype != CV_VARIABLE) {
        assert(0);
      }
      if (left.as.c1.cuptype && left.as.c1.cuptype != right.as.c2.variable->type) {
        return (CUPValue){CV_ERROR, {i}};
      }
      // TODO set address to value (*add = var.val)
      rvalue = right;
      break;
    }
    if (left.vtype != CV_VARIABLE) {
      assert(0);
    }
    if (right.vtype == CV_LIST) {
      cup_free(right.as.c3.values);
      return (CUPValue){CV_ERROR, {i}};
    }
    if (right.vtype == CV_VALUE) {
      if (right.as.c1.cuptype == &cup_type_void) {
        return (CUPValue){CV_ERROR, {i}};
      }
      if (left.as.c2.variable->type && left.as.c2.variable->type != right.as.c1.cuptype) {
        return (CUPValue){CV_ERROR, {i}};
      }
      left.as.c2.variable->type = right.as.c1.cuptype;
      left.as.c2.variable->value = *right.as.c1.svalue;
      rvalue = right;
      break;
    }
    if (right.vtype != CV_VARIABLE) {
      assert(0);
    }
    if (left.as.c2.variable->type && left.as.c2.variable->type != right.as.c2.variable->type) {
      return (CUPValue){CV_ERROR, {i}};
    }
    left.as.c2.variable->type = right.as.c2.variable->type;
    left.as.c2.variable->value = right.as.c2.variable->value;
    rvalue = right;
    }
    break;
  case T_OSB: {
    CUPValue left = defTypeUU(ctx, i, NULL);
    CUPValue right = defTypeUU(ctx, i, NULL);
    if (left.vtype == CV_ERROR || right.vtype == CV_ERROR) {
      return left;
    }
    if (left.vtype == CV_VALUE) {
      if (right.vtype == CV_LIST)
        cup_free(right.as.c3.values);
      return (CUPValue){CV_ERROR, {i}};
    }
    if (left.vtype == CV_VARIABLE) {
      if (right.vtype == CV_LIST) {
        assert(0);
      }
      if (right.vtype == CV_VARIABLE) {
        assert(0);
      }
      if (right.vtype != CV_VALUE) {
        assert(0);
      }
      if (right.as.c1.cuptype == &cup_type_void) {
        return (CUPValue){CV_ERROR, {i}};
      }
      if (left.as.c2.variable->type == NULL
        || left.as.c2.variable->type->type != CUP_TYPE_ARRAY
        || left.as.c2.variable->type->elements == NULL
        || left.as.c2.variable->type->elements[0] == NULL) {
        assert(0);
      }
      // TODO CV_ADDRESS left(arr) + right(index)
      rvalue = (CUPValue){ CV_ADDRESS, .as.c1 = {left.as.c2.variable->type->elements[0], &left.as.c2.variable->value } };
      break;
    }
    if (left.vtype != CV_LIST) {
      assert(0);
    }
    assert(0);
    
    cup_free(left.as.c3.values);
    if (right.vtype == CV_LIST) {
      cup_free(right.as.c3.values);
    }
    }
    break;
  case T_CALL: {
    size_t save = *i;
    CUPValue left = defTypeUU(ctx, i, NULL);
    size_t end = *i;
    CUPValue right = defTypeUU(ctx, i, NULL);
    if (left.vtype == CV_ERROR || right.vtype == CV_ERROR) {
      return left;
    }
    if (left.vtype == CV_LIST) {
      cup_free(left.as.c3.values);
      if (right.vtype == CV_LIST) {
        cup_free(right.as.c3.values);
      }
      return (CUPValue){CV_ERROR, {i}};
    }
    if (left.vtype == CV_VALUE) {
      if (right.vtype == CV_LIST) {
        cup_free(right.as.c3.values);
      }
      return (CUPValue){CV_ERROR, {i}};
    }
    if (left.vtype != CV_VARIABLE) {
      assert(0);
    }
    if (right.vtype == CV_VALUE) {
      assert(0);
    }
    if (right.vtype == CV_VARIABLE) {
      assert(0);
    }
    if (right.vtype != CV_LIST) {
      assert(0);
    }
    if (left.as.c2.variable->type == NULL) {
      assert(0);
    }
    if (left.as.c2.variable->type->type == CUP_TYPE_FUNCTION) {
      assert(0);
      cup_free(right.as.c3.values);
      return (CUPValue){CV_ERROR, {i}};
    }
    if (left.as.c2.variable->type->type == CUP_TYPE_GENERATOR) {
      size_t count = ((size_t*)right.as.c3.types)[-1];
      CUPGenContext gc = {ctx.state};
      CUP_GEN_RESULT r = ((CUPTypeExt*)left.as.c2.variable->type)->gen(&gc, right.as.c3.types, right.as.c3.values, count);
      if (r == CUP_GEN_CONTINUE) {
        assert(0);
      }
      if (r == CUP_GEN_ERROR || right.as.c3.types[count] == NULL) {
        cup_free(right.as.c3.values);
        return (CUPValue){CV_ERROR, {i}};
      }
      CUPValue value = (CUPValue) {CV_VALUE, .as.c1 = {right.as.c3.types[count], NULL}};
      cup_free(right.as.c3.values);
      return value;
    }
    (void)save; (void)end;
    assert(0);
    }
    break;
  default:
    cup_errorf("defType UNKNOWN node make for '%s'", token_names[n.token]); 
    return (CUPValue){CV_ERROR, {i}};
  }
  return rvalue;
}

typedef struct SSC {
  size_t index;
  size_t scope;
} SSC;

CVecT(SSC);

static void defTypeErr(SSCs pending, const char* err) {
  size_t unresolved_count = pending.size / sizeof(*pending.data);
  cup_errorf(err, unresolved_count, unresolved_count == 1 ? "" : "s");
  cup_free(pending.data);
}

int defType(CUPState *state, Nodes* nodes) {
  if (state == NULL) {
    cup_error("defType: state is NULL");
    return 1;
  }
  return 1;
  Nodes nnodes = {};
  defCTX ctx = { state, nodes, &nnodes };
  //size_t scope = 0;
  SSC index = {0, 0};
  SSCs pending = {};
  vector_pushT(pending.vec, index);
  size_t pass = 0;
  do {
    size_t count = 1;
    SSC begin = *pending.data;
    state->scope = begin.scope;
    size_t i = begin.index;
    do {
      CUPValue value = defTypeUU(ctx, &i, NULL);
      if (value.vtype == CV_ERROR) {
        defTypeErr(pending,
          "defType: could not resolve %zu expression%s "
          "(undefined symbol or cyclic type dependency)");
        return 1;
      }
      if (value.vtype == CV_LIST) {
        cup_free(value.as.c3.values);
      }
      if (value.vtype == CV_VALUE) {}
      if (value.vtype == CV_VARIABLE) {}
      // vector_pushT(pending.vec, save);
      // continue;
    } while(--count);
    vector_fpop(&pending.vec, sizeof(begin));
    if (pass++ >= CUP_DEFTYPE_MAX_PASSES) {
      defTypeErr(pending,
        "defType: maximum inference passes reached; "
        "%zu expression%s remain unresolved");
      return 1;
    }
  } while(pending.size);
  return 0;
}

int cahngeType(const CUPType* left, const CUPType* right) {
  if (!left || !right) return 0;
  if (left->type == right->type) {
    if (left->type == CUP_TYPE_STRUCT)
      return INT_MAX;
    if (left->type == CUP_TYPE_FUNCTION) {
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
  if (left->type == CUP_TYPE_VOID)
    return 1;
  if (right->type == CUP_TYPE_VOID)
    return -1;
  return 0;
}