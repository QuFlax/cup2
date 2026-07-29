#ifndef SODUMP_H
#define SODUMP_H

#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum {
    SO_SYMBOL_UNKNOWN,
    SO_SYMBOL_FUNC,
    SO_SYMBOL_OBJECT,
    SO_SYMBOL_TLS
} SoSymbolType;

typedef struct {
    char         *name;

    /*
     * For ET_DYN shared libraries, addr is normally a virtual address
     * relative to the library load base.
     */
    uint64_t      addr;
    uint64_t      size;

    uint16_t      section_index;
    unsigned char binding;
    unsigned char visibility;
    SoSymbolType  type;

    int           is_func;
    int           is_object;
    int           is_tls;
    int           is_local;
    int           is_weak;
    int           is_defined;
    int           is_dynamic;
} SoSymbol;

typedef struct {
    SoSymbol *syms;
    size_t    count;
    size_t    capacity;

    void     *map;
    size_t    map_len;
} SoSymTable;

static inline int sodump_range_valid(
    size_t file_size,
    uint64_t offset,
    uint64_t size
) {
    if (offset > file_size)
        return 0;

    return size <= file_size - (size_t)offset;
}

static inline SoSymbolType sodump_symbol_type(unsigned char elf_type) {
    switch (elf_type) {
        case STT_FUNC:
#ifdef STT_GNU_IFUNC
        case STT_GNU_IFUNC:
#endif
            return SO_SYMBOL_FUNC;

        case STT_OBJECT:
        case STT_COMMON:
            return SO_SYMBOL_OBJECT;

        case STT_TLS:
            return SO_SYMBOL_TLS;

        default:
            return SO_SYMBOL_UNKNOWN;
    }
}

static inline int sodump_reserve(SoSymTable *table, size_t wanted) {
    if (wanted <= table->capacity)
        return 1;

    size_t new_capacity = table->capacity ? table->capacity : 64;

    while (new_capacity < wanted) {
        if (new_capacity > SIZE_MAX / 2)
            return 0;

        new_capacity *= 2;
    }

    SoSymbol *new_syms = realloc(
        table->syms,
        new_capacity * sizeof(*new_syms)
    );

    if (!new_syms)
        return 0;

    table->syms = new_syms;
    table->capacity = new_capacity;
    return 1;
}

/*
 * A symbol can occur in both .dynsym and .symtab.
 * Prefer the .symtab copy because it may contain more complete information.
 */
static inline ssize_t sodump_find_duplicate(
    const SoSymTable *table,
    const char *name,
    uint64_t addr,
    uint16_t section_index,
    SoSymbolType type
) {
    for (size_t i = 0; i < table->count; ++i) {
        const SoSymbol *symbol = &table->syms[i];

        if (symbol->addr != addr)
            continue;

        if (symbol->section_index != section_index)
            continue;

        if (symbol->type != type)
            continue;

        if (strcmp(symbol->name, name) != 0)
            continue;

        return (ssize_t)i;
    }

    return -1;
}

static inline int sodump_add_symbol(
    SoSymTable *table,
    const Elf64_Sym *elf_symbol,
    const char *name,
    int is_dynamic
) {
    unsigned char elf_type = ELF64_ST_TYPE(elf_symbol->st_info);
    unsigned char binding = ELF64_ST_BIND(elf_symbol->st_info);
    unsigned char visibility = ELF64_ST_VISIBILITY(elf_symbol->st_other);

    SoSymbolType type = sodump_symbol_type(elf_type);

    /*
     * Skip section/file/debug symbols. STT_NOTYPE is retained because some
     * assemblers and generated object files use it for usable symbols.
     */
    switch (elf_type) {
        case STT_SECTION:
        case STT_FILE:
            return 1;

        default:
            break;
    }

    if (!name || name[0] == '\0')
        return 1;

    ssize_t duplicate = sodump_find_duplicate(
        table,
        name,
        elf_symbol->st_value,
        elf_symbol->st_shndx,
        type
    );

    if (duplicate >= 0) {
        SoSymbol *old = &table->syms[duplicate];

        /*
         * Replace a dynamic-table entry with the full symbol-table entry.
         */
        if (old->is_dynamic && !is_dynamic) {
            old->addr = elf_symbol->st_value;
            old->size = elf_symbol->st_size;
            old->section_index = elf_symbol->st_shndx;
            old->binding = binding;
            old->visibility = visibility;
            old->type = type;

            old->is_func = type == SO_SYMBOL_FUNC;
            old->is_object = type == SO_SYMBOL_OBJECT;
            old->is_tls = type == SO_SYMBOL_TLS;
            old->is_local = binding == STB_LOCAL;
            old->is_weak = binding == STB_WEAK;
            old->is_defined = elf_symbol->st_shndx != SHN_UNDEF;
            old->is_dynamic = 0;
        }

        return 1;
    }

    if (!sodump_reserve(table, table->count + 1))
        return 0;

    char *copied_name = strdup(name);
    if (!copied_name)
        return 0;

    SoSymbol *out = &table->syms[table->count++];

    memset(out, 0, sizeof(*out));

    out->name = copied_name;
    out->addr = elf_symbol->st_value;
    out->size = elf_symbol->st_size;
    out->section_index = elf_symbol->st_shndx;
    out->binding = binding;
    out->visibility = visibility;
    out->type = type;

    out->is_func = type == SO_SYMBOL_FUNC;
    out->is_object = type == SO_SYMBOL_OBJECT;
    out->is_tls = type == SO_SYMBOL_TLS;
    out->is_local = binding == STB_LOCAL;
    out->is_weak = binding == STB_WEAK;
    out->is_defined = elf_symbol->st_shndx != SHN_UNDEF;
    out->is_dynamic = is_dynamic;

    return 1;
}

static inline int sodump_parse_symbol_section(
    SoSymTable *table,
    const Elf64_Shdr *section_headers,
    size_t section_count,
    const Elf64_Shdr *symbol_section,
    size_t file_size
) {
    if (symbol_section->sh_link >= section_count)
        return 0;

    const Elf64_Shdr *string_section =
        &section_headers[symbol_section->sh_link];

    if (!sodump_range_valid(
            file_size,
            symbol_section->sh_offset,
            symbol_section->sh_size))
        return 0;

    if (!sodump_range_valid(
            file_size,
            string_section->sh_offset,
            string_section->sh_size))
        return 0;

    if (symbol_section->sh_entsize == 0)
        return 0;

    if (symbol_section->sh_entsize < sizeof(Elf64_Sym))
        return 0;

    const uint8_t *base = table->map;
    const char *string_table =
        (const char *)(base + string_section->sh_offset);

    size_t symbol_count =
        symbol_section->sh_size / symbol_section->sh_entsize;

    int is_dynamic = symbol_section->sh_type == SHT_DYNSYM;

    for (size_t i = 0; i < symbol_count; ++i) {
        uint64_t symbol_offset =
            symbol_section->sh_offset +
            i * symbol_section->sh_entsize;

        if (!sodump_range_valid(
                file_size,
                symbol_offset,
                sizeof(Elf64_Sym)))
            return 0;

        const Elf64_Sym *symbol =
            (const Elf64_Sym *)(base + symbol_offset);

        if (symbol->st_name >= string_section->sh_size)
            continue;

        const char *name = string_table + symbol->st_name;

        /*
         * Ensure the name has a terminating NUL inside the string table.
         */
        size_t remaining = string_section->sh_size - symbol->st_name;

        if (!memchr(name, '\0', remaining))
            continue;

        if (!sodump_add_symbol(table, symbol, name, is_dynamic))
            return 0;
    }

    return 1;
}

static inline void sodump_free(SoSymTable *table) {
    if (!table)
        return;

    for (size_t i = 0; i < table->count; ++i)
        free(table->syms[i].name);

    free(table->syms);

    if (table->map && table->map != MAP_FAILED && table->map_len)
        munmap(table->map, table->map_len);

    free(table);
}

static inline SoSymTable *sodump_parse(const char *path) {
    if (!path)
        return NULL;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;

    struct stat st;

    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return NULL;
    }

    size_t file_size = (size_t)st.st_size;

    if (file_size < sizeof(Elf64_Ehdr)) {
        close(fd);
        return NULL;
    }

    void *map = mmap(
        NULL,
        file_size,
        PROT_READ,
        MAP_PRIVATE,
        fd,
        0
    );

    close(fd);

    if (map == MAP_FAILED)
        return NULL;

    const Elf64_Ehdr *header = map;

    if (memcmp(header->e_ident, ELFMAG, SELFMAG) != 0 ||
        header->e_ident[EI_CLASS] != ELFCLASS64 ||
        header->e_ident[EI_DATA] != ELFDATA2LSB ||
        header->e_version != EV_CURRENT ||
        header->e_shentsize < sizeof(Elf64_Shdr)) {
        munmap(map, file_size);
        return NULL;
    }

    if (header->e_shnum == 0 ||
        header->e_shoff == 0 ||
        !sodump_range_valid(
            file_size,
            header->e_shoff,
            (uint64_t)header->e_shnum * header->e_shentsize)) {
        munmap(map, file_size);
        return NULL;
    }

    /*
     * Most normal ELF64 files use sizeof(Elf64_Shdr) as e_shentsize.
     * Reject a different layout rather than indexing it incorrectly.
     */
    if (header->e_shentsize != sizeof(Elf64_Shdr)) {
        munmap(map, file_size);
        return NULL;
    }

    const Elf64_Shdr *section_headers =
        (const Elf64_Shdr *)((const uint8_t *)map + header->e_shoff);

    SoSymTable *table = calloc(1, sizeof(*table));
    if (!table) {
        munmap(map, file_size);
        return NULL;
    }

    table->map = map;
    table->map_len = file_size;

    /*
     * Parse .dynsym first, then .symtab.
     * Entries from .symtab replace matching dynamic entries.
     */
    for (size_t pass = 0; pass < 2; ++pass) {
        uint32_t wanted_type = pass == 0 ? SHT_DYNSYM : SHT_SYMTAB;

        for (size_t i = 0; i < header->e_shnum; ++i) {
            const Elf64_Shdr *section = &section_headers[i];

            if (section->sh_type != wanted_type)
                continue;

            if (!sodump_parse_symbol_section(
                    table,
                    section_headers,
                    header->e_shnum,
                    section,
                    file_size)) {
                sodump_free(table);
                return NULL;
            }
        }
    }

    return table;
}

static inline const char *sodump_type_name(SoSymbolType type) {
    switch (type) {
        case SO_SYMBOL_FUNC:
            return "function";

        case SO_SYMBOL_OBJECT:
            return "object";

        case SO_SYMBOL_TLS:
            return "thread-local";

        default:
            return "unknown";
    }
}

#endif