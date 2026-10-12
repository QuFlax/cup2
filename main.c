#define CUPE 1

#include "./include/libcup.h"
#if CUPE
#include "./include/libcupext.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#include "include/sodump.h"

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

size_t myprint(size_t v) { printf("%zu\n", v); return v + 1; }
size_t myprint2(size_t v, size_t (*cb)(size_t v)) {
  printf("%zu 2+2\n", v);
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
  //static size_t count = 0;
#if 1
  if (size != 0) {
    void *p = realloc(ptr, size);
    //if (ptr == NULL) count++;
    //printf("%c|- %zu / %zu\n", (ptr == NULL) ? 'A' : 'R', size, count);
    if (p) {
      return p;
    }
    cup_log(CUP_Level_ERRO, "memory full for reallocator");
    exit(1);
  }
  //count--;
  //printf("F|- %zu\n", count);
  free(ptr);
  return NULL;
#else
  static char mem[16384 * 2] = {};
  static size_t index = 0;
  static char memc[100 * sizeof(struct MBlock)] = {};
  static CVector mv = {memc, 0, sizeof(memc)};
  if (size != 0) {
    if (1) {
      all += size;
      char* p = mem + index;
      if (ptr) {
        memcpy(p, ptr, 0);
      }
      struct MBlock bl = { p, size };
      vector_push(&mv, &bl, sizeof(bl));
      printf("%c | %p = %zu / %zu\n", (ptr == NULL) ? 'A' : 'R', p, size, all);
      index += size;
      return (void*)p;
    }
  }
  for (size_t i = 0; i < mv.size; i += sizeof(struct MBlock)) {
    struct MBlock* fg = (struct MBlock*)((char*)mv.data + i);
    if (fg->ptr != ptr) continue;
    all -= fg->size;
    printf("F | %p = %zu / %zu\n", fg, fg->size, all);
    struct MBlock bl = {};
    memcpy(&bl, (char*)mv.data + mv.size - sizeof(bl), sizeof(bl));
    memcpy((char*)mv.data + mv.size - sizeof(bl), fg, sizeof(bl));
    memcpy(fg, &bl, sizeof(bl));
    vector_pop(&mv, sizeof(bl));
    return NULL;
  }
  return NULL;
#endif
}

#if CUPE

CUP_GEN_RESULT get_size_wrapper_meta(CUPGenContext *ctx, CUP_META_OP op, void* value, CUP_META meta) {
  (void)value;
  switch (op) {
  case CUP_MOP_MEMBER: // .
    break;
  case CUP_MOP_BRACK: // []
    break;
  case CUP_MOP_CALL: // (...)
  {
    if (meta.call.max != 1) {
      cup_errorf("get_size_wrapper: argc(%zu) is incorrect", meta.call.max);
      return CUP_GEN_ERROR;
    }
    if (meta.call.argtypes[0] == NULL) {
      cup_error("get_size_wrapper: types[0] is NULL");
      return CUP_GEN_ERROR;
    }
    if (meta.call.argtypes[0]->cuptype == CUP_TYPE_INT) {
      cup_gen_value(ctx, (void*)sizeof(size_t), &cup_type_int);
    } else {
      cup_gen_value(ctx, (void*)0, &cup_type_int);
    }
    return CUP_GEN_DONE;
  }
  }
  return CUP_GEN_ERROR;
}

CUP_GEN_RESULT printf_wrapper_meta(CUPGenContext *ctx, CUP_META_OP op, void* value, CUP_META meta) {
  (void)value;
  switch (op) {
  case CUP_MOP_MEMBER: // .
    break;
  case CUP_MOP_BRACK: // []
    break;
  case CUP_MOP_CALL: // (...)
    {
      if (meta.call.max > 0) {
        if (meta.call.argvalues[0] == 0) {
          //cup_error("");
          return CUP_GEN_ERROR;
        }
        const CUPType *ptr = meta.call.argtypes[0];
        if (ptr->cuptype != CUP_TYPE_ARRAY || ptr->arrtype.element->cuptype != CUP_TYPE_UINT8) {
          char tname[256];
          cup_type_snname(tname, sizeof(tname), ptr);
          cup_errorf("First args.type( %s ) is not uint8[]", tname);
          return CUP_GEN_ERROR;
        }
        const char* format = (char*) meta.call.argvalues[0];
        size_t count = 1; // FORMAT_CELL
        for (const char* str = format; *str; str++)
          if (*str == '%') count++;
        if (count != meta.call.max) {
          cup_errorf("argc(%ld) != count(%ld) format(\"%s\")", meta.call.max, count, format);
          return CUP_GEN_ERROR;
        }
      }
      assert(UINT8_MAX > (meta.call.max + 1));
      const CUPType* DTypes[UINT8_MAX] = {};
      memcpy(DTypes + 1, meta.call.argtypes, meta.call.max * sizeof(void*));
      DTypes[0] = &cup_type_void;
      DTypes[meta.call.max + 1] = &cup_type_void;
      
      cup_gen_call(ctx, (void*)printf, meta.call.max, DTypes);
      return CUP_GEN_DONE;
    }
  }

  return CUP_GEN_ERROR;
}
#endif

void symbol_cb(void *ctx, const char *name, const CUPType *type, size_t value, size_t scope) {
  (void)ctx;
  char tname[256];
  cup_type_snname(tname, sizeof(tname), type);
  printf("%s[%zu], type= %s, value= %zu\n", name, scope, tname, value);
}

int main(int argc, char **argv) {
  size_t x = 277;
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
    ///const CUPType* ft = cup_typef(state, 2, &cup_type_int, &cup_type_int);
    const void* ft_a[] = {&cup_type_int, &cup_type_int};
    const CUPType* ft = cup_type(state, CUP_TYPE_FUNCTION, 1, ft_a);
    const void* ft2_a[] = {&cup_type_int, &cup_type_int, ft};
    const CUPType* ft2 = cup_type(state, CUP_TYPE_FUNCTION, 2, ft2_a);
    const void* ft3_p_a[] = {&cup_type_uint8};
    const CUPType* ft3_p = cup_type(state, CUP_TYPE_FUNCTION, 0, ft3_p_a);
    const void* ft3_a[] = {&cup_type_void, ft3_p};
    const CUPType* ft3 = cup_type(state, CUP_TYPE_FUNCTION, 1, ft3_a);
    //const CUPType* ft2 = cup_typef(state, 3, &cup_type_int, &cup_type_int, ft);
    //const CUPType* ft3_p = cup_typef(state, 1, &cup_type_uint8);
    //const CUPType* ft3 = cup_typef(state, 2, &cup_type_void, ft3_p);
#if CUPE
    // const CUPType* gent = cup_typeExt(state, printf_wrapper_meta);
    const CUPType* gent = cup_typemeta(state, 0, printf_wrapper_meta);
    // const CUPType* gent2 = cup_typeExt(state, get_size_wrapper_meta);
    const CUPType* gent2 = cup_typemeta(state, 0, get_size_wrapper_meta);
    cup_add_symbol(state, "printf", NULL, gent);
    cup_add_symbol(state, "sizeof", NULL, gent2);
#endif
    //const CUPType ft3 = cup_type_function(&cup_type_int, &cup_type_int);
    cup_add_symbol(state, "print", (void *)myprint, ft);
    cup_add_symbol(state, "print2", (void *)myprint2, ft2);
    cup_add_symbol(state, "print3", (void *)myprint3, ft3);
    cup_add_symbol(state, "main", NULL, &cup_type_void);

    cup_add_symbol(state, "x", (void *)x, &cup_type_int);
  }
  int flagmask = 1;
  const char *instructions_path = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bytecode") == 0) {
      if (++i >= argc) {
        cup_errorf("Error: %s requires a filename argument", argv[i - 1]);
        cup_delete(state);
        return 1;
      }
      cup_bytecode_path(state, argv[i]);
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

  //cup_output_object(state, , "out.o");

#if CUPE
  CVariable *get1 = cup_get_symbol(state, "get1");
  CVariable *a = cup_get_symbol(state, "a");
  CVariable *mainf = cup_get_symbol(state, "main");
  if (a) {
    size_t* arrs = (size_t*)cup_var_value(a);
    printf("a = %p\n", arrs);
    if (arrs) {
      printf("a[0] = %zu\n", arrs[0]);
      printf("a[1] = %zu\n", arrs[1]);
    }
  }
  if (mainf) {
    void* mainfv = cup_var_value(mainf);
    typedef size_t (*Ft)();
    printf("%p main() -> %zu\n", mainfv, ((Ft)mainfv)());
  }
  if (get1) {
    void* get1v = cup_var_value(get1);
    //typedef size_t (*Fadd)(size_t, size_t);
    //size_t r = ((Fadd)*add)(54, 10);
    //printf("add(54, 10) -> %ld\n", r);
    typedef size_t (*Ft)();
    //size_t r = 251;
    //r = ((Ft)minus->value)(0);
    //printf("minus(0) -> %ld\n", r);
    printf("%p get1() -> %zu\n", get1v, ((Ft)get1v)());
  }
  cup_list_symbols(state, NULL, symbol_cb);
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