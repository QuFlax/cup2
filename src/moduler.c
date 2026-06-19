#include "../include/cup.h"
//#include <threads.h>
#include <sys/stat.h>
#include <sys/mman.h>

Nodes statement(CUPState *state);

void* allocMemory(size_t size) {
#ifdef _WIN32
  void* ptr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (!ptr) {
    fprintf(stderr, "VirtualAlloc failed: %lu\n", GetLastError());
    exit(1);
  }
  return ptr;
#else
  void* mem = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem != MAP_FAILED)
    return mem;
  perror("mmap failed");
  exit(1);
#endif
}

static int samefile(const char* a, const char* b) {
#if 0
  //TODO: windows stuf
  return 0;
#else
  if (!a || a[0] == '0' || !b || *b == '0') return 0;
  struct stat sa, sb;
  if (stat(a, &sa) != 0 || stat(b, &sb) != 0) return 0;
  return (sa.st_ino == sb.st_ino) && (sa.st_dev == sb.st_dev);
#endif
}

void importModule(CUPState* state) {
  //CUPState copy;
  //memcpy(&copy, state, sizeof(CUPState));
  const char* name = getString(state, state->nodes.value);
  if (cup_compile_file(state, name))
    cup_errorf("Load module '%s'", name);
    
  //vector_pushT(left, state->nodes);
  //getToken(state);

  //state->input_stream = input_stream;
}

void externalModule(CUPState* state) {
  const char* name = getString(state, state->nodes.value);
  char temp[2048] = {0};
  strncpy(temp, name, sizeof(temp));
  char* dot = strrchr(temp, '.');
  if (!dot || (dot && strcmp(dot, ".so")))
      strcat(temp, ".so");

  /*void *handle = dlopen(temp, RTLD_LAZY); // "./libmylib.so"
  if (!handle) {
    printf("Error: %s\n", dlerror());
    return 1;
  }
  dlerror(); // Clear any existing error

  int (*add_func)(int, int) = dlsym(handle, "add");
  printf("2 + 3 = %d\n", add_func(2, 3));

  dlclose(handle);*/
  return;
}

void printVar(void* ctx, size_t key, size_t dist, const void* v) {
  CUPState* state = (CUPState*) ctx;
  CVARS* vars = (CVARS*)v;
  const char* str = getString(state, key);
  char tname[256];
  cup_type_snname(tname, sizeof(tname), vars->vars[0].type);
  printf("Var: [%zu]%s %ld:\n%s %ld %ld\n", key, str, dist, tname, vars->vars[0].value, vars->vars[0].scope);
  cup_type_snname(tname, sizeof(tname), vars->vars[1].type);
  printf("%s %ld %ld\n", tname, vars->vars[1].value, vars->vars[1].scope);
  //cup_type_snname(tname, sizeof(tname), v->type);
  //printf("Var: [%i]%s type= %s, ptr= %ld, %p, scope= %ld\n",
  //          0, "str", tname, v->value, (void*)v->value, v->scope);
}

void printNodes(CUPModule* buf) {
  for (Node* it = buf->it; it != buf->end; it++) {
    if (it->type == N_BLOCK || it->type == T_NUMBER || it->type == T_COMMA) {
      printf("%s - ", token_names[it->type]);
      printf("%ld\n", (++it)->value);
    } else if (it->type == T_IDENTIFIER) {
      printf("%s - ", token_names[it->type]);
      //printf("%p, %ld\n", buf->state, (++it)->value);
      printf("%s\n", getString(buf->state, (++it)->value));
    } else if (it->type == T_EQ || it->type == T_CALL) {
      printf("%s\n", token_names[it->type]);
    } else {
      printf("%s (%ld)\n", token_names[it->type], it->value);
    }
  }
}

void parse(CUPState *state, const char* buf, const char* file) {
  if (file) {
    for (CUPModuleList* it = state->modules; it; it = it->next)
      if (samefile(it->path, file))
        return;
  }
  CUPModule* m = (CUPModule*)cup_malloc(sizeof(CUPModuleList));
  memset(m, 0, sizeof(CUPModuleList));
  ((CUPModuleList*)m)->path = file;
  ((CUPModuleList*)m)->next = state->modules;
  state->modules = (CUPModuleList*)m;

  const char* input_stream = state->input_stream;
  const char* priv_stream = state->priv_stream;
  Node2 ncopy = state->nodes;

  state->priv_stream = NULL;
  state->input_stream = buf;
  state->loc.col = 1;
  state->loc.line = 1;
  m->state = state;

  Nodes nodes = {};
  //Loc ttt = {N_BLOCK, 1, 1};
  state->nodes = (Node2){};
  state->loc = (Loc){N_BLOCK, 1, 1};
  //state->nodes = (Node2){ {{N_BLOCK}, {}, 1}, 0 };
  //state->nodes = (Node2){{N_BLOCK, 1, 1},{0}};
  vector_pushT(nodes.vec, state->nodes);
  state->scope++;

  skipSpaces(state);
  while (state->type != T_EOF) {
    Nodes n = statement(state);
    if (n.data) nodes.data[1].value++;
    push_vector(&nodes.vec, n.vec);
  }
  state->scope--;
  
  const CUPType *type = NULL;
  char tname[256];
  defType(state, nodes.data, &type);
  if (type) {
    cup_type_snname(tname, sizeof(tname), type);
    printf("type = %s\n", tname);
  }
  m->it = nodes.data;
  m->end = (Node*)((char*)m->it + nodes.size);

  //printNodes(m);
  state->input_stream = input_stream;
  state->nodes = ncopy;
  //return;

  map_iter(&state->symmap, state, printVar, sizeof(CVARS), SIZE_MAX);
  //return;

  printf("--\n");
  size_t init = 0;
  codegen_func(m, &init, 0);

  void *page = allocMemory(m->size);
  memcpy(page, m->data, m->size);
  cup_free(m->data);
  
  CRealloc *rptr = m->reallocs.data;
  for (size_t i = m->reallocs.size / sizeof(CRealloc); i--;) {
    CRealloc rr = rptr[i];
    printf("[%ld] %ld %p ", i, rr.ptr, rr.var);
    printf("before = %zx ", *rr.var);
    *rr.var = (size_t)(page + rr.ptr);
    //if (m->data[rr.ptr] == 0x55) {
      //printf("BR: %ld\n", *rr.var);
      //*rr.var = (size_t)(m->data + rr.ptr);
      //printf("AR: %ld\n", *rr.var);
    //}
    //if (m->data[rr.ptr] == 0) {
      //memcpy(m->data + rr.ptr, rr.var, sizeof(size_t));
    //}
    printf("after = %zx\n", *rr.var);
  }
  printf("\n------\n");
  for (size_t i = 0; i < m->size; i++) {
    printf("%02X", ((uint8_t*)page)[i]);
  }
  printf("\n------\n");
  //printf("init = %zx\n", init);

  //symmap_iter(&state->symmap, state, printVar);

  printf("page = %p, init = %p\n", page, (void*)init);
  typedef void (*JitFunc)();
  ((JitFunc)init)();
  
  state->priv_stream = priv_stream;
  state->input_stream = input_stream;
  state->nodes = ncopy;
}

void emit_error(CUPModule *buf, size_t size) {
  size_t capacity = PAGESIZE;
  if (buf == NULL) {
    cup_error("emit_error: buf is NULL");
    exit(1);
  }
  if (buf->data == NULL)
    buf->data = (uint8_t *)cup_malloc(capacity);
  buf->size += size;
  if (buf->size > capacity) {
    do { capacity += PAGESIZE; } while (buf->size > capacity);
    buf->data = (uint8_t *)cup_realloc(buf->data, capacity);
  }
}
void emit(CUPModule *buf, const void *value, size_t size) {
  if (buf == NULL)
    return;
  size_t offset = buf->size;
  emit_error(buf, size);
  memcpy(buf->data + offset, value, size);
}


CUPState *cup_new(int target) {
  (void)target;
  #define DCHART uint16_t
  CUPState* state;
  DCHART* names;
  cup_alloc(state, CUPState);
  cup_alloc(names, DCHART);
  state->names = (uint8_t*)(names + 1);
  //cup_calloc(state, sizeof(CUPState));
  //cup_calloc(state->names, sizeof(uint8_t) * 2);
  //state->names += 2;
  //symmap_init(&state->symmap);
  CVARS vars = {0};
  map_init(&state->symmap, SIZE_MAX, sizeof(CVARS), &vars);
  return state;
}
void cup_delete(CUPState *state) {
  //TODO: chech cup_free all
  if (state == NULL)
    return;
  for (CUPModuleList* m = state->modules; m; m = m->next) {
    //cup_freeMemory(m->code, m->capacity);
  }
  //symmap_free(&state->symmap);
  map_free(&state->symmap);
  //cup_free(state->vars);
  cup_free(state->data);
  cup_free(state);
}
int cup_compile_string(CUPState *state, const char *buf) {
  if (state == NULL || buf == NULL || *buf == '\0')
    return CUP_ERR_ARGUMENTS;
  parse(state, buf, "");
  return 0;
}
int cup_compile_file(CUPState *state, const char *filename) {
 if (state == NULL || filename == NULL || *filename == '\0')
    return CUP_ERR_ARGUMENTS;
  FILE *f = fopen(filename, "rb");
  if (f == NULL)
    return CUP_ERR_FILE_NOT_FOUND;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = (char *)cup_malloc(size + 1);
  buf[size] = '\0';
  fread(buf, size, 1, f);
  fclose(f);
  parse(state, buf, filename);
  cup_free(buf);
  return 0;
}
int cup_add_symbol(CUPState *state, const char *name, void *val, const CUPType type) {
  // TODO: add check safe type
  if (state == NULL || name == NULL || *name == '\0') {
    cup_error("Cannot add symbol(%s) because state == NULL || name == NULL");
    return -1;
  }

  size_t len = strlen(name);
  if (!idToken(state, name, len)) {
    cup_errorf("Cannot add symbol(%s) because it is a keyword", token_names[state->type]);
    return -2;
  }

  CVariable* v = getVarScoped(state, state->nodes.value, 1);
  if (v) {
    cup_errorf("Cannot add symbol(%s) because it already exist", name);
    return -3;
  }
  const CUPType* t = cup_type_put(state, (CUPType*)&type);
  //symmap_put(&state->symmap, state->nodes.value, t, (size_t)val, 0);
  CVARS vars = {(CVariable){t, (size_t)val, 1}};
  map_put(&state->symmap, state->nodes.value, &vars, sizeof(CVARS), SIZE_MAX);
  return 0;
}
CVariable *cup_get_symbol(CUPState *state, const char *name) {
  if (state == NULL)
    return NULL;
  if (name == NULL || *name == '\0')
    return NULL;
  size_t len = strlen(name);
  idToken(state, name, len);
  if (state->nodes.value == SIZE_MAX)
    return NULL;
  return getVarScoped(state, state->nodes.value, CVARS_MAX);
}
void cup_list_symbols(CUPState *state, void *ctx, cup_list_symbols_callback cb) {
  if (state == NULL)
    return;
  if (!state->symmap.slots) return;
  size_t dkv_size = sizeof(CVARS) + sizeof(size_t) + sizeof(size_t);
  for (size_t i = 0; i < state->symmap.capacity; i++) {
    size_t *slot = state->symmap.slots + i * dkv_size;
    CVARS* vars = (CVARS*)(slot + 2);
    if (*slot != SIZE_MAX) {
      cb(ctx, getString(state, *slot), vars->vars[0].value, vars->vars[0].type);
    }
  }
}
