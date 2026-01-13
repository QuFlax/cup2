#ifndef CUP_ELF_H
#define CUP_ELF_H

#include <stdint.h>

/* common types */
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef int32_t Elf32_Sword;
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;

enum EI_CLASS : uint8_t {
  ELFCLASSNONE = 0x00,
  ELFCLASS32 = 0x01,
  ELFCLASS64 = 0x02,
};
enum EI_DATA : uint8_t {
  ELFDATANONE = 0x00,
  ELFDATA2LSB = 0x01,
  ELFDATA2MSB = 0x02,
};
// OS ABI identification.
enum {
  ELFOSABI_NONE = 0,           // UNIX System V ABI
  ELFOSABI_HPUX = 1,           // HP-UX operating system
  ELFOSABI_NETBSD = 2,         // NetBSD
  ELFOSABI_GNU = 3,            // GNU/Linux
  ELFOSABI_LINUX = 3,          // Historical alias for ELFOSABI_GNU.
  ELFOSABI_HURD = 4,           // GNU/Hurd
  ELFOSABI_SOLARIS = 6,        // Solaris
  ELFOSABI_AIX = 7,            // AIX
  ELFOSABI_IRIX = 8,           // IRIX
  ELFOSABI_FREEBSD = 9,        // FreeBSD
  ELFOSABI_TRU64 = 10,         // TRU64 UNIX
  ELFOSABI_MODESTO = 11,       // Novell Modesto
  ELFOSABI_OPENBSD = 12,       // OpenBSD
  ELFOSABI_OPENVMS = 13,       // OpenVMS
  ELFOSABI_NSK = 14,           // Hewlett-Packard Non-Stop Kernel
  ELFOSABI_AROS = 15,          // AROS
  ELFOSABI_FENIXOS = 16,       // FenixOS
  ELFOSABI_CLOUDABI = 17,      // Nuxi CloudABI
  ELFOSABI_CUDA = 51,          // NVIDIA CUDA architecture.
  ELFOSABI_CUDA_V2 = 41,       // NVIDIA CUDA architecture.
  ELFOSABI_FIRST_ARCH = 64,    // First architecture-specific OS ABI
  ELFOSABI_AMDGPU_HSA = 64,    // AMD HSA runtime
  ELFOSABI_AMDGPU_PAL = 65,    // AMD PAL runtime
  ELFOSABI_AMDGPU_MESA3D = 66, // AMD GCN GPUs (GFX6+) for MESA runtime
  ELFOSABI_ARM = 97,           // ARM
  ELFOSABI_ARM_FDPIC = 65,     // ARM FDPIC
  ELFOSABI_C6000_ELFABI = 64,  // Bare-metal TMS320C6000
  ELFOSABI_C6000_LINUX = 65,   // Linux TMS320C6000
  ELFOSABI_STANDALONE = 255,   // Standalone (embedded) application
  ELFOSABI_LAST_ARCH = 255     // Last Architecture-specific OS ABI
};
typedef uint8_t EI_OSABI;
typedef uint8_t EI_VERSION;
typedef uint8_t EI_ABIVERSION;
struct E_IDENT {
  unsigned char ei_MAG[4];
  EI_CLASS ei_CLASS;
  EI_DATA ei_DATA;
  EI_VERSION ei_VERSION;
  EI_OSABI ei_OSABI;
  EI_ABIVERSION ei_ABIVERSION;
  uint8_t padding[7];
};

enum E_TYPE : uint16_t {
  ET_NONE = 0x0000,
  ET_REL = 0x0001,
  ET_EXEC = 0x0002,
  ET_DYN = 0x0003,
  ET_CORE = 0x0004,
};
enum E_MACHINE : uint16_t {
  EM_NONE = 0x0000,
  EM_M32 = 0x0001,
  EM_SPARC = 0x0002,
  EM_386 = 0x0003,
  EM_68K = 0x0004,
  EM_88K = 0x0005,
  EM_IAMCU = 0x0006,
  EM_860 = 0x0007,
  EM_MIPS = 0x0008,
  EM_S370 = 0x0009,
  EM_MIPS_RS4_BE = 0x000a,
  EM_PARISC = 0x000f,
  EM_VPP500 = 0x0011,
  EM_SPARC32PLUS = 0x0012,
  EM_960 = 0x0013,
  EM_PPC = 0x0014,
  EM_PPC64 = 0x0015,
  EM_S390 = 0x0016,
  EM_SPU = 0x0017,
  EM_V800 = 0x0024,
  EM_FR20 = 0x0025,
  EM_RH32 = 0x0026,
  EM_RCE = 0x0027,
  EM_ARM = 0x0028,
  EM_ALPHA = 0x0029,
  EM_SH = 0x002A,
  EM_SPARCV9 = 0x002B,
  EM_TRICORE = 0x002C,
  EM_ARC = 0x002D,
  EM_H8_300 = 0x002E,
  EM_H8_300H = 0x002F,
  EM_H8S = 0x0030,
  EM_H8_500 = 0x0031,
  EM_IA_64 = 0x0032,
  EM_MIPS_X = 0x0033,
  EM_COLDFIRE = 0x0034,
  EM_68HC12 = 0x0035,
  EM_MMA = 0x0036,
  EM_PCP = 0x0037,
  EM_NCPU = 0x0038,
  EM_NDR1 = 0x0039,
  EM_STARCORE = 0x003A,
  EM_ME16 = 0x003B,
  EM_ST100 = 0x003C,
  EM_TINYJ = 0x003D,
  EM_X86_64 = 0x003E,
  EM_PDSP = 0x003F,
  EM_PDP10 = 0x0040,
  EM_PDP11 = 0x0041,
  EM_FX66 = 0x0042,
  EM_ST9PLUS = 0x0043,
  EM_ST7 = 0x0044,
  EM_68HC16 = 0x0045,
  EM_68HC11 = 0x0046,
  EM_68HC08 = 0x0047,
  EM_68HC05 = 0x0048,
  EM_SVX = 0x0049,
  EM_ST19 = 0x004A,
  EM_VAX = 0x004B,
  EM_CRIS = 0x004C,
  EM_JAVELIN = 0x004D,
  EM_FIREPATH = 0x004E,
  EM_ZSP = 0x004F,
  EM_MMIX = 0x0050,
  EM_HUANY = 0x0051,
  EM_PRISM = 0x0052,
  EM_AVR = 0x0053,
  EM_FR30 = 0x0054,
  EM_D10V = 0x0055,
  EM_D30V = 0x0056,
  EM_V850 = 0x0057,
  EM_M32R = 0x0058,
  EM_MN10300 = 0x0059,
  EM_MN10200 = 0x005A,
  EM_PJ = 0x005B,
  EM_OPENRISC = 0x005C,
  EM_ARC_COMPACT = 0x005D,
  EM_XTENSA = 0x005E,
  EM_VIDEOCORE = 0x005F,
  EM_TMM_GPP = 0x0060,
  EM_NS32K = 0x0061,
  EM_TPC = 0x0062,
  EM_SNP1K = 0x0063,
  EM_ST200 = 0x0064,
  EM_IP2K = 0x0065,
  EM_MAX = 0x0066,
  EM_CR = 0x0067,
  EM_F2MC16 = 0x0068,
  EM_MSP430 = 0x0069,
  EM_BLACKFIN = 0x006A,
  EM_SE_C33 = 0x006B,
  EM_SEP = 0x006C,
  EM_ARCA = 0x006D,
  EM_UNICORE = 0x006E,
  EM_EXCESS = 0x006F,
  EM_DXP = 0x0070,
  EM_ALTERA_NIOS2 = 0x0071,
  EM_CRX = 0x0072,
  EM_XGATE = 0x0073,
  EM_C166 = 0x0074,
  EM_M16C = 0x0075,
  EM_DSPIC30F = 0x0076,
  EM_CE = 0x0077,
  EM_M32C = 0x0078,
  EM_TSK3000 = 0x0083,
  EM_RS08 = 0x0084,
  EM_SHARC = 0x0085,
  EM_ECOG2 = 0x0086,
  EM_SCORE7 = 0x0087,
  EM_DSP24 = 0x0088,
  EM_VIDEOCORE3 = 0x0089,
  EM_LATTICEMICO32 = 0x008A,
  EM_SE_C17 = 0x008B,
  EM_TI_C6000 = 0x008C,
  EM_TI_C2000 = 0x008D,
  EM_TI_C5500 = 0x008E,
  EM_TI_ARP32 = 0x008F,
  EM_TI_PRU = 0x0090,
  EM_MMDSP_PLUS = 0x00A0,
  EM_CYPRESS_M8C = 0x00A1,
  EM_R32C = 0x00A2,
  EM_TRIMEDIA = 0x00A3,
  EM_QDSP6 = 0x00A4,
  EM_8051 = 0x00A5,
  EM_STXP7X = 0x00A6,
  EM_NDS32 = 0x00A7,
  EM_ECOG1 = 0x00A8,
  EM_ECOG1X = 0x00A8,
  EM_MAXQ30 = 0x00A9,
  EM_XIMO16 = 0x00AA,
  EM_MANIK = 0x00AB,
  EM_CRAYNV2 = 0x00AC,
  EM_RX = 0x00AD,
  EM_METAG = 0x00AE,
  EM_MCST_ELBRUS = 0x00AF,
  EM_ECOG16 = 0x00B0,
  EM_CR16 = 0x00B1,
  EM_ETPU = 0x00B2,
  EM_SLE9X = 0x00B3,
  EM_L10M = 0x00B4,
  EM_K10M = 0x00B5,
  EM_AARCH64 = 0x00B7,
  EM_AVR32 = 0x00B9,
  EM_STM8 = 0x00BA,
  EM_TILE64 = 0x00BB,
  EM_TILEPRO = 0x00BC,
  EM_MICROBLAZE = 0x00BD,
  EM_CUDA = 0x00BE,
  EM_TILEGX = 0x00BF,
  EM_CLOUDSHIELD = 0x00C0,
  EM_COREA_1ST = 0x00C1,
  EM_COREA_2ND = 0x00C2,
  EM_ARC_COMPACT2 = 0x00C3,
  EM_OPEN8 = 0x00C4,
  EM_RL78 = 0x00C5,
  EM_VIDEOCORE5 = 0x00C6,
  EM_78KOR = 0x00C7,
  EM_56800EX = 0x00C8,
  EM_BA1 = 0x00C9,
  EM_BA2 = 0x00CA,
  EM_XCORE = 0x00CB,
  EM_MCHP_PIC = 0x00CC,
  EM_INTEL205 = 0x00CD,
  EM_INTEL206 = 0x00CE,
  EM_INTEL207 = 0x00CF,
  EM_INTEL208 = 0x00D0,
  EM_INTEL209 = 0x00D1,
  EM_KM32 = 0x00D2,
  EM_KMX32 = 0x00D3,
  EM_KMX16 = 0x00D4,
  EM_KMX8 = 0x00D5,
  EM_KVARC = 0x00D6,
  EM_CDP = 0x00D7,
  EM_COGE = 0x00D8,
  EM_COOL = 0x00D9,
  EM_NORC = 0x00DA,
  EM_CSR_KALIMBA = 0x00DB,
  EM_Z80 = 0x00DC,
  EM_VISIUM = 0x00DD,
  EM_FT32 = 0x00DE,
  EM_MOXIE = 0x00DF,
  EM_AMDGPU = 0x00E0,
  EM_RISCV = 0x00F3,
};
enum E_VERSION : uint32_t {
  EV_NONE = 0,
  EV_CURRENT = 1,
};

enum {
  PT_NULL = 0,            // Unused segment.
  PT_LOAD = 1,            // Loadable segment.
  PT_DYNAMIC = 2,         // Dynamic linking information.
  PT_INTERP = 3,          // Interpreter pathname.
  PT_NOTE = 4,            // Auxiliary information.
  PT_SHLIB = 5,           // Reserved.
  PT_PHDR = 6,            // The program header table itself.
  PT_TLS = 7,             // The thread-local storage template.
  PT_LOOS = 0x60000000,   // Lowest operating system-specific pt entry type.
  PT_HIOS = 0x6fffffff,   // Highest operating system-specific pt entry type.
  PT_LOPROC = 0x70000000, // Lowest processor-specific program hdr entry type.
  PT_HIPROC = 0x7fffffff, // Highest processor-specific program hdr entry type.
};

enum : unsigned {
  PF_X = 1,                // Execute
  PF_W = 2,                // Write
  PF_R = 4,                // Read
  PF_MASKOS = 0x0ff00000,  // Bits for operating system-specific semantics.
  PF_MASKPROC = 0xf0000000 // Bits for processor-specific semantics.
};

/* ==== ELF FILE HEADERS ==== */
typedef struct Elf32_Ehdr {
  E_IDENT e_ident;
  E_TYPE e_type;
  E_MACHINE e_machine;
  E_VERSION e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
} Elf32_Ehdr;
typedef struct Elf64_Ehdr {
  E_IDENT e_ident;
  E_TYPE e_type;
  E_MACHINE e_machine;
  E_VERSION e_version;
  Elf64_Addr e_entry;
  Elf64_Off e_phoff;
  Elf64_Off e_shoff;
  Elf64_Word e_flags;
  Elf64_Half e_ehsize;
  Elf64_Half e_phentsize;
  Elf64_Half e_phnum;
  Elf64_Half e_shentsize;
  Elf64_Half e_shnum;
  Elf64_Half e_shstrndx;
} Elf64_Ehdr;
#if __WORDSIZE == 64
typedef Elf64_Ehdr Elf_Ehdr;
#else
typedef Elf32_Ehdr Elf_Ehdr;
#endif

/* ==== ELF PROGRAM HEADERS ==== */
typedef struct Elf32_Phdr {
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
} Elf32_Phdr;
typedef struct Elf64_Phdr {
  Elf64_Word p_type;
  Elf64_Word p_flags;
  Elf64_Off p_offset;
  Elf64_Addr p_vaddr;
  Elf64_Addr p_paddr;
  Elf64_Xword p_filesz;
  Elf64_Xword p_memsz;
  Elf64_Xword p_align;
} Elf64_Phdr;

/* ==== ELF SECTION HEADERS ==== */
typedef struct Elf32_Shdr {
  Elf32_Word sh_name;
  Elf32_Word sh_type;
  Elf32_Word sh_flags;
  Elf32_Addr sh_addr;
  Elf32_Off sh_offset;
  Elf32_Word sh_size;
  Elf32_Word sh_link;
  Elf32_Word sh_info;
  Elf32_Word sh_addralign;
  Elf32_Word sh_entsize;
} Elf32_Shdr;
typedef struct Elf64_Shdr {
  Elf64_Word sh_name;
  Elf64_Word sh_type;
  Elf64_Xword sh_flags;
  Elf64_Addr sh_addr;
  Elf64_Off sh_offset;
  Elf64_Xword sh_size;
  Elf64_Word sh_link;
  Elf64_Word sh_info;
  Elf64_Xword sh_addralign;
  Elf64_Xword sh_entsize;
} Elf64_Shdr;

#define CUP_ELF_IMPLEMENTATION

#include <stdio.h>

#ifdef CUP_ELF_IMPLEMENTATION

static bool is_elf(const E_IDENT id) {
  return id.ei_MAG[0] == 0x7f && id.ei_MAG[1] == 'E' && id.ei_MAG[2] == 'L' &&
         id.ei_MAG[3] == 'F';
}

struct ELF_Symbol {};

#include <string.h>

static const uint8_t ELFMAGIC[4] = {0x7f, 'E', 'L', 'F'};

static void initELFHeader64(Elf64_Ehdr *eh, const E_MACHINE Machine,
                            const EI_CLASS eclass, const EI_DATA endianness) {
  memset(eh, 0, sizeof(*eh));
  // ELF identification.
  memcpy(eh->e_ident.ei_MAG, ELFMAGIC, 4);
  eh->e_ident.ei_CLASS = eclass;
  eh->e_ident.ei_DATA = endianness;
  eh->e_ident.ei_OSABI = ELFOSABI_NONE;
  // Remainder of ELF header.
  eh->e_type = ET_DYN;
  eh->e_machine = Machine;
  eh->e_version = EV_CURRENT;
  eh->e_ehsize = sizeof(Elf64_Ehdr);
  eh->e_phentsize = sizeof(Elf64_Phdr);
  eh->e_shentsize = sizeof(Elf64_Shdr);
}

int write_elf(const char *filename, const E_MACHINE machine) {
  FILE *file = fopen(filename, "wb");
  if (!file)
    return -1;
  Elf64_Ehdr ehdr = {0};
  E_IDENT ident = {{0x7f, 'E', 'L', 'F'}};
  ident.ei_CLASS = ELFCLASS64;
  ident.ei_DATA = ELFDATA2LSB;
  ident.ei_VERSION = EV_CURRENT;
  ident.ei_OSABI = ELFOSABI_NONE;
  ident.ei_ABIVERSION = 0;
  ehdr.e_ident = ident;
  ehdr.e_type = ET_EXEC;
  ehdr.e_machine = machine;
  ehdr.e_version = EV_CURRENT;
  ehdr.e_ehsize = sizeof(Elf64_Ehdr);
  ehdr.e_phentsize = sizeof(Elf64_Phdr);
  ehdr.e_shentsize = sizeof(Elf64_Shdr);
  ehdr.e_phnum = 1;
  ehdr.e_shnum = 1;
  ehdr.e_shstrndx = 1;
  ehdr.e_entry = 0x400000;
  ehdr.e_phoff = sizeof(Elf64_Ehdr);

  Elf64_Phdr ph = {0};
  ph.p_type = PT_LOAD;
  ph.p_flags = PF_R | PF_X;
  ph.p_offset = 0;
  ph.p_vaddr = 0x400000;
  ph.p_paddr = 0x400000;
  ph.p_filesz = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 0;
  ph.p_memsz = ph.p_filesz;
  ph.p_align = 0x200000;

  fwrite(&ehdr, sizeof(ehdr), 1, file);

  fclose(file);
  return 0;
}

#endif /* CUP_ELF_IMPLEMENTATION */
#endif /* CUP_ELF_H */
