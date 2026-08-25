#define CUPE 1

#include "./include/libcup.h"
#if CUPE
#include "./include/libcupext.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/sodump.h"

#include <stdarg.h>

#define VERSION "1.0.0"

static void printUsage() {
  puts("Usage:\tcup [options] <file>\nOptions:\n"
       "-?, -h, --help\t\t\tShow this help\n"
       "-v, --version\t\t\tShow the version\n"
       "-n\t\t\t\tNo warnings\n"
       "-x\t\t\t\tCompile x32\n"
       "-d\t\t\t\tAdd DebugInfo\n"
       "-b, --bytecode <file>\t\tDump parsed AST/nodes to <file>\n"
       "-i, --instructions <file>\tDump codegen'd machine code to <file>\n");
}

static size_t l = 0;
size_t get_size(size_t v) {
  printf("get_size: %ld\n", v);
  return l;
}

size_t myprint(size_t v) { printf("%ld\n", v); return v + 1; }
size_t myprint2(size_t v, size_t (*cb)(size_t v)) {
  printf("%ld 2+2\n", v);
  return cb(v + 1);
}
void myprint3(char* str) {
  printf("\n----s-- %s --s-- \n", str);
}

void pp(size_t i, ...) {
  va_list ap;
  va_start(ap, i);

  printf("---------------\n");
  for (size_t j = 0; j < i; ++j) {
    printf("%zu\n", va_arg(ap, size_t));
  }
  printf("---------------\n");
  va_end(ap);
}

void* r(void *ptr, size_t size) {
  //printf("REALLOC: %p, size = %ld\n", ptr, size);
  if (size != 0) {
    void *p = realloc(ptr, size);
    if (p)
      return p;
    cup_log(CUP_Level_ERRO, "memory full for reallocator");
    exit(CUP_ERR_MOMORY);
  }
  free(ptr);
  return NULL;
}

size_t size4() {
  return 4;
}
size_t size8() {
  return 8;
}
#if CUPE
CUP_GEN_RESULT get_size_wrapper(CUPState* state,  CValue* args, size_t max, CValue* rvalue) {
  // char tname[256];
  // cup_type_snname(tname, sizeof(tname), var->type);

  // if (args.value)
  //   printf("State: %p %s %ld\nV = %ld, %ld %ld\n", state, tname, var->value, *args.value, i, max);
  // else
  //   printf("State: %p %s %ld\nV = NULL, %ld %ld\n", state, tname, var->value, i, max);
  printf("max = %ld -------\n", max);
  if (max != 1) {
    cup_error("get_size_wrapper: argc is incorrect");
    return CUP_GEN_ERROR;
  }
  if (args[1].type == NULL) {
    cup_error("get_size_wrapper: arg type is NULL");
    return CUP_GEN_ERROR;
  }
  if (args[1].type->realtype == CUP_TYPE_INT) {
    *rvalue = (CValue){&cup_type_int, (void*)sizeof(size_t), NULL};
  } else {
    *rvalue = (CValue){&cup_type_int, NULL, NULL};
  }
  return CUP_GEN_DONE;
  (void)state;
}

size_t count_per(const char* str) {
  size_t count = 0;
  while (*str) {
    if (*str++ == '%') count++;
  }
  return count;
}

CUP_GEN_RESULT printf_wrapper(CUPState *state, CValue* args, size_t max, CValue* rvalue) {
  static const char* format = NULL;
  if (max > 0) {
    if (args[1].value == NULL) {
      printf("args.value == NULL: %ld\n", max);
      return CUP_GEN_ERROR;
    }
    if (args[1].type->realtype != CUP_TYPE_ARRAY || args[1].type->elements[0]->realtype != CUP_TYPE_UINT8) {
      char tname[256];
      cup_type_snname(tname, sizeof(tname), args[1].type);
      cup_errorf("args.type( %s ) is not uint8[]", tname);
      return CUP_GEN_ERROR;
    }
    size_t str_id = *(size_t *)args[1].value;
    format = (char *)getData(state, str_id);
#define FORMAT_CELL 1
    size_t c = count_per(format) + FORMAT_CELL;
#undef FORMAT_CELL
    if (c != max) {
      cup_errorf("argc(%ld) != argc(%ld) format(\"%s\")", max, c, format);
      return CUP_GEN_ERROR;
    }
  }
  assert(UINT8_MAX > max);
  const CUPType* DTypes[UINT8_MAX] = {};
  for (size_t i = 1; i <= max; i++) {
    DTypes[i] = args[i].type;
  }
  DTypes[max + 1] = &cup_type_void;
  DTypes[0] = &cup_type_void;
  CUPType fn_t = (CUPType){ sizeof(void *), (uint16_t)sizeof(void *), CUP_TYPE_FUNCTION, { DTypes } };
  args[0].type = cup_type_put(state, fn_t);
  *rvalue = (CValue){&cup_type_void, NULL, NULL};
  return CUP_GEN_DONE;
}
#endif

void symbol_cb(void *ctx, const char *name, const CVariable var) {
  CUPState* state = (CUPState*)ctx;
  (void)state;
  char tname[256];
  cup_type_snname(tname, sizeof(tname), var.type);
  //char* tname = cup_type_name(type);
  printf("%s[%ld], type= %s, value= %ld\n", name, var.scope, tname, var.value);
  //r(tname, 0);
}

int main(int argc, char **argv) {
  cup_set_realloc(r);
  CUPState* state = cup_new(-1);
  if (!state) {
#if CUPE
    cup_error("Failed to create CUPState");
#else
    fputs("Failed to create CUPState\n", stderr);
#endif
    return 1;
  }
  {
    const CUPType ft = cup_type_function(&cup_type_int, &cup_type_int);
    const CUPType ft2 = cup_type_function(&cup_type_int, &cup_type_int, &ft);
    const CUPType ft3_p = cup_type_pointer(&cup_type_uint8);
    const CUPType ft3 = cup_type_function(&cup_type_void, &ft3_p);
#if CUPE
    const CUPType gent = cup_type_generator(printf_wrapper);
    const CUPType gent2 = cup_type_generator(get_size_wrapper);
    //cup_add_symbol(state, "printf", (void *)printf, gent);
    //cup_add_symbol(state, "sizeof", (void *)get_size, gent2);
#endif
    //const CUPType ft3 = cup_type_function(&cup_type_int, &cup_type_int);
    cup_add_symbol(state, "print", (void *)myprint, ft);
    cup_add_symbol(state, "print2", (void *)myprint2, ft2);
    cup_add_symbol(state, "print3", (void *)myprint3, ft3);
  }
  int flagmask = 1;
  const char *bytecode_path = NULL;
  const char *instructions_path = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bytecode") == 0) {
      if (++i >= argc) {
        cup_errorf("Error: %s requires a filename argument", argv[i - 1]);
        cup_delete(state);
        return 1;
      }
      bytecode_path = argv[i];
      cup_bytecode_path(state, bytecode_path);
    } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--instructions") == 0) {
      if (++i >= argc) {
        cup_errorf("Error: %s requires a filename argument", argv[i - 1]);
        cup_delete(state);
        return 1;
      }
      instructions_path = argv[i];
      cup_code_path(state, instructions_path);
    } else if (strcmp(argv[i], "-?") == 0 || strcmp(argv[i], "-h") == 0 ||
               strcmp(argv[i], "--help") == 0) {
      flagmask &= ~(1 << 0);
      flagmask |= 2;
    } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
      flagmask &= ~(1 << 0);
      flagmask |= 4;
    } else if (argv[i][0] == '-') {
      cup_errorf("Error: unknown option %s", argv[i]);
      cup_delete(state);
      return 1;
    } else {
      flagmask &= ~(1 << 0);
      CUPModule* m = cup_compile_file(state, argv[i]);
      if (m == NULL) {
        cup_errorf("Error: failed to compile %s", argv[i]);
        cup_delete(state);
        return 1;
      }
      if (bytecode_path)
        cup_write_bytecode(state, m);
      if (instructions_path)
        cup_write_code(state, m);
    }
  }

  if (flagmask & 1) {
#if CUPE
    cup_error("Error: no input file given");
#else
    fputs("Error: no input file given", stderr);
#endif
    printUsage();
    cup_delete(state);
    return 1;
  }
  if (flagmask & 2) {
    printUsage();
    cup_delete(state);
    return 0;
  }
  if (flagmask & 4) {
    printf("CUP %i.%i\n", cup_version_major(), cup_version_minor());
    cup_delete(state);
    return 0;
  }

  printf("OK\n");
#if CUPE
  CVariable *add = cup_get_symbol(state, "s");
  CVariable *a = cup_get_symbol(state, "a");
  if (a) {
    printf("a = %ld\n", a->value);
  }
  if (add) {
    //typedef size_t (*Fadd)(size_t, size_t);
    //size_t r = ((Fadd)*add)(54, 10);
    //printf("add(54, 10) -> %ld\n", r);
    typedef size_t (*Ft)(size_t);
    //size_t r = 251;
    //r = ((Ft)minus->value)(0);
    //printf("minus(0) -> %ld\n", r);
    printf("s(2) -> %ld\n", ((Ft)add->value)(2));
  }
  if (a) {
    printf("a = %ld\n", a->value);
  }
  cup_list_symbols(state, state, symbol_cb);
#endif

#if 0
  SoSymTable* t = sodump_parse("/usr/lib/libc.so.6");

  if (!t) return 0;

  for (size_t i = 0; i < t->count; i++) {
    if (t->syms[i].is_defined)
    printf("%s %i %i %zu %zu\n", t->syms[i].name, t->syms[i].is_func,
      t->syms[i].is_defined, t->syms[i].addr, t->syms[i].size);
  }
  

  sodump_free(t);

  return 0;

  
  //(CUPType){ sizeof(void*), sizeof(void*), CUP_TYPE_FUNCTION, types };
  //const CType *args[] = {
  //    cup_type_get_number(state, sizeof(size_t), false),
  //};
  //const CType *args_type = cup_type_get_complex(state, 1, args);
  //const CType *vt = cup_type_get_void(state);
  //const CType *ft = cup_type_get_function(state, ABI_DEFAULT, args_type, vt);
  //cup_add_symbol(state, "print", (void *)myprint, ft);

  printf("LIST:\n");
  cup_list_symbols(state, state, symbol_cb);
  printf("OK\n");
  while (1) {
    char buf[1024] = {0};
    if (!fgets(buf, sizeof(buf), stdin)) break;  // EOF
    buf[strcspn(buf, "\n")] = 0;  // remove newline
    if (strcmp(buf, "exit") == 0) break;
    if (cup_compile_string(state, buf)) {
      cup_delete(state);
      return 1;
    }
  }

  return 0;
  CVariable *a = cup_get_symbol(state, "g");
  CVariable* add = cup_get_symbol(state, "add");
  if (a)
    printf("g = %s\n", (char*)cup_var_value(a));
  else
    printf("NO A\n");
  if (add && cup_var_value(add)) {
    //typedef size_t (*Fadd)(size_t, size_t);
    //size_t r = ((Fadd)*add)(54, 10);
    //printf("add(54, 10) -> %ld\n", r);
    typedef size_t (*Fadd)(size_t, size_t);
    size_t r = ((Fadd)cup_var_value(add))(111, 543);
    printf("add(111, 543) -> %ld\n", r);
  }
#endif
  printf("OK\n");
  cup_delete(state);
  return 0;
}