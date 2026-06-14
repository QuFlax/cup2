#ifndef LIEF_ELF_SEGMENT_H
#define LIEF_ELF_SEGMENT_H

#include "../../include/section.h" //#include "LIEF/Abstract/Section.hpp"

static constexpr uint64_t PT_BIT = 33;
static constexpr uint64_t PT_OS_BIT = 53;
static constexpr uint64_t PT_MASK = (uint64_t(1) << PT_BIT) - 1;
static constexpr uint64_t PT_ARM = uint64_t(1) << PT_BIT;
static constexpr uint64_t PT_AARCH64 = uint64_t(2) << PT_BIT;
static constexpr uint64_t PT_MIPS = uint64_t(3) << PT_BIT;
static constexpr uint64_t PT_RISCV = uint64_t(4) << PT_BIT;
static constexpr uint64_t PT_IA_64 = uint64_t(5) << PT_BIT;
static constexpr uint64_t PT_HPUX = uint64_t(1) << PT_OS_BIT;

enum TYPE : uint64_t {
    UNKNOWN = uint64_t(-1),
    PT_NULL_ = 0, /**< Unused segment. */
    LOAD = 1,     /**< Loadable segment. */
    DYNAMIC = 2,  /**< Dynamic linking information. */
    INTERP = 3,   /**< Interpreter pathname. */
    NOTE = 4,     /**< Auxiliary information. */
    SHLIB = 5,    /**< Reserved. */
    PHDR = 6,     /**< The program header table itself. */
    TLS = 7,      /**< The thread-local storage template. */

    GNU_EH_FRAME = 0x6474e550,

    GNU_STACK = 0x6474e551,    /**< Indicates stack executability. */
    GNU_PROPERTY = 0x6474e553, /**< GNU property */
    GNU_RELRO = 0x6474e552,    /**< Read-only after relocation. */
    PAX_FLAGS = 0x65041580,

    ARM_ARCHEXT =
        0x70000000 | PT_ARM, /**< Platform architecture compatibility info */
    ARM_EXIDX = 0x70000001 | PT_ARM,

    AARCH64_MEMTAG_MTE = 0x70000002 | PT_AARCH64,

    MIPS_REGINFO = 0x70000000 | PT_MIPS,  /**< Register usage information. */
    MIPS_RTPROC = 0x70000001 | PT_MIPS,   /**< Runtime procedure table. */
    MIPS_OPTIONS = 0x70000002 | PT_MIPS,  /**< Options segment. */
    MIPS_ABIFLAGS = 0x70000003 | PT_MIPS, /**< Abiflags segment. */

    RISCV_ATTRIBUTES = 0x70000003 | PT_RISCV,

    IA_64_EXT = (0x70000000 + 0x0) | PT_IA_64,
    IA_64_UNWIND = (0x70000000 + 0x1) | PT_IA_64,

    HP_TLS = (0x60000000 + 0x0) | PT_HPUX,
    HP_CORE_NONE = (0x60000000 + 0x1) | PT_HPUX,
    HP_CORE_VERSION = (0x60000000 + 0x2) | PT_HPUX,
    HP_CORE_KERNEL = (0x60000000 + 0x3) | PT_HPUX,
    HP_CORE_COMM = (0x60000000 + 0x4) | PT_HPUX,
    HP_CORE_PROC = (0x60000000 + 0x5) | PT_HPUX,
    HP_CORE_LOADABLE = (0x60000000 + 0x6) | PT_HPUX,
    HP_CORE_STACK = (0x60000000 + 0x7) | PT_HPUX,
    HP_CORE_SHM = (0x60000000 + 0x8) | PT_HPUX,
    HP_CORE_MMF = (0x60000000 + 0x9) | PT_HPUX,
    HP_PARALLEL = (0x60000000 + 0x10) | PT_HPUX,
    HP_FASTBIND = (0x60000000 + 0x11) | PT_HPUX,
    HP_OPT_ANNOT = (0x60000000 + 0x12) | PT_HPUX,
    HP_HSL_ANNOT = (0x60000000 + 0x13) | PT_HPUX,
    HP_STACK = (0x60000000 + 0x14) | PT_HPUX,
    HP_CORE_UTSNAME = (0x60000000 + 0x15) | PT_HPUX,
  };

enum ELFSG_FLAGS {
    NONE = 0,
    X = 1,
    W = 2,
    R = 4,
};

struct elfsegment {
  TYPE type_;
  ARCH arch_ = ARCH::NONE;
  uint32_t flags_;
  uint64_t file_offset_;
  uint64_t virtual_address_;
  uint64_t physical_address_;
  uint64_t size_;
  uint64_t virtual_size_;
  uint64_t alignment_;
  uint64_t handler_size_;
  void* sections_; //std::vector<Section*>;
  //DataHandler::Handler* datahandler_ = nullptr;
  //std::vector<uint8_t> content_c_;
};

//LIEF_LOCAL uint64_t handler_size() const;
//span<uint8_t> writable_content();



#endif /* _ELF_SEGMENT_H */