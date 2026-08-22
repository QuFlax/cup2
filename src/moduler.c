#include "../include/cup.h"
// #include <threads.h>

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

#else
#include <dlfcn.h>
#include <unistd.h>

#include <linux/limits.h>
#include <sys/mman.h>
#include <sys/stat.h>

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

#endif

int cup_version_major() { return 1; }
int cup_version_minor() { return 12; }
int cup_version() { return cup_version_minor() | (cup_version_major() * 0xFF); }


Nodes statement(CUPState *state);

void importModule(CUPState *state) {
  // CUPState copy;
  // memcpy(&copy, state, sizeof(CUPState));
  const char *name = getString(state, state->nodes.value);
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

void externalSymbol(CUPState *state, size_t name_idx, size_t sig_idx) {
  const char *name = getString(state, name_idx);
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
  CVariable *v = getVarScoped(state, state->nodes.value, SIZE_MAX);
  if (v)
  {
    cup_errorf("Cannot add symbol(%s) because it already exist", name);
    cup_errorf("external: could not register symbol '%s'", name);
    exit(1);
    // return -3;
  }
  // symmap_put(&state->symmap, state->nodes.value, t, (size_t)val, 0);
  CVARS vars = {.vars = {{type, (size_t)fn, 1}}};
  map_put(&state->symmap, name_idx, &vars, sizeof(CVARS), SIZE_MAX);
}

void printVar(void *ctx, size_t key, size_t dist, const void *v)
{
  CUPState *state = (CUPState *)ctx;
  CVARS *vars = (CVARS *)v;
  const char *str = getString(state, key);
  char tname[256];
  cup_type_snname(tname, sizeof(tname), vars->vars[0].type);
  printf("Var: [%zu]%s %ld:\n%s %ld %ld\n", key, str, dist, tname, vars->vars[0].value, vars->vars[0].scope);
  cup_type_snname(tname, sizeof(tname), vars->vars[1].type);
  printf("%s %ld %ld\n", tname, vars->vars[1].value, vars->vars[1].scope);
  // cup_type_snname(tname, sizeof(tname), v->type);
  // printf("Var: [%i]%s type= %s, ptr= %ld, %p, scope= %ld\n",
  //           0, "str", tname, v->value, (void*)v->value, v->scope);
}

void printNodes(FILE *out, const NRange range, CUPState *state) {
  for (Node *it = range.it; it != range.end; it++) {
    const char* name = token_names[it->token];
    if (it->token == N_BLOCK || it->token == T_NUMBER || it->token == T_COMMA)
      fprintf(out, "%s - %ld\n", name, (++it)->value);
    else if (it->token == T_IDENTIFIER)
      fprintf(out, "%s - %s\n", name, getString(state, (++it)->value));
    else if (it->token == T_EQ || it->token == T_CALL)
      fprintf(out, "%s\n", name);
    else if (it->token == T_STRING || it->token == T_MSTRING)
      fprintf(out, "%s - \"%s\"\n", name, getData(state, (++it)->value));
    else
      fprintf(out, "%s (%ld)\n", name, it->value);
  }
}
void printInstructions(FILE *out, CUPModule *m)
{
  // fprintf(out, "-- module: %zu bytes --\n", m->size);
  for (size_t i = 0; i < m->size; i++)
  {
    fprintf(out, "%02X ", m->data[i]);
    if ((i + 1) % 16 == 0)
      fprintf(out, "\n");
  }
  if (m->size % 16 != 0)
    fprintf(out, "\n");
  // fprintf(out, "-- end --\n");
}

void codegen(CUPState *state, CUPModule *m, const NRange range) {
  size_t init = 0;
  //const NRange range = state->range;
  state->vector_ = (CVector){};
  codegen_func(state, m, &init, 0);
  m->range = range;
  void *page = allocMemory(m->size);
  memcpy(page, m->data, m->size);
  cup_free(m->data);
  m->data = page;

  CRealloc *rptr = m->reallocs.data;
  for (size_t i = m->reallocs.size / sizeof(CRealloc); i--;)
  {
    CRealloc rr = rptr[i];
    printf("[%ld] %ld %p ", i, rr.ptr, rr.var);
    if (rr.var) {
      *rr.var = (size_t)(page + rr.ptr);
      printf("before = %hhx", *(uint8_t*)*rr.var);
      printf("after = %zx\n", *rr.var);
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

  // symmap_iter(&state->symmap, state, printVar);

  printf("page = %p, init = %p\n", page, (void *)init);
  typedef void *(*JitFunc)();
  void *r = ((JitFunc)init)();
  printf("r = %p\n", r);
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

  state->priv_stream = NULL;
  state->input_stream = buf;
  state->nodes = (Node2){};
  state->nodes.node.loc = (Loc){N_BLOCK, 1, 1};

  Nodes nodes = {};
  vector_pushT(nodes.vec, state->nodes);

  state->scope++;
  skipSpaces(state);
  while (state->nodes.token != T_EOF) {
    Nodes n = statement(state);
    if (n.data)
      nodes.data[1].value++;
    push_vector(&nodes.vec, n.vec);
  }
  //state->scope--;

  m->module.range = (NRange){nodes.data, (Node *)((char *)nodes.data + nodes.size)};
  if (defType(state, &nodes) == 0) {
    NRange nr = (NRange){nodes.data, (Node *)((char *)nodes.data + nodes.size)};
    codegen(state, &m->module, nr);
  }

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
void cup_write_bytecode(CUPState *state, CUPModule *m) {
  if (state == NULL || m == NULL)
    return;
  const char *configured = cup_bytecode_path(state, NULL);
  char *generated = configured ? NULL : cup_sprintf("%s.bytecode", m->path);
  const char *filename = configured ? configured : generated;
  FILE *f = fopen(filename, "w");
  if (f == NULL) {
    cup_errorf("Could not open bytecode output '%s'", filename);
    return;
  }
  size_t i = 0;
  while (1) {
    const char* t = getString(state, i);
    if (t == NULL || *t == '\0') break;
    fprintf(f, "[%ld] %s\n", i++, t);
  }
  for (i = 0; i < state->data_size; i++)
    fputc(*getData(state, i), f);
    //fprintf(f, "%hhx", *getData(state, i));
  fputc('\n', f);
  printNodes(f, m->range, state);
  fclose(f);
  cup_free(generated);
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

CUPState *cup_new(int target)
{
  (void)target;
#define DCHART uint16_t
  CUPState *state;
  DCHART *names;
  cup_calloc(state, CUPState);
  cup_calloc(names, DCHART);
  state->names = (uint8_t *)(names + 1);
  // cup_calloc(state, sizeof(CUPState));
  // cup_calloc(state->names, sizeof(uint8_t) * 2);
  // state->names += 2;
  // symmap_init(&state->symmap);
  CVARS vars = {0};
  map_init(&state->symmap, SIZE_MAX, sizeof(CVARS), &vars);
  return state;
}
void cup_delete(CUPState *state)
{
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
    cup_free(m->module.reallocs.data);
    cup_free(m->module.range.it);
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

  if (state->names) {
    uint8_t *base = state->names - 1;
    while (!(base[-1] == '\0' && *base == '\0'))
      base--;
    cup_free(base - 1);
  }

  map_free(&state->symmap);
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
CUPModule *cup_compile_file(CUPState *state, const char *filename)
{
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
int cup_add_symbol(CUPState *state, const char *name, void *val, const CUPType type)
{
  if (state == NULL || name == NULL || *name == '\0')
  {
    cup_error("Cannot add symbol(%s) because state == NULL || name == NULL");
    return -1;
  }

  size_t len = strlen(name);
  if (!idToken(state, name, len))
  {
    cup_errorf("Cannot add symbol(%s) because it is a keyword", token_names[state->nodes.token]);
    return -2;
  }

  CVariable *v = getVarScoped(state, state->nodes.value, SIZE_MAX);
  if (v)
  {
    cup_errorf("Cannot add symbol(%s) because it already exist", name);
    return -3;
  }
  const CUPType *t = cup_type_put(state, type);
  // symmap_put(&state->symmap, state->nodes.value, t, (size_t)val, 0);
  CVARS vars = {.vars = {{t, (size_t)val, 1}}};
  map_put(&state->symmap, state->nodes.value, &vars, sizeof(CVARS), SIZE_MAX);
  return 0;
}
CVariable *cup_get_symbol(CUPState *state, const char *name)
{
  if (state == NULL)
    return NULL;
  if (name == NULL || *name == '\0')
    return NULL;
  size_t len = strlen(name);
  idToken(state, name, len);
  if (state->nodes.value == SIZE_MAX)
    return NULL;
  return getVarScoped(state, state->nodes.value, SIZE_MAX);
}

void cup_list_symbols(CUPState *state, void *ctx, cup_list_symbols_callback cb)
{
  if (state == NULL)
    return;
  if (!state->symmap.slots)
    return;
  //struct LLLT l = {state, cb, ctx};
  //map_iter(&state->symmap, &l, lll, sizeof(CVARS), SIZE_MAX);
  size_t kdv_size = sizeof(size_t) + sizeof(size_t) + sizeof(CVARS);
  for (size_t i = 0; i < state->symmap.capacity; i++) {
    size_t *slot = state->symmap.slots + i * kdv_size;
    if (*slot == SIZE_MAX) continue;
    //lll(ctx, *slot, slot[1], (void*)(slot + 2));
    CVariable var = ((CVARS *)(slot + 2))->vars[0];
    cb(ctx, getString(state, slot[1]), var.value, var.type);
  }
}