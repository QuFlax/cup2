#include "elfbinary.h"

size_t physical_size(const elfsegment* segment, size_t* value) {
  if (value) {
    //if (datahandler_ != NULL) {
    //auto node = datahandler_->get(file_offset(), handler_size(),
    //                              DataHandler::Node::SEGMENT);
    //  if (node) {
    //    node->get().size(value);
    //segment->_.p_filesz = value[0];//   handler_size_ = value;
    //  } else {
    //    LIEF_ERR("Node not found, physical size cannot be updated");
    //  }
    //}
    //segment->_.p_memsz = value[0]; //size_ = value;
  } else {

  }
  return segment->_.p_memsz;
}
size_t handler_size(const elfsegment* segment, size_t* value) {
  if (value) {

  } else {
    if (segment->_.p_filesz > 0) {
      return segment->_.p_filesz;
    }
    return physical_size(segment, value);
  }
}

span content(const elfsegment* segment) {
  //if (datahandler_ == nullptr) {
  //  LIEF_DEBUG("Get content of segment {}@0x{:x} from cache",
  //             to_string(type()), virtual_address());
  //  return content_c_;
  //}

  //segment->_.p_offset; handler_size(segment, NULL); ...
  //auto res = datahandler_->get(file_offset(), handler_size(), DataHandler::Node::SEGMENT);
  //if (!res) {
  //  LIEF_ERR("Can't find the node. The segment's content can't be accessed");
  //  return {};
  //}
  //DataHandler::Node& node = res.value();

  // Create a span based on our values
  //const std::vector<uint8_t>& binary_content = datahandler_->content();
  //const size_t size = binary_content.size();
  //if (node.offset() >= size) {
  //  LIEF_ERR("Can't access content of segment {}:0x{:x}",
  //           to_string(type()), virtual_address());
  //  return {};
  //}

  //const uint8_t* ptr = binary_content.data() + node.offset();

  /* node.size() overflow */
  //if (node.offset() + node.size() < node.offset()) {
  //  return {};
  //}

  //if ((node.offset() + node.size()) >= size) {
  //  if ((node.offset() + handler_size()) <= size) {
  //    return {ptr, static_cast<size_t>(handler_size())};
  //  }
  //  LIEF_ERR("Can't access content of segment {}:0x{:x}",
  //           to_string(type()), virtual_address());
  //  return {};
  //}

  //return {ptr, static_cast<size_t>(node.size())};
  return (span){0,0};
}

void shift_sections(elfbinary* b, uint64_t from, uint64_t shift) {
  elfsection* end = b->sections + b->sections_count;
  for(elfsection* section = b->sections;
    section != end; section++) {
    if (section->is_frame)
      continue;
    if (section->_.sh_offset >= from) {
      section->_.sh_offset += shift;
      if (section->_.sh_addr > 0) {
        section->_.sh_addr += shift;
      } 
    }
  }
}

/*
shift_dynamic_entries
shift_symbols
shift_relocations
*/
void shift_desr(elfbinary* b, uint64_t from, uint64_t shift) {
  if (b->header.e_ident[EI_CLASS] == ELFCLASS32) {
    Elf32_Dyn* it;
    for (size_t i = 0; i < 1; i++)
    {
      switch (it->d_tag) {
      case DT_PLTGOT:
      case DT_HASH:
      case DT_GNU_HASH:
      case DT_STRTAB:
      case DT_SYMTAB:
      case DT_RELA:
      case DT_RELR:
      case DT_REL:
      case DT_JMPREL:
      case DT_INIT:
      case DT_FINI:
      case DT_VERSYM:
      case DT_VERDEF:
      case DT_VERNEED:
      case DT_TLSDESC_PLT:
      case DT_TLSDESC_GOT:
      //case DT_ANDROID_REL:
      //case DT_ANDROID_RELA:
      //case DT_ANDROID_RELR:
      {
        if (it->d_un.d_val >= from) {
          it->d_un.d_val += shift; //entry->value(entry->value() + shift);
        }
        break;
      }

      case DT_INIT_ARRAY:
      case DT_FINI_ARRAY:
      case DT_PREINIT_ARRAY:
      {
        //it->d_un.d_ptr;
        /*DynamicEntryArray::array_t& array =
            entry->as<DynamicEntryArray>()->array();
        for (uint64_t& address : array) {
          if (address >= from) {
            if ((type() == Header::CLASS::ELF32 &&
                 static_cast<int32_t>(address) > 0) ||
                (type() == Header::CLASS::ELF64 &&
                 static_cast<int64_t>(address) > 0))
            {
              address += shift;
            }
          }
        }
        */
        if (it->d_un.d_val >= from) {
          it->d_un.d_val += shift; //entry->value(entry->value() + shift);
        }
        break;
      }
      default:
      {
        // LIEF_DEBUG("{} not supported", to_string(entry->tag()));
      }
      }
    }
  } else {
    Elf64_Dyn* it;
    for (size_t i = 0; i < 1; i++)
    {
      switch (it->d_tag) {
      case DT_PLTGOT:
      case DT_HASH:
      case DT_GNU_HASH:
      case DT_STRTAB:
      case DT_SYMTAB:
      case DT_RELA:
      case DT_RELR:
      case DT_REL:
      case DT_JMPREL:
      case DT_INIT:
      case DT_FINI:
      case DT_VERSYM:
      case DT_VERDEF:
      case DT_VERNEED:
      case DT_TLSDESC_PLT:
      case DT_TLSDESC_GOT:
      //case DT_ANDROID_REL:
      //case DT_ANDROID_RELA:
      //case DT_ANDROID_RELR:
      {
        if (it->d_un.d_val >= from) {
          it->d_un.d_val += shift; //entry->value(entry->value() + shift);
        }
        break;
      }

      case DT_INIT_ARRAY:
      case DT_FINI_ARRAY:
      case DT_PREINIT_ARRAY:
      {
        //it->d_un.d_ptr;
        /*DynamicEntryArray::array_t& array =
            entry->as<DynamicEntryArray>()->array();
        for (uint64_t& address : array) {
          if (address >= from) {
            if ((type() == Header::CLASS::ELF32 &&
                 static_cast<int32_t>(address) > 0) ||
                (type() == Header::CLASS::ELF64 &&
                 static_cast<int64_t>(address) > 0))
            {
              address += shift;
            }
          }
        }
        */
        if (it->d_un.d_val >= from) {
          it->d_un.d_val += shift; //entry->value(entry->value() + shift);
        }
        break;
      }
      default:
      {
        // LIEF_DEBUG("{} not supported", to_string(entry->tag()));
      }
      }
    }
  }
  for (elfsymbol* it = b->symbols;;) {
    if (ELF64_ST_TYPE(it->s.st_info) == STT_TLS)
      continue;
    if (it->s.st_value >= from)
      it->s.st_value += shift;
  }
  switch (b->header.e_machine) {
    //case EM_ARM: patch_relocations<EM_ARM>(from, shift); return;
    //case EM_AARCH64: patch_relocations<EM_AARCH64>(from, shift); return;
    //case EM_X86_64: patch_relocations<EM_X86_64>(from, shift); return;
    //case EM_386: patch_relocations<EM_386>(from, shift); return;
    //case EM_PPC: patch_relocations<EM_PPC>(from, shift); return;
    //case EM_PPC64: patch_relocations<EM_PPC64>(from, shift); return;
    //case EM_RISCV: patch_relocations<EM_RISCV>(from, shift); return;
    //case EM_SH: patch_relocations<EM_SH>(from, shift); return;
    //case EM_S390: patch_relocations<EM_S390>(from, shift); return;
    default:
    {
      //LIEF_DEBUG("Unsupported relocation architecture: {}", to_string(arch));
    }
  }
}

const Elf_Dyn* get(size_t tag) {
  //const auto it_entry =
  //    std::find_if(dynamic_entries_.begin(), dynamic_entries_.end(),
  //                 [tag](const std::unique_ptr<DynamicEntry>& entry) {
  //                   return entry->tag() == tag;
  //                 });
  //if (it_entry == dynamic_entries_.end()) {
  //  return nullptr;
  //}
  //return it_entry->get();
  return NULL;
}

typedef enum {
  VA_ss
} VA_TYPES;

const elfsegment* segment_from_virtual_address(elfbinary* b,uint64_t virtual_address) {
  (void)virtual_address;
  return NULL;
}

const elfsegment* segment_from_virtual_address(elfbinary* b, uint64_t address) {
  for (size_t i = 0; i < b->segments_count; i++) {
    const elfsegment segment = b->segments[i];
    if (segment._.p_vaddr <= address && address < (segment._.p_vaddr + segment._.p_filesz))
    return &b->segments[i];
  }
  return NULL;
}

span get_content_from_virtual_address(elfbinary* b, uint64_t virtual_address, uint64_t size) {
  const elfsegment* segment = segment_from_virtual_address(b, virtual_address);
  if (segment == NULL)
    return (span){0, 0};

  //span<const uint8_t> content = segment->content();
  //const uint64_t offset = virtual_address - segment->virtual_address();
  const uint64_t offset = virtual_address - segment->_.p_vaddr;
  const uint64_t content_size = segment->span.size;
  if (offset >= content_size)
    return (span){0, 0};

  uint64_t checked_size = size;
  if ((offset + checked_size) > content_size) {
    checked_size = checked_size - (offset + checked_size - content_size);
  }
  return (span){offset + segment->span.begin, checked_size};
}

void patch_address(elfbinary* b, uint64_t address, span patch_value) {
  // Object file does not have segments
  if (b->header.e_type == ET_REL) {
    elfsection* section = section_from_offset(b, address);
    if (section == NULL) {
      //LIEF_ERR("No section found at virtual address {:#x}", address);
      return;
    }
    span content = section->span;
    const uint64_t offset = address - section->_.sh_offset;
    if (offset + patch_value.size > content.size) {
      //LIEF_ERR(
      //    "Patch value ({} bytes @{:#x}) exceeds segment bounds (limit: {:#x})",
      //    patch_value.size(), offset, content.size()
      //);
      return;
    }
    memcpy(content.begin + offset, patch_value.begin, patch_value.size);
    //std::copy(patch_value.begin(), patch_value.end(), content.data() + offset);
    return;
  }

  // Find the segment associated with the virtual address
  elfsegment* segment_topatch = segment_from_virtual_address(b, address);
  if (segment_topatch == NULL) {
    //LIEF_ERR("No segment found at virtual address {:#x}", address);
    return;
  }
  const uint64_t offset = address - segment_topatch->_.p_vaddr;
  span content_ref = segment_topatch->span;

  if (offset + patch_value.size > content_ref.size) {
    //LIEF_ERR("Patch value ({} bytes @{:#x}) exceeds segment bounds (limit: {:#x})",
    //         patch_value.size, offset, content_ref.size);
    return;
  }
  memcpy(content_ref.begin + offset, patch_value.begin, patch_value.size);
  //std::copy(patch_value.begin(), patch_value.end(), content_ref.data() + offset);
}

void fix_got_entries(elfbinary* b, uint64_t from, uint64_t shift) {
  size_t size = 3 * ((b->header.e_ident[EI_CLASS] == ELFCLASS64) ? sizeof(Elf64_Addr) : sizeof(Elf32_Addr));
  Elf_Dyn* dt_pltgot = get(DT_PLTGOT);
  if (dt_pltgot == NULL) {
    return;
  }
  const uint64_t addr = dt_pltgot->d_un.d_val;
  span content = get_content_from_virtual_address(b, addr, size);
  if (content.size != size) {
    // LIEF_ERR("Failed to read GOT entries");
    return;
  }

  if (b->header.e_ident[EI_CLASS] == ELFCLASS64) {
    Elf64_Addr* got = (Elf64_Addr*)content.begin;
    if (got[0] > 0 && got[0] > from) // Offset to the dynamic section
      got[0] += shift;
    if (got[1] > 0 && got[1] > from) // Prelinked value (unlikely?)
      got[1] += shift;
  } else {
    Elf32_Addr* got = (Elf32_Addr*)content.begin;
    if (got[0] > 0 && got[0] > from) // Offset to the dynamic section
      got[0] += shift;
    if (got[1] > 0 && got[1] > from) // Prelinked value (unlikely?)
      got[1] += shift;
  }
  patch_address(b, addr, content);
}

uint64_t relocate_phdr_table_pie(elfbinary* b) {
  //if (phdr_reloc_info_.new_offset > 0)
  //  return phdr_reloc_info_.new_offset;

  uint64_t phdr_size = 0;

  if (b->header.e_ident[EI_CLASS] == ELFCLASS32) {
    phdr_size = sizeof(Elf32_Phdr);
  } else {
    phdr_size = sizeof(Elf64_Phdr);
  }

  const uint64_t phdr_offset = b->header.e_phoff;
  const uint64_t from = phdr_offset + phdr_size * b->segments_count;
  size_t shift = 0x1000;

  //phdr_reloc_info_.new_offset  = from;
  //phdr_reloc_info_.nb_segments = shift / phdr_size - header_.numberof_segments();

  //auto alloc = datahandler_->make_hole(from, shift);
  //if (!alloc) {
    //LIEF_ERR("Allocation failed");
    //return 0;
  //}

  //LIEF_DEBUG("Header shift: 0x{:x}", shift);

  //header().section_headers_offset(header().section_headers_offset() + shift);

  shift_sections(b, from, shift);
  shift_segments(b, from, shift);

  // Patch segment size for the segment which contains the new segment
  /*
  for (std::unique_ptr<Segment>& segment : segments_) {
    if (segment->file_offset() <= from &&
        from <= (segment->file_offset() + segment->physical_size()))
    {
      segment->virtual_size(segment->virtual_size()   + shift);
      segment->physical_size(segment->physical_size() + shift);
    }
  }
  */

  shift_desr(b, from, shift); // shift_dynamic_entries
  fix_got_entries(b, from, shift);

  if (b->header.e_entry >= from) {
    b->header.e_entry += shift;
  }
  return phdr_offset;
}