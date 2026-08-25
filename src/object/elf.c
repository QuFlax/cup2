#include <elf.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

enum {
    SYM_NULL = 0,
    SYM_TEXT_SECTION = 1,
    SYM_DATA_SECTION = 2,
    SYM_FIRST_GLOBAL = 3
};

enum {
    SH_NULL, SH_TEXT, SH_DATA, SH_RELA_TEXT, SH_RELA_DATA,
    SH_SYMTAB, SH_STRTAB, SH_SHSTRTAB, SH_COUNT
};

int wtite_64_obj_exe(FILE *f, uint64_t shoff) {
  Elf64_Ehdr eh = {0};
  memcpy(eh.e_ident, ELFMAG, SELFMAG);
  eh.e_ident[EI_CLASS] = ELFCLASS64;
  eh.e_ident[EI_DATA] = ELFDATA2LSB;
  eh.e_ident[EI_VERSION] = EV_CURRENT;
  eh.e_ident[EI_OSABI] = ELFOSABI_SYSV;
  eh.e_type = ET_DYN; /* Object file type */
  eh.e_machine = EM_X86_64; /* Architecture */
  eh.e_version = EV_CURRENT;

  eh.e_entry = 0; /* Entry point virtual address */
  eh.e_phoff = 0;	/* Program header table file offset */
  eh.e_shoff = shoff;		/* Section header table file offset */
  eh.e_flags = 0;		/* Processor-specific flags */

  eh.e_ehsize = sizeof(eh);
  eh.e_phentsize = 0;		/* Program header table entry size */
  eh.e_phnum = 0;		/* Program header table entry count */
  eh.e_shentsize = sizeof(Elf64_Shdr);
  eh.e_shnum = SH_COUNT;
  eh.e_shstrndx = SH_SHSTRTAB;
}

void wtite_64_obj(FILE *f, uint64_t shoff) {
  Elf64_Ehdr eh = {0};
  memcpy(eh.e_ident, ELFMAG, SELFMAG);
  eh.e_ident[EI_CLASS] = ELFCLASS64;
  eh.e_ident[EI_DATA] = ELFDATA2LSB;
  eh.e_ident[EI_VERSION] = EV_CURRENT;
  eh.e_ident[EI_OSABI] = ELFOSABI_SYSV;
  eh.e_type = ET_REL; /* Object file type */
  eh.e_machine = EM_X86_64; /* Architecture */
  eh.e_version = EV_CURRENT;

  eh.e_entry = 0; /* Entry point virtual address */
  eh.e_phoff = 0;
  eh.e_shoff = shoff;	/* Section header table file offset */
  eh.e_flags = 0;		/* Processor-specific flags */
  eh.e_ehsize = sizeof(eh);
  eh.e_phentsize = 0;
  eh.e_phnum = 0;
  eh.e_shentsize = sizeof(Elf64_Shdr);
  eh.e_shnum = SH_COUNT;
  eh.e_shstrndx = SH_SHSTRTAB;
  fwrite(&eh, sizeof(eh), 1, f);
}

int write_64_symtab(FILE *f, uint64_t elf_sym_count, uint32_t *name_offsets, size_t** values) {
  elf_sym_count += SYM_FIRST_GLOBAL;
  
  Elf64_Sym *symtab = cup_malloc(sizeof(*symtab) * elf_sym_count);
  memset(symtab, 0, sizeof(*symtab) * elf_sym_count);

  symtab[SYM_TEXT_SECTION].st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
  symtab[SYM_TEXT_SECTION].st_shndx = SH_TEXT;
  symtab[SYM_DATA_SECTION].st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
  symtab[SYM_DATA_SECTION].st_shndx = SH_DATA;

  for (size_t i = elf_sym_count; (i--) > SYM_FIRST_GLOBAL;) {
    Elf64_Sym *s = &symtab[i];
    s->st_name = name_offsets[i - SYM_FIRST_GLOBAL];
    
    s->st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    s->st_other = STV_DEFAULT;

    s->st_shndx = SHN_UNDEF;
    s->st_value = 0;

    if (values[i - SYM_FIRST_GLOBAL]) {
      s->st_shndx = SH_TEXT;
      s->st_value = *(values[i - SYM_FIRST_GLOBAL]);
    }
  }
  
}