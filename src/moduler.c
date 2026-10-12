#include "../include/cup.h"
// #include <threads.h>
#include "dlfcn.h"

#ifdef _WIN32
#include <windows.h>

const char *get_exe_path(void) {
  static char path[MAX_PATH];
  DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
  if (len == 0 || len == MAX_PATH)
    return NULL;
  path[len] = '\0';
  return path;
}
void *allocMemory(size_t size) {
  void *ptr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (ptr != NULL)
    return ptr;
  WORD err = GetLastError();
  char *msg = NULL;
  FormatMessageA(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    NULL, err, 0, (LPSTR)&msg, 0, NULL
  );
  fprintf(stderr, "VirtualAlloc failed: %lu: %s\n", (unsigned long)err, msg ? msg : "Unknown error");
  exit(1);
  return NULL; // Unreachable, but avoids compiler warning
}
int samefile(const char *a, const char *b) {
  //TODO: windows stuf
  return 0;
}

void emit_error(CUPModule *buf, size_t size) {
  if (buf == NULL) return;
  if (buf->data == NULL) {
    buf->vec.capacity = PAGESIZE;
    buf->data = VirtualAlloc(NULL, size,
      MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!buf->data) {
      DWORD err = GetLastError();
      char msg[512];
      FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM
        | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err, 0, msg, sizeof(msg), NULL
      );
      fprintf(stderr, "mmap failed: %s", msg);
      exit(1);
    }
  }
  buf->size += size;
  if (buf->size <= buf->vec.capacity) return;
  size_t oldcap = buf->vec.capacity;
  do {
    buf->vec.capacity += PAGESIZE;
  } while (buf->size > buf->vec.capacity);
  
  void* old = buf->data;
  buf->data = VirtualAlloc(NULL, buf->vec.capacity,
    MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
  if (!buf->data) {
    DWORD err = GetLastError();
    char msg[512];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM
      | FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, err, 0, msg, sizeof(msg), NULL
    );
    fprintf(stderr, "mmap failed: %s", msg);
    exit(1);
  }
  memcpy(buf->data, old, oldcap);
  VirtualFree(old, 0, MEM_RELEASE);
}

#else
#include <unistd.h> // readlink
//#include <linux/limits.h> // PATH_MAX
#include <sys/stat.h>
#include <sys/mman.h>

const char *get_exe_path(void) {
  static char path[PATH_MAX];
  size_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (len < 0)
    return NULL;
  path[len] = '\0';
  return path;
}
void *allocMemory(size_t size) {
  void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem != MAP_FAILED)
    return mem;
  perror("mmap failed");
  exit(1);
  return NULL; // Unreachable, but avoids compiler warning
}
int samefile(const char *a, const char *b) {
  if (!a || *a == '\0' || !b || *b == '\0')
    return 0;
  struct stat sa, sb;
  if (stat(a, &sa) != 0 || stat(b, &sb) != 0)
    return 0;
  return (sa.st_ino == sb.st_ino) && (sa.st_dev == sb.st_dev);
}

void emit_error(CUPModule *buf, size_t size) {
  if (buf == NULL) return;
  if (buf->data == NULL) {
    buf->vec.capacity = PAGESIZE;
    buf->data = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf->data == MAP_FAILED) {
      perror("mmap failed");
      exit(1);
    }
  }
  buf->size += size;
  if (buf->size <= buf->vec.capacity) return;
  size_t oldcap = buf->vec.capacity;
  do {
    buf->vec.capacity += PAGESIZE;
  } while (buf->size > buf->vec.capacity);
  
  buf->data = mremap(buf->data, oldcap, buf->vec.capacity, MREMAP_MAYMOVE);
}

#endif

void emit(CUPModule *buf, const void *value, size_t size) {
  if (buf == NULL) return;
  size_t offset = buf->size;
  emit_error(buf, size);
  memcpy(buf->data + offset, value, size);
}


int cup_version_major() { return 1; }
int cup_version_minor() { return 12; }
int cup_version() { return cup_version_minor() | (cup_version_major() * 0xFF); }

Nodes statement(CUPState *state);

void importModule(CUPState *state) {
  const char* name = (const char*)getData(state, state->nodes.value);
  // CUPState copy;
  // memcpy(&copy, state, sizeof(CUPState));
  //const char *name = getString(state, state->nodes.value);
  if (cup_compile_file(state, name)) {
    cup_errorf("Load module '%s'", name);
    exit(1);
  }

  // vector_pushT(left, state->nodes);
  // getToken(state);

  // state->input_stream = input_stream;
}

void externalModule(CUPState *state) {
  const char so[] = {'.', 's', 'o'};
  const uint8_t *name = getData(state, state->nodes.value);
  size_t len = strlen((char*)name);
  size_t ext = sizeof(so) + 2;
  char* base = cup_malloc(len + 1 + ext);
  memcpy(base, name, len);
  memset(base + len, 0, 1 + ext);
  if (memcmp(base + len - sizeof(so), so, sizeof(so)) != 0)
    memcpy(base + len, so, sizeof(so));

  static const char *suffixes[] = {"\0\0", ".6", ".0", ".1", ".2"};
  //CExternal external = {0};
  void* external = NULL;
  for (size_t i = 0; i < sizeof(suffixes) / sizeof(*suffixes) && external == NULL; i++) {
    memcpy(base + len + sizeof(so), suffixes[i], 2);
    external = dlopen(base, RTLD_LAZY | RTLD_GLOBAL);
  }
  if (external == NULL) {
    cup_errorf("external: dlopen('%s') failed: %s", base, dlerror());
    exit(1);
  }
  vector_pushT(state->externals.vec, external);
  cup_free(base);
}

void externalSymbol(CUPState *state, const char* name, size_t sig_idx) {
  const uint8_t *sig = getData(state, sig_idx);

  void *fn = NULL;
  size_t count = state->externals.size / sizeof(void*);
  for (size_t i = 0; i < count; i++) {
    dlerror();
    fn = dlsym(state->externals.data[i], name);
    if (fn)
      break;
  }
  if (fn == NULL) {
    cup_errorf("external: symbol '%s' not found in any loaded library", name);
    exit(1);
  }

  const CUPType *type = cup_type_parse(state, (const char *)sig);
  CVariable *v = varLookup(state, name, 1);
  if (v) {
    cup_errorf("Cannot add symbol(%s) because it already exist", name);
    cup_errorf("external: could not register symbol '%s'", name);
    exit(1);
  }
  //CVARS vars = {.vars = {{type, (size_t)fn, 1}}};
  //map_put(&state->symmap, name_idx, &vars, sizeof(CVARS), SIZE_MAX);
  putVar(state, name, type, (size_t)fn, 0);
}

void printInstructions(FILE *out, CUPModule *m) {
  // fprintf(out, "-- module: %zu bytes --\n", m->size);
  for (size_t i = 0; i < m->size; i++)
  {
    fprintf(out, "%02X ", ((uint8_t*)m->data)[i]);
    if ((i + 1) % 16 == 0)
      fprintf(out, "\n");
  }
  if (m->size % 16 != 0)
    fprintf(out, "\n");
  // fprintf(out, "-- end --\n");
}

static void symbol_cb(void *ctx, const char *name, const CUPType *type, size_t value, size_t scope) {
  char tname[256] = {};
  cup_type_snname(tname, sizeof(tname), type);
  fprintf((FILE*)ctx, "%s[%zu], type= %s, value= %zu\n", name, scope, tname, value);
}

static void cup_write_bytecode_f(CUPState *state, const NRange range, FILE* f) {
  fputs("//vars\n", f);
  cup_list_symbols(state, f, symbol_cb);
  fputs("//data\n", f);
  
  for (size_t i = 0; i < state->data_size; i++)
    fputc(*getData(state, i), f);
    //fprintf(f, "%hhx", *getData(state, i));
  fputs("\n//ast\n", f);
  //printNodes(f, range, state);
  for (Node *it = range.it; it != range.end; it++) {
    const char* name = token_names[it->token];
    fprintf(f, "%s[%hu:%hu]", name, it->loc.col, it->loc.line);
    if (it->token == N_BLOCK || it->token == T_NUMBER || it->token == T_COMMA) {
      it++;
      fprintf(f, " - %zu\n", it->value);
    }
    else if (it->token == T_IDENTIFIER) {
      it++;
      fprintf(f, " - %s\n", it->svalue);
    }
    else if (it->token == T_EQ || it->token == T_CALL
      || it->token == T_OSB || it->token == T_AT
      || it->token == T_RETURN || it->token == T_THIS) {
      fputc('\n', f);
    }
    else if (it->token == N_ARRAY) {
      it++;
      fprintf(f, " - data offset: %zu\n", it->value);
    }
    else if (it->token == T_STRING || it->token == T_MSTRING) {
      it++;
      fprintf(f, " - data offset: %zu\n", it->value);
      //fprintf(f, " - \"%s\"\n", getData(state, (++it)->value));
    }
    else
      fprintf(f, "%s[%hu:%hu] as value = %zu\n", name, it->loc.col, it->loc.line, it->value);
  }
  fclose(f);
}

static void cup_write_bytecode(CUPState *state, const NRange range) {
  if (state == NULL)
    return;
  const char *configured = state->bytecode_path;
  const char *filename = configured;
  FILE *f = fopen(filename, "w");
  if (f == NULL) {
    cup_errorf("Could not open bytecode output '%s'", filename);
    return;
  }
  cup_write_bytecode_f(state, range, f);
  state->bytecode_path = NULL;
}

void codegen(CUPState *state, CUPModule *m, Nodes nodes, size_t* init) {
  NRange range = (NRange){nodes.data, (Node *)((char *)nodes.data + nodes.size)};
  CReallocs reallocs = {};
  codegen_func(state, m, &reallocs, &range, init, 0);
  void *page = m->data;
  CRealloc *rptr = reallocs.data;
  for (size_t i = reallocs.size / sizeof(CRealloc); i--;)
  {
    CRealloc rr = rptr[i];
    cup_dbgf("[%ld] %ld %p ", i, rr.ptr, rr.var);
    if (rr.var) {
      *rr.var = (size_t)(page + rr.ptr);
      //cup_dbgf("before = %hhx", *(uint8_t*)*rr.var);
      //cup_dbgf("after = %zx", *rr.var);
    }
    // if (m->data[rr.ptr] == 0x55) {
    // printf("BR: %ld\n", *rr.var);
    //*rr.var = (size_t)(m->data + rr.ptr);
    // printf("AR: %ld\n", *rr.var);
    //}
    // if (m->data[rr.ptr] == 0) {
    // memcpy(m->data + rr.ptr, rr.var, sizeof(size_t));
    //}
  }
  // printf("\n------\n");
  // for (size_t i = 0; i < m->size; i++) {
  //   printf("%02X", ((uint8_t*)page)[i]);
  // }
  // printf("\n------\n");
  // return;
  // printf("init = %zx\n", init);

  
  cup_list_symbols(state, stdout, symbol_cb);
}

CUPModule *parse(CUPState *state, const char *buf, const char *file) {
  for (CUPModuleList *it = state->modules; it; it = it->next)
    if (samefile(it->module.path, file))
      return &it->module;
  CUPModuleList *m;
  cup_calloc(m, CUPModuleList);
  m->module.path = cup_strdup(file);
  m->next = state->modules;
  state->modules = m;

  const char *input_stream = state->input_stream;
  const char *priv_stream = state->priv_stream;
  Node2 ncopy = state->nodes;
  
  //putVar(state, );

  state->priv_stream = NULL;
  state->input_stream = buf;
  state->nodes = (Node2){};
  state->nodes.node.loc = (Loc){N_BLOCK, 1, 1};

  //puts("Nodes");
  Nodes nodes = {};
  vector_pushT(nodes.vec, state->nodes);

  getToken(state, 0);
  while (state->nodes.token != T_EOF) {
    Nodes n = statement(state);
    if (n.data)
      nodes.data[1].value++;
    push_vector(&nodes.vec, n.vec);
  }

  //puts("Nodes2");

  state->scope = 0;
  size_t init = 0;
  //codegen(state, &m->module, nodes, &init);
  //cup_dbgf("page = %p, init = %p", m->module.data, (void *)init);
#if 0
  NRange range = (NRange){nodes.data, (Node *)((char *)nodes.data + nodes.size)};
  if (state->bytecode_path)
    cup_write_bytecode(state, range); // NO -O
#endif

  //puts("Nodes3");
  //state->scope = 0;
  int def = resolve(state, &nodes);
  if (def == 0) {
    state->scope = 0;
    m->module.size = 0;
    codegen(state, &m->module, nodes, &init);
    cup_dbgf("page = %p, init = %p\n", m->module.data, (void *)init);
    //puts("Nodes3.5");
    typedef void *(*JitFunc)();
    void *r = ((JitFunc)init)();
    cup_dbgf("r = %p\n", r);
  } else {
    cup_dbgf("def = 0, page = %p, init = %p", m->module.data, (void *)init);
  }
  //puts("Nodes4");


#if 1
  NRange range = (NRange){nodes.data, (Node *)((char *)nodes.data + nodes.size)};
  if (state->bytecode_path)
    cup_write_bytecode(state, range); // NO -O
#elif 0
  NRange range = (NRange){nodes.data, (Node *)((char *)nodes.data + nodes.size)};
  cup_write_bytecode_f(state, range, stdout); // NO -O
#endif
  cup_free(nodes.data);

  state->priv_stream = priv_stream;
  state->input_stream = input_stream;
  state->nodes = ncopy;
  return &m->module;
}

const char* cup_bytecode_path(CUPState* state, const char* path) {
  if (state == NULL) return NULL;
  if (path)
    state->bytecode_path = path;
  return state->bytecode_path;
}
const char* cup_code_path(CUPState* state, const char* path) {
  if (state == NULL) return NULL;
  if (path)
    state->code_path = path;
  return state->code_path;
}
void cup_write_code(CUPState* state, CUPModule *m) {
  if (state == NULL || m == NULL)
    return;
  const char *configured = cup_code_path(state, NULL);
  char *generated = configured ? NULL : cup_sprintf("%s.bin", m->path);
  const char *filename = configured ? configured : generated;
  FILE *f = fopen(filename, "w");
  if (f == NULL) {
    cup_errorf("Could not open instruction output '%s'", filename);
    return;
  }
  printInstructions(f, m);
  fclose(f);
  cup_free(generated);
}

struct FY {
    /* Source input */
    const char *input_stream;
    const char *priv_stream;

    /* Current parser token / node */
    union {
        Node2 nodes;
        CVariable *variable;
    };
    union {
        struct {
            size_t *data;
            size_t size;
        };
        CVector vec;
    } scopes;
    size_t scope;
};

static_assert(sizeof(struct FY) == 64, "");

static CUP_GEN_RESULT meta_this(CUPGenContext *ctx, CUP_META_OP op, void* value, CUP_META meta) {
  switch (op) {
  case CUP_MOP_MEMBER: { // .
    if (meta.member.len == 9 && memcmp(meta.member.name, "data_size", 9) == 0) {
      size_t offset = offsetof(CUPState, data_size);
      cup_dbgf("data_size offset: %zu", offset);
      cup_gen_address(ctx, (char*)value + offset, &cup_type_int);
      return CUP_GEN_DONE;
    }
    cup_errorf("meta_this: unknown member '%s'", meta.member.name);
    return CUP_GEN_ERROR;
  }
    break;
  case CUP_MOP_BRACK: // []
    cup_error("meta_this: '[]' is not supported");
    break;
  case CUP_MOP_CALL: // (...)
    cup_error("meta_this: '()' is not supported");
    break;
  }
  return CUP_GEN_ERROR;
}

CUPState *cup_new(int target) {
  (void)target;
#define DCHART uint16_t
  CUPState *state;
  DCHART *names;
  cup_calloc(state, CUPState);
  cup_calloc(names, DCHART);

  //state->names = (uint8_t *)(names + 1);
  //CVariable def = {NULL, 0, 0};
  //map_init(&state->symmap, SIZE_MAX, sizeof(def), &def);
  
  size_t null_scope = SIZE_MAX;
  vector_pushT(state->scopes.vec, null_scope);
  size_t parent_to_this_scope = 0;
  vector_pushT(state->scopes.vec, parent_to_this_scope);
  state->scope = 1;

  //const CUPType* charptr = cup_type(state, CUP_TYPE_POINTER, 0, (const void*[]){&cup_type_uint8});
  const CUPType* thistype = cup_typemeta(state, 0, meta_this);
  cup_add_symbol(state, "$", (void*)state, thistype);

  return state;
}
void cup_delete(CUPState *state) {
  if (state == NULL)
    return;

  CUPModuleList *m = state->modules;
  while (m) {
    CUPModuleList *next = m->next;
#ifndef _WIN32
    if (m->module.data && m->module.size)
      munmap(m->module.data, m->module.size);
#endif
    cup_free((void *)m->module.path);
    //cup_free(m->module.reallocs.data);
    //cup_free(m->module.range.it);
    //cup_free(m->module.objrefs.data);
    cup_free(m);
    m = next;
  }

  for (size_t i = 0; i < state->types.size / sizeof(*state->types.data); i++)
    cup_free(state->types.data[i]);
  cup_free(state->types.data);

  for (size_t i = 0; i < state->externals.size / sizeof(*state->externals.data); i++)
    if (state->externals.data[i])
      dlclose(state->externals.data[i]);
  cup_free(state->externals.data);

  // TODO free var names

  cup_free(state->vars_map);
  cup_free(state->vars.data);

  cup_free(state->data);
  cup_free(state);
}

CUPModule *cup_compile_string(CUPState *state, const char *buf)
{
  if (state == NULL || buf == NULL || *buf == '\0')
    return NULL;
  const char *filename = get_exe_path();
  if (filename == NULL)
    return NULL;
  return parse(state, buf, filename);
}
CUPModule *cup_compile_file(CUPState *state, const char *filename) {
  if (state == NULL || filename == NULL || *filename == '\0')
    return NULL;
  FILE *f = fopen(filename, "rb");
  if (f == NULL)
    return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = (char *)cup_malloc(size + 1);
  buf[size] = '\0';
  fread(buf, size, 1, f);
  fclose(f);
  CUPModule *m = parse(state, buf, filename);
  cup_free(buf);
  return m;
}
int cup_add_symbol(CUPState *state, const char *name, void *val, const CUPType* type) {
  if (state == NULL || name == NULL || *name == '\0')
  {
    cup_error("Cannot add symbol(%s) because state == NULL || name == NULL");
    return -1;
  }

  size_t len = strlen(name);
  if (!idToken(state, name, len)) {
    cup_errorf("Cannot add symbol(%s) because it is a keyword", token_names[state->nodes.token]);
    return -2;
  }

  CVariable *v = varLookup(state, state->nodes.svalue, 1);
  if (v) {
    cup_errorf("Cannot add symbol(%s) because it already exist", name);
    return -3;
  }
  putVar(state, state->nodes.svalue, type, (size_t)val, 1);
  return 0;
}
CVariable *cup_get_symbol(CUPState *state, const char *name) {
  if (state == NULL)
    return NULL;
  if (name == NULL || *name == '\0')
    return NULL;
  size_t len = strlen(name);
  if (!idToken(state, name, len)) {
    cup_errorf("Cannot add symbol(%s) because it is a keyword", token_names[state->nodes.token]);
    return NULL;
  }
  CVariable *var = varLookup(state, state->nodes.svalue, 1);
  if (var && var->typee != &cup_type_void)
    return var;
  return NULL;
}
void cup_list_symbols(CUPState *state, void *ctx, cup_list_symbols_callback cb) {
  if (state == NULL)
    return;
  /* vars.size is in bytes */
  size_t count = state->vars.size / sizeof(CVariable);
  for (size_t i = 0; i < count; i++) {
    const CVariable *v = &state->vars.data[i];
    cb(ctx, v->name, v->typee, v->value, v->scope);
  }
}