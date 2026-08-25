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
  CVariable *v = getVars(state, name_idx, 1);
  if (v) {
    cup_errorf("Cannot add symbol(%s) because it already exist", name);
    cup_errorf("external: could not register symbol '%s'", name);
    exit(1);
  }
  //CVARS vars = {.vars = {{type, (size_t)fn, 1}}};
  //map_put(&state->symmap, name_idx, &vars, sizeof(CVARS), SIZE_MAX);
  putVar(state, name_idx, (CVariable){type, (size_t)fn, 0});
}

void printVar(void *ctx, size_t key, size_t dist, const void *v) {
  CUPState *state = (CUPState *)ctx;
  CVariable *var = (CVariable *)v;
  const char *str = getString(state, key);
  char tname[256];
  cup_type_snname(tname, sizeof(tname), var->type);
  printf("Var: [%zu]%s %ld:\n%s %ld %ld\n", key, str, dist, tname, var->value, var->scope);
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
  state->scope = 0;
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

  skipSpaces(state);
  while (state->nodes.token != T_EOF) {
    Nodes n = statement(state);
    if (n.data)
      nodes.data[1].value++;
    push_vector(&nodes.vec, n.vec);
  }

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
  CVariable def = {NULL, 0, 0};
  map_init(&state->symmap, SIZE_MAX, sizeof(CVariable), &def);
  scope_enter(state);
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
int cup_add_symbol(CUPState *state, const char *name, void *val, const CUPType type) {
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

  CVariable *v = getVars(state, state->nodes.value, 1);
  if (v) {
    cup_errorf("Cannot add symbol(%s) because it already exist", name);
    return -3;
  }
  const CUPType *t = cup_type_put(state, type);
  // symmap_put(&state->symmap, state->nodes.value, t, (size_t)val, 0);
  //CVARS vars = {.vars = {{t, (size_t)val, 1}}};
  //map_put(&state->symmap, state->nodes.value, &vars, sizeof(CVARS), SIZE_MAX);
  putVar(state, state->nodes.value, (CVariable){t, (size_t)val, 1});
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
  return getVars(state, state->nodes.value, 1);
}
void cup_list_symbols(CUPState *state, void *ctx, cup_list_symbols_callback cb) {
  if (state == NULL)
    return;
  if (!state->symmap.slots)
    return;
  size_t item_size = offsetof(CMapItem, data) + sizeof(CVariable);
  for (size_t i = 0; i < state->symmap.capacity; i++) {
    CMapItem *item = (CMapItem *)((char*)state->symmap.slots + i * item_size);
    if (item->key == SIZE_MAX) continue;
    cb(ctx, getString(state, item->key), *(const CVariable *)item->data);
  }
}

#ifndef _WIN32__
/* ELF64/x86-64 object output for the current JIT backend.
 *
 * The x64 emitter uses MOVABS with process addresses.  For an object file we
 * translate the addresses we understand into ELF R_X86_64_64 relocations:
 *   - &(CVariable.value) -> .data slot
 *   - state->data        -> .data
 *   - generated code     -> .text
 *   - host/external fn   -> undefined ELF symbol
 *
 * This lets the linker rebase CUP code instead of preserving JIT addresses.
 */
typedef struct CUPObjectModuleInfo {
  CUPModule *module;
  size_t text_offset;
} CUPObjectModuleInfo;

typedef struct CUPObjectVarInfo {
  CVariable *var;
  const char *name;
  size_t data_offset;
} CUPObjectVarInfo;

typedef struct CUPObjectSymbolInfo {
  const char *name;
  CVariable *var;
  size_t text_value;
  int is_defined;
  size_t elf_index;
} CUPObjectSymbolInfo;

typedef struct CUPObjectRelocs {
  Elf64_Rela *data;
  size_t count;
  size_t capacity;
} CUPObjectRelocs;

static size_t cup_obj_align(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

static int cup_obj_rela_push(CUPObjectRelocs *v, Elf64_Rela r) {
  if (v->count == v->capacity) {
    size_t cap = v->capacity ? v->capacity * 2 : 16;
    v->data = cup_realloc(v->data, cap * sizeof(*v->data));
    v->capacity = cap;
  }
  v->data[v->count++] = r;
  return 0;
}

static int cup_obj_text_offset(CUPObjectModuleInfo *mods, size_t mod_count,
                               size_t ptr, size_t *offset) {
  uintptr_t p = (uintptr_t)ptr;
  for (size_t i = 0; i < mod_count; i++) {
    uintptr_t begin = (uintptr_t)mods[i].module->data;
    uintptr_t end = begin + mods[i].module->size;
    if (p >= begin && p < end) {
      *offset = mods[i].text_offset + (size_t)(p - begin);
      return 1;
    }
  }
  return 0;
}

static CUPObjectVarInfo *cup_obj_var_by_address(CUPObjectVarInfo *vars,
                                                size_t var_count, size_t ptr) {
  for (size_t i = 0; i < var_count; i++)
    if ((size_t)&vars[i].var->value == ptr)
      return &vars[i];
  return NULL;
}

static CUPObjectSymbolInfo *cup_obj_external_by_value(CUPObjectSymbolInfo *syms,
                                                       size_t sym_count,
                                                       size_t value) {
  for (size_t i = 0; i < sym_count; i++)
    if (!syms[i].is_defined && syms[i].var->value == value)
      return &syms[i];
  return NULL;
}

/* Resolve an old JIT pointer to an ELF symbol + addend. */
static int cup_obj_target(CUPState *state,
                          CUPObjectModuleInfo *mods, size_t mod_count,
                          CUPObjectVarInfo *vars, size_t var_count,
                          CUPObjectSymbolInfo *syms, size_t sym_count,
                          size_t value, size_t data_payload_offset,
                          size_t text_section_sym, size_t data_section_sym,
                          size_t *sym_index, Elf64_Sxword *addend) {
  CUPObjectVarInfo *vi = cup_obj_var_by_address(vars, var_count, value);
  if (vi) {
    *sym_index = data_section_sym;
    *addend = (Elf64_Sxword)vi->data_offset;
    return 1;
  }

  uintptr_t p = (uintptr_t)value;
  uintptr_t db = (uintptr_t)state->data;
  if (state->data && p >= db && p < db + state->data_size) {
    *sym_index = data_section_sym;
    *addend = (Elf64_Sxword)(data_payload_offset + (size_t)(p - db));
    return 1;
  }

  size_t text_off;
  if (cup_obj_text_offset(mods, mod_count, value, &text_off)) {
    *sym_index = text_section_sym;
    *addend = (Elf64_Sxword)text_off;
    return 1;
  }

  CUPObjectSymbolInfo *ext = cup_obj_external_by_value(syms, sym_count, value);
  if (ext) {
    *sym_index = ext->elf_index;
    *addend = 0;
    return 1;
  }
  return 0;
}

int cup_output_object(CUPState *state, const char *filename) {
  if (!state || !filename || !*filename) {
    cup_error("cup_output_object: invalid state or filename");
    return -1;
  }

  /* Collect generated modules and assign final .text offsets. */
  size_t mod_count = 0;
  for (CUPModuleList *it = state->modules; it; it = it->next)
    if (it->module.data && it->module.size)
      mod_count++;
  if (!mod_count) {
    cup_error("cup_output_object: no generated code");
    return -1;
  }
  CUPObjectModuleInfo *mods = cup_malloc(sizeof(*mods) * mod_count);
  size_t text_size = 0, mi = 0;
  for (CUPModuleList *it = state->modules; it; it = it->next) {
    if (!it->module.data || !it->module.size) continue;
    text_size = cup_obj_align(text_size, 16);
    mods[mi++] = (CUPObjectModuleInfo){&it->module, text_size};
    text_size += it->module.size;
  }

  /* Collect every scoped variable exactly once.  The current JIT ABI stores
   * locals/arguments in CVariable.value, so object mode mirrors those cells in
   * .data and relocates MOVABS references to them. */
  size_t var_count = 0;
  size_t kdv_size = sizeof(size_t) + sizeof(size_t) + sizeof(CVARS);
  for (size_t i = 0; i < state->symmap.capacity; i++) {
    size_t *slot = state->symmap.slots + i * kdv_size;
    if (*slot == SIZE_MAX) continue;
    CVARS *vs = (CVARS *)(slot + 2);
    for (size_t j = 0; j < CVARS_MAX; j++)
      if (vs->vars[j].scope != 0)
        var_count++;
  }
  CUPObjectVarInfo *vars = cup_malloc(sizeof(*vars) * (var_count ? var_count : 1));
  size_t vi = 0;
  size_t data_payload_offset = 0;
  size_t data_var_offset = cup_obj_align(state->data_size, sizeof(size_t));
  for (size_t i = 0; i < state->symmap.capacity; i++) {
    size_t *slot = state->symmap.slots + i * kdv_size;
    if (*slot == SIZE_MAX) continue;
    CVARS *vs = (CVARS *)(slot + 2);
    const char *name = getString(state, *slot);
    for (size_t j = 0; j < CVARS_MAX; j++) {
      if (vs->vars[j].scope == 0) continue;
      vars[vi] = (CUPObjectVarInfo){&vs->vars[j], name,
                                   data_var_offset + vi * sizeof(size_t)};
      vi++;
    }
  }
  size_t data_size = data_var_offset + var_count * sizeof(size_t);

  /* Build the set of ELF-visible function symbols.  Generated CUP functions
   * are definitions; host/dlopen functions are undefined symbols. */
  CUPObjectSymbolInfo *symbols = cup_malloc(sizeof(*symbols) * (var_count ? var_count : 1));
  size_t symbol_count = 0;
  for (size_t i = 0; i < var_count; i++) {
    CVariable *v = vars[i].var;
    if (!v->type || !v->value) continue;
    if (v->type->realtype != CUP_TYPE_FUNCTION &&
        v->type->realtype != CUP_TYPE_GENERATOR)
      continue;
    if (!vars[i].name || !*vars[i].name) continue;

    int duplicate = 0;
    for (size_t j = 0; j < symbol_count; j++)
      if (strcmp(symbols[j].name, vars[i].name) == 0) {
        duplicate = 1;
        break;
      }
    if (duplicate) continue;

    size_t tv = 0;
    int defined = cup_obj_text_offset(mods, mod_count, v->value, &tv);
    symbols[symbol_count++] = (CUPObjectSymbolInfo){vars[i].name, v, tv, defined, 0};
  }

  /* Sections: null, text, data, rela.text, rela.data, symtab, strtab, shstrtab. */
  enum {
    SH_NULL, SH_TEXT, SH_DATA, SH_RELA_TEXT, SH_RELA_DATA,
    SH_SYMTAB, SH_STRTAB, SH_SHSTRTAB, SH_COUNT
  };
  enum {
    SYM_NULL = 0,
    SYM_TEXT_SECTION = 1,
    SYM_DATA_SECTION = 2,
    SYM_FIRST_GLOBAL = 3
  };

  /* String table for global symbols. */
  size_t strtab_size = 1;
  for (size_t i = 0; i < symbol_count; i++)
    strtab_size += strlen(symbols[i].name) + 1;
  char *strtab = cup_malloc(strtab_size);
  size_t *name_offsets = cup_malloc(sizeof(*name_offsets) * (symbol_count ? symbol_count : 1));
  strtab[0] = '\0';
  size_t spos = 1;
  for (size_t i = 0; i < symbol_count; i++) {
    name_offsets[i] = spos;
    size_t n = strlen(symbols[i].name) + 1;
    memcpy(strtab + spos, symbols[i].name, n);
    spos += n;
    symbols[i].elf_index = SYM_FIRST_GLOBAL + i;
  }

  size_t elf_sym_count = SYM_FIRST_GLOBAL + symbol_count;
  Elf64_Sym *symtab = cup_malloc(sizeof(*symtab) * elf_sym_count);
  memset(symtab, 0, sizeof(*symtab) * elf_sym_count);
  symtab[SYM_TEXT_SECTION].st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
  symtab[SYM_TEXT_SECTION].st_shndx = SH_TEXT;
  symtab[SYM_DATA_SECTION].st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
  symtab[SYM_DATA_SECTION].st_shndx = SH_DATA;
  for (size_t i = 0; i < symbol_count; i++) {
    Elf64_Sym *s = &symtab[SYM_FIRST_GLOBAL + i];
    s->st_name = (Elf64_Word)name_offsets[i];
    s->st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    s->st_other = STV_DEFAULT;
    if (symbols[i].is_defined) {
      s->st_shndx = SH_TEXT;
      s->st_value = symbols[i].text_value;
    } else {
      s->st_shndx = SHN_UNDEF;
      s->st_value = 0;
    }
  }

  /* Copy JIT text, then replace relocatable MOVABS immediates with zero and
   * record RELA entries. */
  uint8_t *text = cup_malloc(text_size ? text_size : 1);
  memset(text, 0, text_size);
  for (size_t i = 0; i < mod_count; i++)
    memcpy(text + mods[i].text_offset, mods[i].module->data, mods[i].module->size);

  CUPObjectRelocs rela_text = {0}, rela_data = {0};
  for (size_t m = 0; m < mod_count; m++) {
    size_t begin = mods[m].text_offset;
    size_t end = begin + mods[m].module->size;
    for (size_t p = begin; p + 10 <= end; p++) {
      /* emit_mov_reg_imm64 emits: 48/49 B8+reg imm64 */
      if ((text[p] != 0x48 && text[p] != 0x49) ||
          text[p + 1] < 0xB8 || text[p + 1] > 0xBF)
        continue;
      uint64_t imm;
      memcpy(&imm, text + p + 2, sizeof(imm));
      size_t target_sym;
      Elf64_Sxword addend;
      if (cup_obj_target(state, mods, mod_count, vars, var_count,
                         symbols, symbol_count, (size_t)imm,
                         data_payload_offset, SYM_TEXT_SECTION, SYM_DATA_SECTION,
                         &target_sym, &addend)) {
        Elf64_Rela r = {0};
        r.r_offset = p + 2;
        r.r_info = ELF64_R_INFO(target_sym, R_X86_64_64);
        r.r_addend = addend;
        cup_obj_rela_push(&rela_text, r);
        memset(text + p + 2, 0, sizeof(uint64_t));
      }
      p += 9;
    }
  }

  /* Snapshot compiler data and CVariable.value cells into .data. */
  uint8_t *data = cup_malloc(data_size ? data_size : 1);
  memset(data, 0, data_size);
  if (state->data_size)
    memcpy(data + data_payload_offset, state->data, state->data_size);
  for (size_t i = 0; i < var_count; i++) {
    uint64_t value = (uint64_t)vars[i].var->value;
    memcpy(data + vars[i].data_offset, &value, sizeof(value));
    size_t target_sym;
    Elf64_Sxword addend;
    if (value && cup_obj_target(state, mods, mod_count, vars, var_count,
                                symbols, symbol_count, (size_t)value,
                                data_payload_offset, SYM_TEXT_SECTION, SYM_DATA_SECTION,
                                &target_sym, &addend)) {
      Elf64_Rela r = {0};
      r.r_offset = vars[i].data_offset;
      r.r_info = ELF64_R_INFO(target_sym, R_X86_64_64);
      r.r_addend = addend;
      cup_obj_rela_push(&rela_data, r);
      memset(data + vars[i].data_offset, 0, sizeof(uint64_t));
    }
  }

  const char shstrtab[] =
      "\0.text\0.data\0.rela.text\0.rela.data\0.symtab\0.strtab\0.shstrtab\0";
  enum {
    SHNAME_TEXT = 1,
    SHNAME_DATA = 7,
    SHNAME_RELA_TEXT = 13,
    SHNAME_RELA_DATA = 24,
    SHNAME_SYMTAB = 35,
    SHNAME_STRTAB = 43,
    SHNAME_SHSTRTAB = 51
  };

  size_t off = sizeof(Elf64_Ehdr);
  off = cup_obj_align(off, 16); size_t text_off = off; off += text_size;
  off = cup_obj_align(off, 8);  size_t data_off = off; off += data_size;
  off = cup_obj_align(off, 8);  size_t rela_text_off = off;
  off += rela_text.count * sizeof(Elf64_Rela);
  off = cup_obj_align(off, 8);  size_t rela_data_off = off;
  off += rela_data.count * sizeof(Elf64_Rela);
  off = cup_obj_align(off, 8);  size_t symtab_off = off;
  off += elf_sym_count * sizeof(Elf64_Sym);
  size_t strtab_off = off; off += strtab_size;
  size_t shstrtab_off = off; off += sizeof(shstrtab);
  off = cup_obj_align(off, 8); size_t shoff = off;

  Elf64_Ehdr eh = {0};
  memcpy(eh.e_ident, ELFMAG, SELFMAG);
  eh.e_ident[EI_CLASS] = ELFCLASS64;
  eh.e_ident[EI_DATA] = ELFDATA2LSB;
  eh.e_ident[EI_VERSION] = EV_CURRENT;
  eh.e_ident[EI_OSABI] = ELFOSABI_SYSV;
  eh.e_type = ET_REL;
  eh.e_machine = EM_X86_64;
  eh.e_version = EV_CURRENT;
  eh.e_ehsize = sizeof(eh);
  eh.e_shoff = shoff;
  eh.e_shentsize = sizeof(Elf64_Shdr);
  eh.e_shnum = SH_COUNT;
  eh.e_shstrndx = SH_SHSTRTAB;

  Elf64_Shdr sh[SH_COUNT];
  memset(sh, 0, sizeof(sh));
#define SHSTR(name) SHNAME_##name
  sh[SH_TEXT] = (Elf64_Shdr){
      .sh_name=SHSTR(TEXT), .sh_type=SHT_PROGBITS,
      .sh_flags=SHF_ALLOC|SHF_EXECINSTR, .sh_offset=text_off,
      .sh_size=text_size, .sh_addralign=16};
  sh[SH_DATA] = (Elf64_Shdr){
      .sh_name=SHSTR(DATA), .sh_type=SHT_PROGBITS,
      .sh_flags=SHF_ALLOC|SHF_WRITE, .sh_offset=data_off,
      .sh_size=data_size, .sh_addralign=8};
  sh[SH_RELA_TEXT] = (Elf64_Shdr){
      .sh_name=SHSTR(RELA_TEXT), .sh_type=SHT_RELA,
      .sh_offset=rela_text_off, .sh_size=rela_text.count*sizeof(Elf64_Rela),
      .sh_link=SH_SYMTAB, .sh_info=SH_TEXT, .sh_addralign=8,
      .sh_entsize=sizeof(Elf64_Rela)};
  sh[SH_RELA_DATA] = (Elf64_Shdr){
      .sh_name=SHSTR(RELA_DATA), .sh_type=SHT_RELA,
      .sh_offset=rela_data_off, .sh_size=rela_data.count*sizeof(Elf64_Rela),
      .sh_link=SH_SYMTAB, .sh_info=SH_DATA, .sh_addralign=8,
      .sh_entsize=sizeof(Elf64_Rela)};
  sh[SH_SYMTAB] = (Elf64_Shdr){
      .sh_name=SHSTR(SYMTAB), .sh_type=SHT_SYMTAB,
      .sh_offset=symtab_off, .sh_size=elf_sym_count*sizeof(Elf64_Sym),
      .sh_link=SH_STRTAB, .sh_info=SYM_FIRST_GLOBAL, .sh_addralign=8,
      .sh_entsize=sizeof(Elf64_Sym)};
  sh[SH_STRTAB] = (Elf64_Shdr){
      .sh_name=SHSTR(STRTAB), .sh_type=SHT_STRTAB,
      .sh_offset=strtab_off, .sh_size=strtab_size, .sh_addralign=1};
  sh[SH_SHSTRTAB] = (Elf64_Shdr){
      .sh_name=SHSTR(SHSTRTAB), .sh_type=SHT_STRTAB,
      .sh_offset=shstrtab_off, .sh_size=sizeof(shstrtab), .sh_addralign=1};
#undef SHSTR

  FILE *f = fopen(filename, "wb");
  if (!f) {
    cup_errorf("cup_output_object: could not open '%s'", filename);
    goto fail;
  }
#define PAD_TO(target) do { \
    long _p = ftell(f); \
    while (_p >= 0 && (size_t)_p < (target)) { fputc(0, f); _p++; } \
  } while (0)
  fwrite(&eh, sizeof(eh), 1, f);
  PAD_TO(text_off); fwrite(text, 1, text_size, f);
  PAD_TO(data_off); fwrite(data, 1, data_size, f);
  PAD_TO(rela_text_off); fwrite(rela_text.data, sizeof(Elf64_Rela), rela_text.count, f);
  PAD_TO(rela_data_off); fwrite(rela_data.data, sizeof(Elf64_Rela), rela_data.count, f);
  PAD_TO(symtab_off); fwrite(symtab, sizeof(Elf64_Sym), elf_sym_count, f);
  PAD_TO(strtab_off); fwrite(strtab, 1, strtab_size, f);
  PAD_TO(shstrtab_off); fwrite(shstrtab, 1, sizeof(shstrtab), f);
  PAD_TO(shoff); fwrite(sh, sizeof(Elf64_Shdr), SH_COUNT, f);
#undef PAD_TO
  if (ferror(f)) {
    fclose(f);
    cup_errorf("cup_output_object: failed writing '%s'", filename);
    goto fail;
  }
  fclose(f);

  cup_free(rela_data.data); cup_free(rela_text.data);
  cup_free(data); cup_free(text); cup_free(symtab); cup_free(name_offsets);
  cup_free(strtab); cup_free(symbols); cup_free(vars); cup_free(mods);
  return 0;

fail:
  cup_free(rela_data.data); cup_free(rela_text.data);
  cup_free(data); cup_free(text); cup_free(symtab); cup_free(name_offsets);
  cup_free(strtab); cup_free(symbols); cup_free(vars); cup_free(mods);
  return -1;
}
#else
int cup_output_object(CUPState *state, const char *filename) {
  (void)state; (void)filename;
  cup_error("cup_output_object: ELF object output is only implemented on Linux/ELF");
  return -1;
}
#endif