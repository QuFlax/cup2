/*
 * lielf.h - tiny LIEF-like ELF64 object model (single header, ELF64 LE only)
 *
 *   "Library to Instrument ELF" - a small slice of what LIEF does:
 *   load an ELF64 binary into an in-memory object model, mutate it
 *   (sections, symbols, relocations, segments, entry point), and
 *   rebuild a new binary from the model.
 *
 * USAGE
 * -----
 *   #include "lielf.h"          // in every file that needs the API
 *
 *   // exactly ONE translation unit must additionally do:
 *   #define LIELF_IMPLEMENTATION
 *   #include "lielf.h"
 *
 * SCOPE / LIMITATIONS (v1)
 * -------------------------
 *   - ELF64, little-endian, x86_64 only (EI_CLASS=2, EI_DATA=1).
 *   - ET_REL (object files): fully rebuilt from the object model on
 *     write - sections, .symtab/.strtab/.shstrtab, .rela.* are all
 *     regenerated, so add/remove/resize is completely safe.
 *   - ET_EXEC/ET_DYN (executables/shared objects): on write, every
 *     section that was loaded with SHF_ALLOC and was NOT resized
 *     keeps its original file offset / virtual address / bytes
 *     (so existing code/data and their absolute addresses never
 *     move). New or resized SHF_ALLOC sections are appended in a
 *     freshly-mapped region with a synthesized PT_LOAD segment.
 *     All non-ALLOC sections (.symtab, .strtab, .shstrtab, .dynsym,
 *     .dynstr, .rela.*, .dynamic, ...) are regenerated and appended
 *     in a metadata block at the end.
 *   - Binaries that rely on PT_INTERP / PT_DYNAMIC for dynamic
 *     linking can be *loaded and inspected* fully, but rebuilding
 *     them is not guaranteed to remain dynamically-linkable in v1
 *     (the dynamic linker info is preserved verbatim but pointers
 *     into a relocated program header table are not patched).
 *     Statically-linked / freestanding binaries (the common case
 *     for JIT-emitted executables) round-trip and re-execute fine.
 *   - PE support is a possible future addition; this header only
 *     deals with ELF.
 */

#ifndef LIELF_H
#define LIELF_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 *  Raw ELF64 structures (kept self-contained: no dependency on the
 *  system <elf.h>, all names prefixed with LIELF_ / lielf_raw_).
 * ====================================================================== */

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} lielf_raw_ehdr_t; /* 64 bytes */

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} lielf_raw_shdr_t; /* 64 bytes */

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} lielf_raw_phdr_t; /* 56 bytes */

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} lielf_raw_sym_t; /* 24 bytes */

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} lielf_raw_rela_t; /* 24 bytes */

typedef struct {
    int64_t  d_tag;
    uint64_t d_val;
} lielf_raw_dyn_t; /* 16 bytes */

/* ---- e_ident ---- */
#define LIELF_EI_MAG0       0
#define LIELF_EI_MAG1       1
#define LIELF_EI_MAG2       2
#define LIELF_EI_MAG3       3
#define LIELF_EI_CLASS      4
#define LIELF_EI_DATA       5
#define LIELF_EI_VERSION    6
#define LIELF_EI_OSABI      7
#define LIELF_ELFCLASS64    2
#define LIELF_ELFDATA2LSB   1
#define LIELF_EV_CURRENT    1
#define LIELF_ELFOSABI_NONE 0
#define LIELF_ELFOSABI_LINUX 3

/* ---- e_type ---- */
#define LIELF_ET_NONE   0
#define LIELF_ET_REL    1
#define LIELF_ET_EXEC   2
#define LIELF_ET_DYN    3
#define LIELF_ET_CORE   4

/* ---- e_machine ---- */
#define LIELF_EM_X86_64 62

/* ---- sh_type ---- */
#define LIELF_SHT_NULL          0
#define LIELF_SHT_PROGBITS      1
#define LIELF_SHT_SYMTAB        2
#define LIELF_SHT_STRTAB        3
#define LIELF_SHT_RELA          4
#define LIELF_SHT_HASH          5
#define LIELF_SHT_DYNAMIC       6
#define LIELF_SHT_NOTE          7
#define LIELF_SHT_NOBITS        8
#define LIELF_SHT_REL           9
#define LIELF_SHT_DYNSYM        11
#define LIELF_SHT_INIT_ARRAY    14
#define LIELF_SHT_FINI_ARRAY    15

/* ---- sh_flags ---- */
#define LIELF_SHF_WRITE     0x1
#define LIELF_SHF_ALLOC     0x2
#define LIELF_SHF_EXECINSTR 0x4
#define LIELF_SHF_MERGE     0x10
#define LIELF_SHF_STRINGS   0x20
#define LIELF_SHF_INFO_LINK 0x40
#define LIELF_SHF_TLS       0x400

/* ---- p_type ---- */
#define LIELF_PT_NULL       0
#define LIELF_PT_LOAD       1
#define LIELF_PT_DYNAMIC    2
#define LIELF_PT_INTERP     3
#define LIELF_PT_NOTE       4
#define LIELF_PT_SHLIB      5
#define LIELF_PT_PHDR       6
#define LIELF_PT_TLS        7
#define LIELF_PT_GNU_EH_FRAME 0x6474e550
#define LIELF_PT_GNU_STACK  0x6474e551
#define LIELF_PT_GNU_RELRO  0x6474e552

/* ---- p_flags ---- */
#define LIELF_PF_X 0x1
#define LIELF_PF_W 0x2
#define LIELF_PF_R 0x4

/* ---- st_info: bind/type ---- */
#define LIELF_STB_LOCAL  0
#define LIELF_STB_GLOBAL 1
#define LIELF_STB_WEAK   2

#define LIELF_STT_NOTYPE  0
#define LIELF_STT_OBJECT  1
#define LIELF_STT_FUNC    2
#define LIELF_STT_SECTION 3
#define LIELF_STT_FILE    4

#define LIELF_ST_INFO(bind, type) (((bind) << 4) | ((type) & 0xf))
#define LIELF_ST_BIND(info)       ((info) >> 4)
#define LIELF_ST_TYPE(info)       ((info) & 0xf)

/* ---- special section indices ---- */
#define LIELF_SHN_UNDEF  0
#define LIELF_SHN_ABS    0xfff1
#define LIELF_SHN_COMMON 0xfff2

/* ---- common x86_64 relocation types ---- */
#define LIELF_R_X86_64_NONE      0
#define LIELF_R_X86_64_64        1
#define LIELF_R_X86_64_PC32      2
#define LIELF_R_X86_64_GOT32     3
#define LIELF_R_X86_64_PLT32     4
#define LIELF_R_X86_64_COPY      5
#define LIELF_R_X86_64_GLOB_DAT  6
#define LIELF_R_X86_64_JUMP_SLOT 7
#define LIELF_R_X86_64_RELATIVE  8
#define LIELF_R_X86_64_GOTPCREL  9
#define LIELF_R_X86_64_32        10
#define LIELF_R_X86_64_32S       11
#define LIELF_R_X86_64_PC64      24

/* ---- a subset of DT_* dynamic tags ---- */
#define LIELF_DT_NULL     0
#define LIELF_DT_NEEDED   1
#define LIELF_DT_PLTRELSZ 2
#define LIELF_DT_PLTGOT   3
#define LIELF_DT_HASH     4
#define LIELF_DT_STRTAB   5
#define LIELF_DT_SYMTAB   6
#define LIELF_DT_RELA     7
#define LIELF_DT_RELASZ   8
#define LIELF_DT_RELAENT  9
#define LIELF_DT_STRSZ    10
#define LIELF_DT_SYMENT   11
#define LIELF_DT_INIT     12
#define LIELF_DT_FINI     13
#define LIELF_DT_SONAME   14
#define LIELF_DT_RPATH    15
#define LIELF_DT_DEBUG    21
#define LIELF_DT_JMPREL   23
#define LIELF_DT_FLAGS    30

/* default load base used when synthesising PT_LOAD segments for a
 * freshly created (lielf_new) ET_EXEC binary; overridable via
 * bin->base_vaddr before the first lielf_build()/lielf_write() */
#define LIELF_DEFAULT_BASE_EXEC 0x400000ULL
#define LIELF_DEFAULT_BASE_DYN  0x0ULL
#define LIELF_PAGE_SIZE         0x1000ULL

/* ======================================================================
 *  Object model
 * ====================================================================== */

/* lielf_section_t.kind - tells lielf_build() how to (re)generate the
 * bytes for this section. LIELF_SEC_NORMAL sections are written
 * verbatim from `data`; every other kind is fully regenerated from
 * the structured arrays in lielf_binary_t (symbols/relocs/dynamic). */
typedef enum {
    LIELF_SEC_NORMAL = 0,
    LIELF_SEC_SHSTRTAB,
    LIELF_SEC_SYMTAB,
    LIELF_SEC_STRTAB,
    LIELF_SEC_DYNSYM,
    LIELF_SEC_DYNSTR,
    LIELF_SEC_RELA,
    LIELF_SEC_DYNAMIC
} lielf_sec_kind_t;

typedef struct {
    char    *name;
    uint32_t type;       /* SHT_* */
    uint64_t flags;      /* SHF_* */
    uint64_t addr;
    uint64_t offset;     /* informative after load; recomputed on build */
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t addralign;
    uint64_t entsize;
    uint8_t *data;       /* owned heap buffer, `size` bytes (NULL for NOBITS/size==0) */

    lielf_sec_kind_t kind;

    /* --- internal bookkeeping, do not set directly --- */
    int      _loaded_alloc; /* was SHF_ALLOC and present in the file we loaded */
    int      _dirty;        /* size/data changed (or brand new) since load   */
    int      _rela_dynamic; /* for LIELF_SEC_RELA: which reloc group (see lielf_add_reloc) */
    char    *_rela_target;  /* for LIELF_SEC_RELA: target section name       */
} lielf_section_t;

typedef struct {
    uint32_t type;   /* PT_* */
    uint32_t flags;  /* PF_* */
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} lielf_segment_t;

typedef struct {
    char    *name;
    uint64_t value;
    uint64_t size;
    uint8_t  info;   /* LIELF_ST_INFO(bind,type) */
    uint8_t  other;
    uint16_t shndx;
} lielf_symbol_t;

typedef struct {
    char    *target_section; /* section the relocation applies to, e.g. ".text" */
    uint64_t offset;          /* offset within target_section */
    uint32_t sym;             /* index into symbols[] (dynamic==0) or dynsymbols[] (dynamic==1) */
    uint32_t type;            /* R_X86_64_* */
    int64_t  addend;
    int      dynamic;         /* 0 -> .rela<target_section> + .symtab, 1 -> .rela.dyn + .dynsym */
} lielf_reloc_t;

typedef struct {
    int64_t  tag;  /* DT_* */
    uint64_t val;
} lielf_dynentry_t;

typedef struct {
    lielf_raw_ehdr_t ehdr;

    lielf_section_t  *sections;    size_t nsections,    capsections;
    lielf_segment_t  *segments;    size_t nsegments,    capsegments;
    lielf_symbol_t   *symbols;     size_t nsymbols,     capsymbols;     /* .symtab (index 0 = STN_UNDEF) */
    lielf_symbol_t   *dynsymbols;  size_t ndynsymbols,  capdynsymbols;  /* .dynsym (index 0 = STN_UNDEF) */
    lielf_reloc_t    *relocs;      size_t nrelocs,      caprelocs;
    lielf_dynentry_t *dynamic;     size_t ndynamic,     capdynamic;

    uint64_t base_vaddr; /* load base used for newly appended ALLOC sections */
} lielf_binary_t;

/* ======================================================================
 *  Public API
 * ====================================================================== */

/* --- lifecycle --- */
lielf_binary_t *lielf_new(uint16_t e_type, uint16_t e_machine);
lielf_binary_t *lielf_load_file(const char *path);
lielf_binary_t *lielf_load_mem(const uint8_t *buf, size_t size);
void            lielf_free(lielf_binary_t *bin);

/* --- sections --- */
lielf_section_t *lielf_get_section(lielf_binary_t *bin, const char *name);
lielf_section_t *lielf_add_section(lielf_binary_t *bin, const char *name,
                                    uint32_t type, uint64_t flags,
                                    const uint8_t *data, uint64_t size,
                                    uint64_t addralign);
int lielf_remove_section(lielf_binary_t *bin, const char *name);
int lielf_set_section_data(lielf_binary_t *bin, lielf_section_t *sec,
                            const uint8_t *data, uint64_t size);

/* --- segments --- */
lielf_segment_t *lielf_add_segment(lielf_binary_t *bin, uint32_t type, uint32_t flags,
                                    uint64_t vaddr, uint64_t filesz, uint64_t memsz,
                                    uint64_t align);

/* --- symbols (.symtab, or .dynsym when dynamic!=0) --- */
lielf_symbol_t *lielf_add_symbol(lielf_binary_t *bin, const char *name,
                                  uint64_t value, uint64_t size,
                                  uint8_t info, uint16_t shndx, int dynamic);
lielf_symbol_t *lielf_find_symbol(lielf_binary_t *bin, const char *name, int dynamic);
int             lielf_symbol_index(lielf_binary_t *bin, lielf_symbol_t *sym, int dynamic);

/* --- relocations --- */
lielf_reloc_t *lielf_add_reloc(lielf_binary_t *bin, const char *target_section,
                                uint64_t offset, uint32_t sym_index, uint32_t type,
                                int64_t addend, int dynamic);

/* --- dynamic section --- */
lielf_dynentry_t *lielf_get_dynamic(lielf_binary_t *bin, int64_t tag);
lielf_dynentry_t *lielf_add_dynamic(lielf_binary_t *bin, int64_t tag, uint64_t val);

/* --- entry point --- */
uint64_t lielf_get_entry(lielf_binary_t *bin);
void     lielf_set_entry(lielf_binary_t *bin, uint64_t entry);

/* --- build / write ---
 * lielf_build() returns a heap buffer (caller frees) of *out_size bytes
 * containing the rebuilt ELF image. lielf_write() builds and writes it
 * to `path` (mode 0755). Both return NULL/non-zero on error. */
uint8_t *lielf_build(lielf_binary_t *bin, size_t *out_size);
int      lielf_write(lielf_binary_t *bin, const char *path);

/* --- introspection --- */
void lielf_dump(lielf_binary_t *bin, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* LIELF_H */

/* ======================================================================
 *  Implementation
 * ====================================================================== */

#define LIELF_IMPLEMENTATION
 
#ifdef LIELF_IMPLEMENTATION
#ifndef LIELF_IMPLEMENTATION_INCLUDED
#define LIELF_IMPLEMENTATION_INCLUDED

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------
 *  small helpers
 * --------------------------------------------------------------------- */
#define LIELF__ENSURE(arr, n, cap, T) \
    do { \
        if ((n) >= (cap)) { \
            size_t _newcap = (cap) ? (cap) * 2 : 4; \
            T *_tmp = (T *)realloc((arr), _newcap * sizeof(T)); \
            if (!_tmp) { return NULL; } \
            (arr) = _tmp; \
            (cap) = _newcap; \
        } \
    } while (0)

static char *lielf__strdup(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static uint64_t lielf__align_up(uint64_t v, uint64_t a) {
    if (a <= 1) return v;
    return (v + (a - 1)) & ~(a - 1);
}

/* ---------------------------------------------------------------------
 *  lifecycle
 * --------------------------------------------------------------------- */
lielf_binary_t *lielf_new(uint16_t e_type, uint16_t e_machine) {
    lielf_binary_t *bin = (lielf_binary_t *)calloc(1, sizeof(*bin));
    if (!bin) return NULL;

    bin->ehdr.e_ident[0] = 0x7f;
    bin->ehdr.e_ident[1] = 'E';
    bin->ehdr.e_ident[2] = 'L';
    bin->ehdr.e_ident[3] = 'F';
    bin->ehdr.e_ident[LIELF_EI_CLASS]   = LIELF_ELFCLASS64;
    bin->ehdr.e_ident[LIELF_EI_DATA]    = LIELF_ELFDATA2LSB;
    bin->ehdr.e_ident[LIELF_EI_VERSION] = LIELF_EV_CURRENT;
    bin->ehdr.e_ident[LIELF_EI_OSABI]   = LIELF_ELFOSABI_NONE;
    bin->ehdr.e_type      = e_type;
    bin->ehdr.e_machine   = e_machine;
    bin->ehdr.e_version   = LIELF_EV_CURRENT;
    bin->ehdr.e_ehsize    = sizeof(lielf_raw_ehdr_t);
    bin->ehdr.e_phentsize = sizeof(lielf_raw_phdr_t);
    bin->ehdr.e_shentsize = sizeof(lielf_raw_shdr_t);

    bin->base_vaddr = (e_type == LIELF_ET_EXEC)
        ? LIELF_DEFAULT_BASE_EXEC
        : LIELF_DEFAULT_BASE_DYN;

    /* section index 0: SHT_NULL placeholder, required by the ELF spec */
    LIELF__ENSURE(bin->sections, bin->nsections, bin->capsections, lielf_section_t);
    memset(&bin->sections[0], 0, sizeof(lielf_section_t));
    bin->sections[0].name = lielf__strdup("");
    bin->nsections = 1;

    /* symbol table index 0: STN_UNDEF placeholder */
    LIELF__ENSURE(bin->symbols, bin->nsymbols, bin->capsymbols, lielf_symbol_t);
    memset(&bin->symbols[0], 0, sizeof(lielf_symbol_t));
    bin->symbols[0].name = lielf__strdup("");
    bin->nsymbols = 1;

    LIELF__ENSURE(bin->dynsymbols, bin->ndynsymbols, bin->capdynsymbols, lielf_symbol_t);
    memset(&bin->dynsymbols[0], 0, sizeof(lielf_symbol_t));
    bin->dynsymbols[0].name = lielf__strdup("");
    bin->ndynsymbols = 1;

    return bin;
}

void lielf_free(lielf_binary_t *bin) {
    if (!bin) return;
    size_t i;
    for (i = 0; i < bin->nsections; i++) {
        free(bin->sections[i].name);
        free(bin->sections[i].data);
        free(bin->sections[i]._rela_target);
    }
    free(bin->sections);
    free(bin->segments);
    for (i = 0; i < bin->nsymbols; i++) free(bin->symbols[i].name);
    free(bin->symbols);
    for (i = 0; i < bin->ndynsymbols; i++) free(bin->dynsymbols[i].name);
    free(bin->dynsymbols);
    for (i = 0; i < bin->nrelocs; i++) free(bin->relocs[i].target_section);
    free(bin->relocs);
    free(bin->dynamic);
    free(bin);
}

/* ---------------------------------------------------------------------
 *  sections
 * --------------------------------------------------------------------- */
lielf_section_t *lielf_get_section(lielf_binary_t *bin, const char *name) {
    size_t i;
    for (i = 0; i < bin->nsections; i++)
        if (bin->sections[i].name && strcmp(bin->sections[i].name, name) == 0)
            return &bin->sections[i];
    return NULL;
}

lielf_section_t *lielf_add_section(lielf_binary_t *bin, const char *name,
                                    uint32_t type, uint64_t flags,
                                    const uint8_t *data, uint64_t size,
                                    uint64_t addralign) {
    LIELF__ENSURE(bin->sections, bin->nsections, bin->capsections, lielf_section_t);
    lielf_section_t *s = &bin->sections[bin->nsections++];
    memset(s, 0, sizeof(*s));
    s->name = lielf__strdup(name);
    s->type = type;
    s->flags = flags;
    s->size = size;
    s->addralign = addralign ? addralign : 1;
    s->kind = LIELF_SEC_NORMAL;
    s->_dirty = 1;
    if (type != LIELF_SHT_NOBITS && size > 0) {
        s->data = (uint8_t *)malloc((size_t)size);
        if (!s->data) { bin->nsections--; free(s->name); return NULL; }
        if (data) memcpy(s->data, data, (size_t)size);
        else memset(s->data, 0, (size_t)size);
    }
    return s;
}

int lielf_remove_section(lielf_binary_t *bin, const char *name) {
    size_t i;
    for (i = 0; i < bin->nsections; i++) {
        if (bin->sections[i].name && strcmp(bin->sections[i].name, name) == 0) {
            free(bin->sections[i].name);
            free(bin->sections[i].data);
            free(bin->sections[i]._rela_target);
            memmove(&bin->sections[i], &bin->sections[i + 1],
                    (bin->nsections - i - 1) * sizeof(lielf_section_t));
            bin->nsections--;
            return 0;
        }
    }
    return -1;
}

int lielf_set_section_data(lielf_binary_t *bin, lielf_section_t *sec,
                            const uint8_t *data, uint64_t size) {
    (void)bin;
    uint8_t *nd = NULL;
    if (size > 0) {
        nd = (uint8_t *)malloc((size_t)size);
        if (!nd) return -1;
        if (data) memcpy(nd, data, (size_t)size);
        else memset(nd, 0, (size_t)size);
    }
    free(sec->data);
    sec->data = nd;
    sec->size = size;
    sec->_dirty = 1;
    return 0;
}

/* ---------------------------------------------------------------------
 *  segments
 * --------------------------------------------------------------------- */
lielf_segment_t *lielf_add_segment(lielf_binary_t *bin, uint32_t type, uint32_t flags,
                                    uint64_t vaddr, uint64_t filesz, uint64_t memsz,
                                    uint64_t align) {
    LIELF__ENSURE(bin->segments, bin->nsegments, bin->capsegments, lielf_segment_t);
    lielf_segment_t *seg = &bin->segments[bin->nsegments++];
    memset(seg, 0, sizeof(*seg));
    seg->type   = type;
    seg->flags  = flags;
    seg->vaddr  = vaddr;
    seg->paddr  = vaddr;
    seg->filesz = filesz;
    seg->memsz  = memsz;
    seg->align  = align ? align : 1;
    return seg;
}

/* ---------------------------------------------------------------------
 *  symbols
 * --------------------------------------------------------------------- */
lielf_symbol_t *lielf_add_symbol(lielf_binary_t *bin, const char *name,
                                  uint64_t value, uint64_t size,
                                  uint8_t info, uint16_t shndx, int dynamic) {
    lielf_symbol_t *sym;
    if (dynamic) {
        LIELF__ENSURE(bin->dynsymbols, bin->ndynsymbols, bin->capdynsymbols, lielf_symbol_t);
        sym = &bin->dynsymbols[bin->ndynsymbols++];
    } else {
        LIELF__ENSURE(bin->symbols, bin->nsymbols, bin->capsymbols, lielf_symbol_t);
        sym = &bin->symbols[bin->nsymbols++];
    }
    memset(sym, 0, sizeof(*sym));
    sym->name  = lielf__strdup(name);
    sym->value = value;
    sym->size  = size;
    sym->info  = info;
    sym->shndx = shndx;
    return sym;
}

lielf_symbol_t *lielf_find_symbol(lielf_binary_t *bin, const char *name, int dynamic) {
    lielf_symbol_t *arr = dynamic ? bin->dynsymbols : bin->symbols;
    size_t n = dynamic ? bin->ndynsymbols : bin->nsymbols;
    size_t i;
    for (i = 1; i < n; i++) /* skip STN_UNDEF at index 0 */
        if (arr[i].name && strcmp(arr[i].name, name) == 0)
            return &arr[i];
    return NULL;
}

int lielf_symbol_index(lielf_binary_t *bin, lielf_symbol_t *sym, int dynamic) {
    lielf_symbol_t *arr = dynamic ? bin->dynsymbols : bin->symbols;
    size_t n = dynamic ? bin->ndynsymbols : bin->nsymbols;
    size_t i;
    for (i = 0; i < n; i++)
        if (&arr[i] == sym) return (int)i;
    return -1;
}

/* ---------------------------------------------------------------------
 *  relocations
 * --------------------------------------------------------------------- */
lielf_reloc_t *lielf_add_reloc(lielf_binary_t *bin, const char *target_section,
                                uint64_t offset, uint32_t sym_index, uint32_t type,
                                int64_t addend, int dynamic) {
    LIELF__ENSURE(bin->relocs, bin->nrelocs, bin->caprelocs, lielf_reloc_t);
    lielf_reloc_t *r = &bin->relocs[bin->nrelocs++];
    memset(r, 0, sizeof(*r));
    r->target_section = lielf__strdup(target_section ? target_section : "");
    r->offset  = offset;
    r->sym     = sym_index;
    r->type    = type;
    r->addend  = addend;
    r->dynamic = dynamic;
    return r;
}

/* ---------------------------------------------------------------------
 *  dynamic section
 * --------------------------------------------------------------------- */
lielf_dynentry_t *lielf_get_dynamic(lielf_binary_t *bin, int64_t tag) {
    size_t i;
    for (i = 0; i < bin->ndynamic; i++)
        if (bin->dynamic[i].tag == tag) return &bin->dynamic[i];
    return NULL;
}

lielf_dynentry_t *lielf_add_dynamic(lielf_binary_t *bin, int64_t tag, uint64_t val) {
    LIELF__ENSURE(bin->dynamic, bin->ndynamic, bin->capdynamic, lielf_dynentry_t);
    lielf_dynentry_t *d = &bin->dynamic[bin->ndynamic++];
    d->tag = tag;
    d->val = val;
    return d;
}

/* ---------------------------------------------------------------------
 *  entry point
 * --------------------------------------------------------------------- */
uint64_t lielf_get_entry(lielf_binary_t *bin) { return bin->ehdr.e_entry; }
void     lielf_set_entry(lielf_binary_t *bin, uint64_t entry) { bin->ehdr.e_entry = entry; }

/* ---------------------------------------------------------------------
 *  loading
 * --------------------------------------------------------------------- */
static int lielf__ensure_null_sym(lielf_binary_t *bin, int dynamic) {
    if (dynamic) {
        if (bin->ndynsymbols != 0) return 0;
        if (bin->ndynsymbols >= bin->capdynsymbols) {
            size_t nc = bin->capdynsymbols ? bin->capdynsymbols * 2 : 4;
            lielf_symbol_t *t = (lielf_symbol_t *)realloc(bin->dynsymbols, nc * sizeof(lielf_symbol_t));
            if (!t) return -1;
            bin->dynsymbols = t;
            bin->capdynsymbols = nc;
        }
        memset(&bin->dynsymbols[0], 0, sizeof(lielf_symbol_t));
        bin->dynsymbols[0].name = lielf__strdup("");
        bin->ndynsymbols = 1;
    } else {
        if (bin->nsymbols != 0) return 0;
        if (bin->nsymbols >= bin->capsymbols) {
            size_t nc = bin->capsymbols ? bin->capsymbols * 2 : 4;
            lielf_symbol_t *t = (lielf_symbol_t *)realloc(bin->symbols, nc * sizeof(lielf_symbol_t));
            if (!t) return -1;
            bin->symbols = t;
            bin->capsymbols = nc;
        }
        memset(&bin->symbols[0], 0, sizeof(lielf_symbol_t));
        bin->symbols[0].name = lielf__strdup("");
        bin->nsymbols = 1;
    }
    return 0;
}

lielf_binary_t *lielf_load_mem(const uint8_t *buf, size_t size) {
    if (size < sizeof(lielf_raw_ehdr_t)) return NULL;
    if (buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F') return NULL;
    if (buf[LIELF_EI_CLASS] != LIELF_ELFCLASS64) return NULL; /* ELF64 only */
    if (buf[LIELF_EI_DATA]  != LIELF_ELFDATA2LSB) return NULL; /* little-endian only */

    lielf_binary_t *bin = (lielf_binary_t *)calloc(1, sizeof(*bin));
    if (!bin) return NULL;
    memcpy(&bin->ehdr, buf, sizeof(lielf_raw_ehdr_t));
    bin->base_vaddr = (bin->ehdr.e_type == LIELF_ET_EXEC)
        ? LIELF_DEFAULT_BASE_EXEC
        : LIELF_DEFAULT_BASE_DYN;

    /* ---- section headers + raw bytes ---- */
    size_t shnum = bin->ehdr.e_shnum;
    if (shnum && (uint64_t)bin->ehdr.e_shoff + (uint64_t)shnum * sizeof(lielf_raw_shdr_t) <= size) {
        const lielf_raw_shdr_t *shdrs = (const lielf_raw_shdr_t *)(buf + bin->ehdr.e_shoff);
        const char *shstrtab = NULL;
        size_t shstrsz = 0;
        if (bin->ehdr.e_shstrndx < shnum) {
            uint64_t off = shdrs[bin->ehdr.e_shstrndx].sh_offset;
            uint64_t sz  = shdrs[bin->ehdr.e_shstrndx].sh_size;
            if (off + sz <= size) { shstrtab = (const char *)(buf + off); shstrsz = (size_t)sz; }
        }

        size_t i;
        for (i = 0; i < shnum; i++) {
            LIELF__ENSURE(bin->sections, bin->nsections, bin->capsections, lielf_section_t);
            lielf_section_t *s = &bin->sections[bin->nsections++];
            memset(s, 0, sizeof(*s));
            const lielf_raw_shdr_t *sh = &shdrs[i];
            const char *nm = "";
            if (shstrtab && sh->sh_name < shstrsz) nm = shstrtab + sh->sh_name;
            s->name      = lielf__strdup(nm);
            s->type      = sh->sh_type;
            s->flags     = sh->sh_flags;
            s->addr      = sh->sh_addr;
            s->offset    = sh->sh_offset;
            s->size      = sh->sh_size;
            s->link      = sh->sh_link;
            s->info      = sh->sh_info;
            s->addralign = sh->sh_addralign;
            s->entsize   = sh->sh_entsize;
            s->kind      = LIELF_SEC_NORMAL;
            s->_loaded_alloc = (sh->sh_type != LIELF_SHT_NULL && (sh->sh_flags & LIELF_SHF_ALLOC)) ? 1 : 0;
            if (sh->sh_type != LIELF_SHT_NOBITS &&
                sh->sh_size > 0 &&
                sh->sh_offset + sh->sh_size <= size) {
                s->data = (uint8_t *)malloc((size_t)sh->sh_size);
                if (s->data)
                    memcpy(s->data, buf + sh->sh_offset, (size_t)sh->sh_size);
            }
        }

        /* ---- classify special sections ---- */
        if (bin->ehdr.e_shstrndx != 0 && bin->ehdr.e_shstrndx < bin->nsections)
            bin->sections[bin->ehdr.e_shstrndx].kind = LIELF_SEC_SHSTRTAB;

        for (i = 0; i < bin->nsections; i++) {
            lielf_section_t *s = &bin->sections[i];
            if (s->type == LIELF_SHT_SYMTAB) {
                s->kind = LIELF_SEC_SYMTAB;
                if (s->link < bin->nsections) bin->sections[s->link].kind = LIELF_SEC_STRTAB;
            } else if (s->type == LIELF_SHT_DYNSYM) {
                s->kind = LIELF_SEC_DYNSYM;
                if (s->link < bin->nsections) bin->sections[s->link].kind = LIELF_SEC_DYNSTR;
            } else if (s->type == LIELF_SHT_DYNAMIC) {
                s->kind = LIELF_SEC_DYNAMIC;
            } else if (s->type == LIELF_SHT_RELA) {
                s->kind = LIELF_SEC_RELA;
                if (s->info < bin->nsections)
                    s->_rela_target = lielf__strdup(bin->sections[s->info].name);
                if (s->link < bin->nsections)
                    s->_rela_dynamic = (bin->sections[s->link].type == LIELF_SHT_DYNSYM) ? 1 : 0;
            }
        }

        /* ---- parse symtab/dynsym/rela/dynamic into structured arrays ---- */
        for (i = 0; i < bin->nsections; i++) {
            lielf_section_t *s = &bin->sections[i];
            if (s->kind == LIELF_SEC_SYMTAB || s->kind == LIELF_SEC_DYNSYM) {
                const char *strtab = NULL;
                size_t strsz = 0;
                if (s->link < bin->nsections) {
                    strtab = (const char *)bin->sections[s->link].data;
                    strsz  = (size_t)bin->sections[s->link].size;
                }
                size_t n = s->entsize ? (size_t)(s->size / s->entsize) : 0;
                size_t j;
                for (j = 0; j < n && s->data; j++) {
                    const lielf_raw_sym_t *rs = (const lielf_raw_sym_t *)(s->data + j * sizeof(lielf_raw_sym_t));
                    lielf_symbol_t sym;
                    memset(&sym, 0, sizeof(sym));
                    const char *nm = "";
                    if (strtab && rs->st_name < strsz) nm = strtab + rs->st_name;
                    sym.name   = lielf__strdup(nm);
                    sym.value  = rs->st_value;
                    sym.size   = rs->st_size;
                    sym.info   = rs->st_info;
                    sym.other  = rs->st_other;
                    sym.shndx  = rs->st_shndx;
                    if (s->kind == LIELF_SEC_SYMTAB) {
                        LIELF__ENSURE(bin->symbols, bin->nsymbols, bin->capsymbols, lielf_symbol_t);
                        bin->symbols[bin->nsymbols++] = sym;
                    } else {
                        LIELF__ENSURE(bin->dynsymbols, bin->ndynsymbols, bin->capdynsymbols, lielf_symbol_t);
                        bin->dynsymbols[bin->ndynsymbols++] = sym;
                    }
                }
            } else if (s->kind == LIELF_SEC_RELA && s->data) {
                size_t n = s->entsize ? (size_t)(s->size / s->entsize)
                                      : (size_t)(s->size / sizeof(lielf_raw_rela_t));
                size_t j;
                for (j = 0; j < n; j++) {
                    const lielf_raw_rela_t *rr = (const lielf_raw_rela_t *)(s->data + j * sizeof(lielf_raw_rela_t));
                    LIELF__ENSURE(bin->relocs, bin->nrelocs, bin->caprelocs, lielf_reloc_t);
                    lielf_reloc_t *r = &bin->relocs[bin->nrelocs++];
                    memset(r, 0, sizeof(*r));
                    r->target_section = lielf__strdup(s->_rela_target ? s->_rela_target : "");
                    r->offset  = rr->r_offset;
                    r->sym     = (uint32_t)(rr->r_info >> 32);
                    r->type    = (uint32_t)(rr->r_info & 0xffffffffu);
                    r->addend  = rr->r_addend;
                    r->dynamic = s->_rela_dynamic;
                }
            } else if (s->kind == LIELF_SEC_DYNAMIC && s->data) {
                size_t n = s->entsize ? (size_t)(s->size / s->entsize)
                                      : (size_t)(s->size / sizeof(lielf_raw_dyn_t));
                size_t j;
                for (j = 0; j < n; j++) {
                    const lielf_raw_dyn_t *rd = (const lielf_raw_dyn_t *)(s->data + j * sizeof(lielf_raw_dyn_t));
                    if (rd->d_tag == LIELF_DT_NULL) break;
                    LIELF__ENSURE(bin->dynamic, bin->ndynamic, bin->capdynamic, lielf_dynentry_t);
                    bin->dynamic[bin->ndynamic].tag = rd->d_tag;
                    bin->dynamic[bin->ndynamic].val = rd->d_val;
                    bin->ndynamic++;
                }
            }
        }
    }

    if (lielf__ensure_null_sym(bin, 0) != 0 || lielf__ensure_null_sym(bin, 1) != 0) {
        lielf_free(bin);
        return NULL;
    }

    /* ---- program headers ---- */
    size_t phnum = bin->ehdr.e_phnum;
    if (phnum && (uint64_t)bin->ehdr.e_phoff + (uint64_t)phnum * sizeof(lielf_raw_phdr_t) <= size) {
        const lielf_raw_phdr_t *phdrs = (const lielf_raw_phdr_t *)(buf + bin->ehdr.e_phoff);
        size_t i;
        for (i = 0; i < phnum; i++) {
            LIELF__ENSURE(bin->segments, bin->nsegments, bin->capsegments, lielf_segment_t);
            lielf_segment_t *seg = &bin->segments[bin->nsegments++];
            seg->type   = phdrs[i].p_type;
            seg->flags  = phdrs[i].p_flags;
            seg->offset = phdrs[i].p_offset;
            seg->vaddr  = phdrs[i].p_vaddr;
            seg->paddr  = phdrs[i].p_paddr;
            seg->filesz = phdrs[i].p_filesz;
            seg->memsz  = phdrs[i].p_memsz;
            seg->align  = phdrs[i].p_align;
        }
    }

    return bin;
}

lielf_binary_t *lielf_load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { free(buf); return NULL; }
    lielf_binary_t *bin = lielf_load_mem(buf, (size_t)sz);
    free(buf);
    return bin;
}

/* ---------------------------------------------------------------------
 *  build helpers: regenerate special sections from the object model
 * --------------------------------------------------------------------- */

/* keep `sec`'s data == newbuf/newsize, but only mark it dirty if the
 * content actually changed (so unchanged ALLOC sections, e.g. .dynsym
 * in an untouched binary, stay put on rebuild). Takes ownership of newbuf. */
static void lielf__maybe_update_data(lielf_section_t *sec, uint8_t *newbuf, size_t newsize) {
    if (sec->data && sec->size == newsize &&
        (newsize == 0 || memcmp(sec->data, newbuf, newsize) == 0)) {
        free(newbuf);
        return;
    }
    free(sec->data);
    sec->data = newbuf;
    sec->size = (uint64_t)newsize;
    sec->_dirty = 1;
}

static int lielf__rebuild_strtab(lielf_binary_t *bin, int dynamic) {
    lielf_symbol_t *syms = dynamic ? bin->dynsymbols : bin->symbols;
    size_t n = dynamic ? bin->ndynsymbols : bin->nsymbols;
    const char *secname = dynamic ? ".dynstr" : ".strtab";

    if (n <= 1) {
        lielf_section_t *sec = lielf_get_section(bin, secname);
        if (!sec) return 0; /* nothing referencing it, leave as-is */
        uint8_t *buf = (uint8_t *)malloc(1);
        if (!buf) return -1;
        buf[0] = 0;
        lielf__maybe_update_data(sec, buf, 1);
        return 0;
    }

    size_t total = 1, i;
    for (i = 1; i < n; i++) total += strlen(syms[i].name) + 1;
    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) return -1;
    buf[0] = 0;
    size_t pos = 1;
    for (i = 1; i < n; i++) {
        size_t l = strlen(syms[i].name) + 1;
        memcpy(buf + pos, syms[i].name, l);
        pos += l;
    }

    lielf_section_t *sec = lielf_get_section(bin, secname);
    if (!sec) sec = lielf_add_section(bin, secname, LIELF_SHT_STRTAB,
                                       dynamic ? LIELF_SHF_ALLOC : 0, NULL, 0, 1);
    if (!sec) { free(buf); return -1; }
    sec->type    = LIELF_SHT_STRTAB;
    sec->entsize = 0;
    sec->kind    = dynamic ? LIELF_SEC_DYNSTR : LIELF_SEC_STRTAB;
    lielf__maybe_update_data(sec, buf, total);
    return 0;
}

static int lielf__rebuild_symtab(lielf_binary_t *bin, int dynamic) {
    lielf_symbol_t *syms = dynamic ? bin->dynsymbols : bin->symbols;
    size_t n = dynamic ? bin->ndynsymbols : bin->nsymbols;
    const char *secname = dynamic ? ".dynsym" : ".symtab";

    if (n <= 1) {
        lielf_section_t *sec = lielf_get_section(bin, secname);
        if (!sec) return 0;
        uint8_t *buf = (uint8_t *)calloc(1, sizeof(lielf_raw_sym_t));
        if (!buf) return -1;
        lielf__maybe_update_data(sec, buf, sizeof(lielf_raw_sym_t));
        return 0;
    }

    size_t total = n * sizeof(lielf_raw_sym_t);
    uint8_t *buf = (uint8_t *)calloc(1, total);
    if (!buf) return -1;

    size_t name_off = 1, i;
    for (i = 0; i < n; i++) {
        lielf_raw_sym_t rs;
        memset(&rs, 0, sizeof(rs));
        if (i != 0) {
            rs.st_name = (uint32_t)name_off;
            name_off += strlen(syms[i].name) + 1;
        }
        rs.st_info  = syms[i].info;
        rs.st_other = syms[i].other;
        rs.st_shndx = syms[i].shndx;
        rs.st_value = syms[i].value;
        rs.st_size  = syms[i].size;
        memcpy(buf + i * sizeof(rs), &rs, sizeof(rs));
    }

    lielf_section_t *sec = lielf_get_section(bin, secname);
    if (!sec) sec = lielf_add_section(bin, secname,
                                       dynamic ? LIELF_SHT_DYNSYM : LIELF_SHT_SYMTAB,
                                       dynamic ? LIELF_SHF_ALLOC : 0, NULL, 0, 8);
    if (!sec) { free(buf); return -1; }
    sec->type    = dynamic ? LIELF_SHT_DYNSYM : LIELF_SHT_SYMTAB;
    sec->entsize = sizeof(lielf_raw_sym_t);
    sec->kind    = dynamic ? LIELF_SEC_DYNSYM : LIELF_SEC_SYMTAB;

    uint32_t first_global = (uint32_t)n;
    for (i = 0; i < n; i++) {
        if (LIELF_ST_BIND(syms[i].info) != LIELF_STB_LOCAL) { first_global = (uint32_t)i; break; }
    }
    sec->info = first_global;

    lielf__maybe_update_data(sec, buf, total);
    return 0;
}

static lielf_section_t *lielf__find_or_create_rela(lielf_binary_t *bin, const char *target, int dynamic) {
    size_t i;
    const char *t = target ? target : "";
    for (i = 0; i < bin->nsections; i++) {
        lielf_section_t *s = &bin->sections[i];
        if (s->kind != LIELF_SEC_RELA) continue;
        if (s->_rela_dynamic != dynamic) continue;
        const char *st = s->_rela_target ? s->_rela_target : "";
        if (strcmp(st, t) == 0) return s;
    }
    char name[256];
    if (dynamic && t[0] == 0) snprintf(name, sizeof(name), ".rela.dyn");
    else snprintf(name, sizeof(name), ".rela%s", t);

    lielf_section_t *s = lielf_add_section(bin, name, LIELF_SHT_RELA,
                                            dynamic ? LIELF_SHF_ALLOC : 0, NULL, 0, 8);
    if (!s) return NULL;
    s->kind          = LIELF_SEC_RELA;
    s->_rela_dynamic = dynamic;
    s->_rela_target  = lielf__strdup(t);
    return s;
}

static int lielf__rebuild_one_rela(lielf_binary_t *bin, lielf_section_t *sec) {
    const char *st = sec->_rela_target ? sec->_rela_target : "";
    size_t cnt = 0, ri;
    for (ri = 0; ri < bin->nrelocs; ri++) {
        lielf_reloc_t *r = &bin->relocs[ri];
        const char *rt = r->target_section ? r->target_section : "";
        if (r->dynamic == sec->_rela_dynamic && strcmp(rt, st) == 0) cnt++;
    }
    size_t total = cnt * sizeof(lielf_raw_rela_t);
    uint8_t *buf = total ? (uint8_t *)malloc(total) : NULL;
    if (total && !buf) return -1;
    size_t pos = 0;
    for (ri = 0; ri < bin->nrelocs; ri++) {
        lielf_reloc_t *r = &bin->relocs[ri];
        const char *rt = r->target_section ? r->target_section : "";
        if (r->dynamic != sec->_rela_dynamic || strcmp(rt, st) != 0) continue;
        lielf_raw_rela_t rr;
        rr.r_offset = r->offset;
        rr.r_info   = ((uint64_t)r->sym << 32) | (uint64_t)r->type;
        rr.r_addend = r->addend;
        memcpy(buf + pos, &rr, sizeof(rr));
        pos += sizeof(rr);
    }
    sec->entsize = sizeof(lielf_raw_rela_t);
    lielf__maybe_update_data(sec, buf, total);
    return 0;
}

static int lielf__rebuild_relas(lielf_binary_t *bin) {
    size_t i;
    for (i = 0; i < bin->nsections; i++)
        if (bin->sections[i].kind == LIELF_SEC_RELA)
            if (lielf__rebuild_one_rela(bin, &bin->sections[i]) != 0) return -1;

    for (i = 0; i < bin->nrelocs; i++) {
        lielf_reloc_t *r = &bin->relocs[i];
        if (!lielf__find_or_create_rela(bin, r->target_section, r->dynamic)) return -1;
    }

    for (i = 0; i < bin->nsections; i++)
        if (bin->sections[i].kind == LIELF_SEC_RELA)
            if (lielf__rebuild_one_rela(bin, &bin->sections[i]) != 0) return -1;
    return 0;
}

static int lielf__rebuild_dynamic(lielf_binary_t *bin) {
    if (bin->ndynamic == 0) return 0; /* leave any existing .dynamic untouched */

    size_t n = bin->ndynamic + 1; /* + DT_NULL terminator */
    size_t total = n * sizeof(lielf_raw_dyn_t);
    uint8_t *buf = (uint8_t *)calloc(1, total);
    if (!buf) return -1;
    size_t i;
    for (i = 0; i < bin->ndynamic; i++) {
        lielf_raw_dyn_t rd;
        rd.d_tag = bin->dynamic[i].tag;
        rd.d_val = bin->dynamic[i].val;
        memcpy(buf + i * sizeof(rd), &rd, sizeof(rd));
    }
    /* trailing entry left zeroed -> DT_NULL */

    lielf_section_t *sec = lielf_get_section(bin, ".dynamic");
    if (!sec) sec = lielf_add_section(bin, ".dynamic", LIELF_SHT_DYNAMIC,
                                       LIELF_SHF_ALLOC | LIELF_SHF_WRITE, NULL, 0, 8);
    if (!sec) { free(buf); return -1; }
    sec->type    = LIELF_SHT_DYNAMIC;
    sec->entsize = sizeof(lielf_raw_dyn_t);
    sec->kind    = LIELF_SEC_DYNAMIC;
    lielf__maybe_update_data(sec, buf, total);
    return 0;
}

static int lielf__rebuild_shstrtab(lielf_binary_t *bin) {
    lielf_section_t *shstrtab = lielf_get_section(bin, ".shstrtab");
    if (!shstrtab) {
        shstrtab = lielf_add_section(bin, ".shstrtab", LIELF_SHT_STRTAB, 0, NULL, 0, 1);
        if (!shstrtab) return -1;
    }
    shstrtab->kind    = LIELF_SEC_SHSTRTAB;
    shstrtab->type    = LIELF_SHT_STRTAB;
    shstrtab->entsize = 0;

    size_t total = 1, i;
    for (i = 0; i < bin->nsections; i++) total += strlen(bin->sections[i].name) + 1;
    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) return -1;
    buf[0] = 0;
    size_t pos = 1;
    for (i = 0; i < bin->nsections; i++) {
        size_t l = strlen(bin->sections[i].name) + 1;
        memcpy(buf + pos, bin->sections[i].name, l);
        pos += l;
    }
    lielf__maybe_update_data(shstrtab, buf, total);
    return 0;
}

static int lielf__section_index(lielf_binary_t *bin, const char *name) {
    size_t i;
    for (i = 0; i < bin->nsections; i++)
        if (strcmp(bin->sections[i].name, name) == 0) return (int)i;
    return 0;
}

static void lielf__resolve_links(lielf_binary_t *bin) {
    size_t i;
    for (i = 0; i < bin->nsections; i++) {
        lielf_section_t *s = &bin->sections[i];
        switch (s->kind) {
            case LIELF_SEC_SYMTAB:
                s->link = (uint32_t)lielf__section_index(bin, ".strtab");
                break;
            case LIELF_SEC_DYNSYM:
                s->link = (uint32_t)lielf__section_index(bin, ".dynstr");
                break;
            case LIELF_SEC_RELA:
                s->link = (uint32_t)lielf__section_index(bin, s->_rela_dynamic ? ".dynsym" : ".symtab");
                s->info = (s->_rela_target && s->_rela_target[0])
                              ? (uint32_t)lielf__section_index(bin, s->_rela_target)
                              : 0;
                break;
            default:
                break;
        }
    }
}

/* ---------------------------------------------------------------------
 *  build / write
 * --------------------------------------------------------------------- */
uint8_t *lielf_build(lielf_binary_t *bin, size_t *out_size) {
    if (!bin) return NULL;

    /* 1. regenerate everything derived from the structured model */
    if (lielf__rebuild_strtab(bin, 0) != 0) return NULL;
    if (lielf__rebuild_symtab(bin, 0) != 0) return NULL;
    if (lielf__rebuild_strtab(bin, 1) != 0) return NULL;
    if (lielf__rebuild_symtab(bin, 1) != 0) return NULL;
    if (lielf__rebuild_relas(bin) != 0) return NULL;
    if (lielf__rebuild_dynamic(bin) != 0) return NULL;
    if (lielf__rebuild_shstrtab(bin) != 0) return NULL;
    lielf__resolve_links(bin);

    int is_rel = (bin->ehdr.e_type == LIELF_ET_REL);
    size_t orig_nsegments = bin->nsegments;

    /* 2. base load address used for newly appended ALLOC sections */
    uint64_t base = bin->base_vaddr;
    if (!is_rel) {
        size_t i;
        for (i = 0; i < orig_nsegments; i++) {
            if (bin->segments[i].type == LIELF_PT_LOAD) {
                base = bin->segments[i].vaddr - bin->segments[i].offset;
                break;
            }
        }
    }

    /* 3. classify sections: 0=skip(NULL), 1=fixed-alloc, 2=new/resized-alloc, 3=meta */
    uint64_t fixed_end = 64;
    if (orig_nsegments > 0) {
        uint64_t a = 64 + orig_nsegments * (uint64_t)sizeof(lielf_raw_phdr_t);
        uint64_t b = bin->ehdr.e_phoff + orig_nsegments * (uint64_t)sizeof(lielf_raw_phdr_t);
        fixed_end = a > b ? a : b;
    }

    size_t nsec = bin->nsections;
    int *cls = (int *)calloc(nsec, sizeof(int));
    if (!cls) return NULL;

    {
        size_t i;
        for (i = 1; i < nsec; i++) {
            lielf_section_t *s = &bin->sections[i];
            if (s->type == LIELF_SHT_NULL) { cls[i] = 0; continue; }
            if (!(s->flags & LIELF_SHF_ALLOC)) { cls[i] = 3; continue; }
            int fixed = (!is_rel) && s->_loaded_alloc && !s->_dirty && orig_nsegments > 0;
            cls[i] = fixed ? 1 : 2;
            if (cls[i] == 1) {
                uint64_t end = s->offset + (s->type == LIELF_SHT_NOBITS ? 0 : s->size);
                if (end > fixed_end) fixed_end = end;
            }
        }
    }

    /* 4. lay out new/resized ALLOC sections in a fresh page-aligned region */
    uint64_t alloc_region_align = is_rel ? 8 : LIELF_PAGE_SIZE;
    uint64_t cur = lielf__align_up(fixed_end, alloc_region_align);
    uint64_t alloc_region_start = cur;
    int any_new_alloc = 0;
    uint32_t new_alloc_flags = 0;
    {
        size_t i;
        for (i = 1; i < nsec; i++) {
            if (cls[i] != 2) continue;
            any_new_alloc = 1;
            lielf_section_t *s = &bin->sections[i];
            uint64_t al = s->addralign ? s->addralign : 1;
            cur = lielf__align_up(cur, al);
            s->offset = cur;
            s->addr = is_rel ? 0 : (base + cur);
            if (s->type != LIELF_SHT_NOBITS) cur += s->size;
            new_alloc_flags |= LIELF_PF_R;
            if (s->flags & LIELF_SHF_WRITE)     new_alloc_flags |= LIELF_PF_W;
            if (s->flags & LIELF_SHF_EXECINSTR) new_alloc_flags |= LIELF_PF_X;
        }
    }
    uint64_t alloc_end = cur;

    /* 5. lay out non-ALLOC sections in a metadata block after that */
    cur = lielf__align_up(alloc_end, 8);
    {
        size_t i;
        for (i = 1; i < nsec; i++) {
            if (cls[i] != 3) continue;
            lielf_section_t *s = &bin->sections[i];
            uint64_t al = s->addralign ? s->addralign : 1;
            cur = lielf__align_up(cur, al);
            s->offset = cur;
            s->addr = 0;
            if (s->type != LIELF_SHT_NOBITS) cur += s->size;
        }
    }
    uint64_t meta_end = cur;
    free(cls);

    /* 6. append a synthesized PT_LOAD for the new ALLOC region, if any */
    if (!is_rel && any_new_alloc) {
        if (!lielf_add_segment(bin, LIELF_PT_LOAD, new_alloc_flags,
                                base + alloc_region_start,
                                alloc_end - alloc_region_start,
                                alloc_end - alloc_region_start,
                                LIELF_PAGE_SIZE))
            return NULL;
        bin->segments[bin->nsegments - 1].offset = alloc_region_start;
    }

    /* 7. program header table placement */
    uint64_t phdr_off = bin->ehdr.e_phoff;
    if (bin->nsegments != orig_nsegments || orig_nsegments == 0) {
        if (bin->nsegments > 0) {
            phdr_off = lielf__align_up(meta_end, 8);
            meta_end = phdr_off + bin->nsegments * (uint64_t)sizeof(lielf_raw_phdr_t);
        } else {
            phdr_off = 0;
        }
    }
    uint16_t phdr_num_final = (uint16_t)bin->nsegments;

    /* 8. section header table placement */
    uint64_t shdr_off = lielf__align_up(meta_end, 8);
    size_t shstrndx = (size_t)lielf__section_index(bin, ".shstrtab");
    size_t total_size = (size_t)(shdr_off + (uint64_t)nsec * sizeof(lielf_raw_shdr_t));

    bin->ehdr.e_phoff    = (bin->nsegments > 0) ? phdr_off : 0;
    bin->ehdr.e_phnum    = phdr_num_final;
    bin->ehdr.e_shoff    = shdr_off;
    bin->ehdr.e_shnum    = (uint16_t)nsec;
    bin->ehdr.e_shstrndx = (uint16_t)shstrndx;

    /* 9. serialize */
    uint8_t *out = (uint8_t *)calloc(1, total_size);
    if (!out) return NULL;

    memcpy(out, &bin->ehdr, sizeof(lielf_raw_ehdr_t));

    if (phdr_num_final > 0) {
        size_t i;
        for (i = 0; i < bin->nsegments; i++) {
            lielf_raw_phdr_t ph;
            ph.p_type   = bin->segments[i].type;
            ph.p_flags  = bin->segments[i].flags;
            ph.p_offset = bin->segments[i].offset;
            ph.p_vaddr  = bin->segments[i].vaddr;
            ph.p_paddr  = bin->segments[i].paddr;
            ph.p_filesz = bin->segments[i].filesz;
            ph.p_memsz  = bin->segments[i].memsz;
            ph.p_align  = bin->segments[i].align;
            memcpy(out + bin->ehdr.e_phoff + i * sizeof(ph), &ph, sizeof(ph));
        }
    }

    {
        size_t i;
        for (i = 1; i < nsec; i++) {
            lielf_section_t *s = &bin->sections[i];
            if (s->type == LIELF_SHT_NOBITS || s->size == 0 || !s->data) continue;
            memcpy(out + s->offset, s->data, (size_t)s->size);
        }
    }

    {
        /* section-name offsets within .shstrtab, computed the same way
           lielf__rebuild_shstrtab laid them out */
        uint32_t *name_offs = (uint32_t *)malloc(nsec * sizeof(uint32_t));
        if (!name_offs) { free(out); return NULL; }
        size_t pos = 1, i;
        for (i = 0; i < nsec; i++) {
            name_offs[i] = (uint32_t)pos;
            pos += strlen(bin->sections[i].name) + 1;
        }
        for (i = 0; i < nsec; i++) {
            lielf_section_t *s = &bin->sections[i];
            lielf_raw_shdr_t sh;
            memset(&sh, 0, sizeof(sh));
            if (i != 0) {
                sh.sh_name      = name_offs[i];
                sh.sh_type      = s->type;
                sh.sh_flags     = s->flags;
                sh.sh_addr      = s->addr;
                sh.sh_offset    = s->offset;
                sh.sh_size      = s->size;
                sh.sh_link      = s->link;
                sh.sh_info      = s->info;
                sh.sh_addralign = s->addralign;
                sh.sh_entsize   = s->entsize;
            }
            memcpy(out + shdr_off + i * sizeof(sh), &sh, sizeof(sh));
        }
        free(name_offs);
    }

    /* 10. make rebuild idempotent: everything just written is now "fixed" */
    {
        size_t i;
        for (i = 0; i < nsec; i++) {
            lielf_section_t *s = &bin->sections[i];
            s->_loaded_alloc = (s->type != LIELF_SHT_NULL && (s->flags & LIELF_SHF_ALLOC)) ? 1 : 0;
            s->_dirty = 0;
        }
    }

    if (out_size) *out_size = total_size;
    return out;
}

int lielf_write(lielf_binary_t *bin, const char *path) {
    size_t size = 0;
    uint8_t *buf = lielf_build(bin, &size);
    if (!buf) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return -1; }
    size_t wr = fwrite(buf, 1, size, f);
    fclose(f);
    free(buf);
    if (wr != size) return -1;
#ifndef _WIN32
    chmod(path, 0755);
#endif
    return 0;
}

/* ---------------------------------------------------------------------
 *  introspection
 * --------------------------------------------------------------------- */
static const char *lielf__sht_name(uint32_t t) {
    switch (t) {
        case LIELF_SHT_NULL: return "NULL";
        case LIELF_SHT_PROGBITS: return "PROGBITS";
        case LIELF_SHT_SYMTAB: return "SYMTAB";
        case LIELF_SHT_STRTAB: return "STRTAB";
        case LIELF_SHT_RELA: return "RELA";
        case LIELF_SHT_HASH: return "HASH";
        case LIELF_SHT_DYNAMIC: return "DYNAMIC";
        case LIELF_SHT_NOTE: return "NOTE";
        case LIELF_SHT_NOBITS: return "NOBITS";
        case LIELF_SHT_REL: return "REL";
        case LIELF_SHT_DYNSYM: return "DYNSYM";
        case LIELF_SHT_INIT_ARRAY: return "INIT_ARRAY";
        case LIELF_SHT_FINI_ARRAY: return "FINI_ARRAY";
        default: return "?";
    }
}

static const char *lielf__pt_name(uint32_t t) {
    switch (t) {
        case LIELF_PT_NULL: return "NULL";
        case LIELF_PT_LOAD: return "LOAD";
        case LIELF_PT_DYNAMIC: return "DYNAMIC";
        case LIELF_PT_INTERP: return "INTERP";
        case LIELF_PT_NOTE: return "NOTE";
        case LIELF_PT_PHDR: return "PHDR";
        case LIELF_PT_TLS: return "TLS";
        case LIELF_PT_GNU_EH_FRAME: return "GNU_EH_FRAME";
        case LIELF_PT_GNU_STACK: return "GNU_STACK";
        case LIELF_PT_GNU_RELRO: return "GNU_RELRO";
        default: return "?";
    }
}

static const char *lielf__stt_name(uint8_t info) {
    switch (LIELF_ST_TYPE(info)) {
        case LIELF_STT_NOTYPE: return "NOTYPE";
        case LIELF_STT_OBJECT: return "OBJECT";
        case LIELF_STT_FUNC: return "FUNC";
        case LIELF_STT_SECTION: return "SECTION";
        case LIELF_STT_FILE: return "FILE";
        default: return "?";
    }
}

static const char *lielf__stb_name(uint8_t info) {
    switch (LIELF_ST_BIND(info)) {
        case LIELF_STB_LOCAL: return "LOCAL";
        case LIELF_STB_GLOBAL: return "GLOBAL";
        case LIELF_STB_WEAK: return "WEAK";
        default: return "?";
    }
}

void lielf_dump(lielf_binary_t *bin, FILE *out) {
    if (!bin || !out) return;
    const char *type = bin->ehdr.e_type == LIELF_ET_REL ? "REL"
                      : bin->ehdr.e_type == LIELF_ET_EXEC ? "EXEC"
                      : bin->ehdr.e_type == LIELF_ET_DYN ? "DYN" : "?";
    fprintf(out, "ELF64 LE  type=%s  machine=%u  entry=0x%llx\n",
            type, bin->ehdr.e_machine, (unsigned long long)bin->ehdr.e_entry);

    fprintf(out, "\nSections (%zu):\n", bin->nsections);
    fprintf(out, "  %-3s %-18s %-10s %-6s %-10s %-10s %-10s\n",
            "idx", "name", "type", "flags", "addr", "offset", "size");
    size_t i;
    for (i = 0; i < bin->nsections; i++) {
        lielf_section_t *s = &bin->sections[i];
        char flagstr[8]; size_t fp = 0;
        if (s->flags & LIELF_SHF_ALLOC)     flagstr[fp++] = 'A';
        if (s->flags & LIELF_SHF_WRITE)     flagstr[fp++] = 'W';
        if (s->flags & LIELF_SHF_EXECINSTR) flagstr[fp++] = 'X';
        flagstr[fp] = 0;
        fprintf(out, "  %-3zu %-18s %-10s %-6s 0x%08llx 0x%08llx 0x%08llx\n",
                i, s->name, lielf__sht_name(s->type), flagstr,
                (unsigned long long)s->addr, (unsigned long long)s->offset,
                (unsigned long long)s->size);
    }

    if (bin->nsegments) {
        fprintf(out, "\nSegments (%zu):\n", bin->nsegments);
        fprintf(out, "  %-3s %-12s %-5s %-10s %-10s %-10s %-10s\n",
                "idx", "type", "flags", "offset", "vaddr", "filesz", "memsz");
        for (i = 0; i < bin->nsegments; i++) {
            lielf_segment_t *p = &bin->segments[i];
            char flagstr[4]; size_t fp = 0;
            if (p->flags & LIELF_PF_R) flagstr[fp++] = 'R';
            if (p->flags & LIELF_PF_W) flagstr[fp++] = 'W';
            if (p->flags & LIELF_PF_X) flagstr[fp++] = 'X';
            flagstr[fp] = 0;
            fprintf(out, "  %-3zu %-12s %-5s 0x%08llx 0x%08llx 0x%08llx 0x%08llx\n",
                    i, lielf__pt_name(p->type), flagstr,
                    (unsigned long long)p->offset, (unsigned long long)p->vaddr,
                    (unsigned long long)p->filesz, (unsigned long long)p->memsz);
        }
    }

    if (bin->nsymbols > 1) {
        fprintf(out, "\nSymbols (.symtab, %zu):\n", bin->nsymbols);
        fprintf(out, "  %-3s %-10s %-8s %-7s %-7s %-6s %s\n",
                "idx", "value", "size", "bind", "type", "shndx", "name");
        for (i = 0; i < bin->nsymbols; i++) {
            lielf_symbol_t *sym = &bin->symbols[i];
            fprintf(out, "  %-3zu 0x%08llx %-8llu %-7s %-7s %-6u %s\n",
                    i, (unsigned long long)sym->value, (unsigned long long)sym->size,
                    lielf__stb_name(sym->info), lielf__stt_name(sym->info),
                    sym->shndx, sym->name);
        }
    }

    if (bin->ndynsymbols > 1) {
        fprintf(out, "\nDynamic symbols (.dynsym, %zu):\n", bin->ndynsymbols);
        fprintf(out, "  %-3s %-10s %-8s %-7s %-7s %-6s %s\n",
                "idx", "value", "size", "bind", "type", "shndx", "name");
        for (i = 0; i < bin->ndynsymbols; i++) {
            lielf_symbol_t *sym = &bin->dynsymbols[i];
            fprintf(out, "  %-3zu 0x%08llx %-8llu %-7s %-7s %-6u %s\n",
                    i, (unsigned long long)sym->value, (unsigned long long)sym->size,
                    lielf__stb_name(sym->info), lielf__stt_name(sym->info),
                    sym->shndx, sym->name);
        }
    }

    if (bin->nrelocs) {
        fprintf(out, "\nRelocations (%zu):\n", bin->nrelocs);
        fprintf(out, "  %-14s %-10s %-6s %-6s %-6s %s\n",
                "target", "offset", "sym", "type", "dyn", "addend");
        for (i = 0; i < bin->nrelocs; i++) {
            lielf_reloc_t *r = &bin->relocs[i];
            fprintf(out, "  %-14s 0x%08llx %-6u %-6u %-6d %lld\n",
                    r->target_section, (unsigned long long)r->offset,
                    r->sym, r->type, r->dynamic, (long long)r->addend);
        }
    }

    if (bin->ndynamic) {
        fprintf(out, "\nDynamic section (%zu entries):\n", bin->ndynamic);
        for (i = 0; i < bin->ndynamic; i++)
            fprintf(out, "  tag=%-4lld val=0x%llx\n",
                    (long long)bin->dynamic[i].tag, (unsigned long long)bin->dynamic[i].val);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* LIELF_IMPLEMENTATION_INCLUDED */
#endif /* LIELF_IMPLEMENTATION */