#include "./include/libcup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void printUsage() {
  puts("Usage:\tcup [options] <file>\nOptions:\n"
       "-? / -h\t\tShow this help\n"
       "-v\t\tShow the version\n"
       "-n\t\tNo warnings\n"
       "-x\t\tCompile x32\n"
       "-d\t\tAdd DebugInfo\n");
}

size_t myprint(size_t v) { printf("%ld\n", v); return v + 1; }
size_t myprint2(size_t v, size_t (*cb)(size_t v)) {
  printf("%ld 2+2\n", v);
  return cb(v + 1);
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

void symbol_cb(void *ctx, const char *name, const size_t val, const CUPType *type) {
  CUPState* state = (CUPState*)ctx;
  (void)state;
  char tname[256];
  cup_type_snname(tname, sizeof(tname), type);
  //char* tname = cup_type_name(type);
  printf("%s, type= %s, value= %ld\n", name, tname, val);
  //r(tname, 0);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printUsage();
    return 0;
  }

  cup_set_realloc(r);

  CUPState* state = cup_new(-1);
  if (!state) {
    fputs("Failed to create CUPState\n", stderr);
    return 1;
  }

  const CUPType ft = cup_type_function(&cup_type_int, &cup_type_int);
  const CUPType ft2 = cup_type_function(&cup_type_int, &cup_type_int, &ft);
  cup_add_symbol(state, "print", (void *)myprint, ft);
  cup_add_symbol(state, "print2", (void *)myprint2, ft2);
  //(CUPType){ sizeof(void*), sizeof(void*), CUP_TYPE_FUNCTION, types };
  //const CType *args[] = {
  //    cup_type_get_number(state, sizeof(size_t), false),
  //};
  //const CType *args_type = cup_type_get_complex(state, 1, args);
  //const CType *vt = cup_type_get_void(state);
  //const CType *ft = cup_type_get_function(state, ABI_DEFAULT, args_type, vt);
  //cup_add_symbol(state, "print", (void *)myprint, ft);

  for (int i = 1; i < argc; i++) {
    if (cup_compile_file(state, argv[i])) {
      cup_delete(state);
      return 1;
    }
  }
  printf("LIST:\n");
  cup_list_symbols(state, state, symbol_cb);
  printf("OK\n");
#if 0
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
#endif
  CVariable *a = cup_get_symbol(state, "a");
  CVariable* add = cup_get_symbol(state, "add");
  if (a)
    printf("a = %ld\n", (size_t)cup_var_value(a));
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

  printf("OK\n");
  cup_delete(state);
  return 0;
}
