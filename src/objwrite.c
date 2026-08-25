/**
 * @file objwrite.c
 * @brief Emit the last-compiled CUPModule as a relocatable ELF64 (.o).
 */

#include "../include/cup.h"

#define LIELF_IMPLEMENTATION
#include "../include/lielf.h"

#include <stdio.h>

/* Reverse-lookup a CVariable* to its interned name (best-effort). */
static const char *find_var_name(CUPState *state, CVariable *target) {
  const char *name = NULL;
  size_t item_size = offsetof(CMapItem, data) + sizeof(CVariable);
  for (size_t i = 0; i < state->symmap.capacity; i++) {
    CMapItem *item = (CMapItem *)((char*)state->symmap.slots + i * item_size);
    if (item->key == SIZE_MAX) continue;
    if (name) continue;
    if (target == (const CVariable *)item->data) {
      name = getString(state, item->key);
      return name;
    }
    //cb(state, ctx, getString(state, item->key), *(const CVariable *)item->data);
  }
  return name;
}

/* Is `var` one of this module's own @-defined functions? Give its .text offset. */
static int find_internal_offset(CUPModule *m, CVariable *var, size_t *out_offset) {
  CRealloc *r = (CRealloc *)m->reallocs.data;
  size_t n = m->reallocs.size / sizeof(CRealloc);
  for (size_t i = 0; i < n; i++) {
    if (r[i].var == &var->value) {
      *out_offset = r[i].ptr;
      return 1;
    }
  }
  return 0;
}

static size_t sec_index(lielf_binary_t *bin, const char *name) {
  for (size_t i = 0; i < bin->nsections; i++)
    if (strcmp(bin->sections[i].name, name) == 0)
      return i;
  return 0;
}

#ifndef _WIN32
int cup_output_object(CUPState *state, const char *filename) {
  if (state == NULL || filename == NULL) {
    cup_error("cup_output_object: state or filename is NULL");
    return -1;
  }
  if (state->modules == NULL) {
    cup_error("cup_output_object: no compiled module");
    return -1;
  }

  CUPModule *m = &state->modules->module;
  if (m->data == NULL || m->size == 0) {
    cup_error("cup_output_object: module has no generated code");
    return -1;
  }

  lielf_binary_t *bin = lielf_new(LIELF_ET_REL, LIELF_EM_X86_64);
  if (!bin) {
    cup_error("cup_output_object: lielf_new failed");
    return -1;
  }
  size_t data_idx = 0;
  size_t rodata_idx = 0;

  if (!lielf_add_section(bin, ".text", LIELF_SHT_PROGBITS,
        LIELF_SHF_ALLOC | LIELF_SHF_EXECINSTR, m->data, m->size, 16)) {
    cup_error("cup_output_object: failed to add .text");
    lielf_free(bin);
    return -1;
  }
  size_t text_idx = sec_index(bin, ".text");

  int have_rodata = state->data_size > 0;
  if (have_rodata) {
    if (!lielf_add_section(bin, ".rodata", LIELF_SHT_PROGBITS, LIELF_SHF_ALLOC,
          state->data, state->data_size, 8)) {
      cup_error("cup_output_object: failed to add .rodata");
      lielf_free(bin);
      return -1;
    }
    rodata_idx = sec_index(bin, ".rodata");
  }

  size_t nrefs = m->objrefs.size / sizeof(CObjRef);
  CObjRef *refs = (CObjRef *)m->objrefs.data;

  size_t nvarslots = 0;
  for (size_t i = 0; i < nrefs; i++)
    if (refs[i].kind == 0) nvarslots++;

  if (nvarslots) {
    if (!lielf_add_section(bin, ".data", LIELF_SHT_PROGBITS,
          LIELF_SHF_ALLOC | LIELF_SHF_WRITE, NULL, nvarslots * 8, 8)) {
      cup_error("cup_output_object: failed to add .data");
      lielf_free(bin);
      return -1;
    }
    data_idx = sec_index(bin, ".data");
  }

  size_t slot = 0;
  char nm[128];

  for (size_t i = 0; i < nrefs; i++) {
    CObjRef *r = &refs[i];

    if (r->kind == 1) {
      if (!have_rodata) continue;
      snprintf(nm, sizeof(nm), "__cup_ro_%zu", r->data_offset);
      lielf_symbol_t *sym = lielf_find_symbol(bin, nm, 0);
      if (!sym)
        sym = lielf_add_symbol(bin, nm, r->data_offset, 0,
            LIELF_ST_INFO(LIELF_STB_LOCAL, LIELF_STT_OBJECT),
            (uint16_t)rodata_idx, 0);
      if (!sym) { cup_error("cup_output_object: symbol alloc failed"); lielf_free(bin); return -1; }
      int symidx = lielf_symbol_index(bin, sym, 0);
      if (!lielf_add_reloc(bin, ".text", r->offset, (uint32_t)symidx,
            LIELF_R_X86_64_64, 0, 0)) {
        cup_error("cup_output_object: reloc alloc failed");
        lielf_free(bin);
        return -1;
      }
      continue;
    }

    /* kind == 0: variable/function address, indirected through a .data slot,
     * matching how the JIT'd code loads it (address-of, then deref). */
    size_t slot_off = slot * 8;
    slot++;

    snprintf(nm, sizeof(nm), "__cup_slot_%zu", slot_off);
    lielf_symbol_t *slotsym = lielf_add_symbol(bin, nm, slot_off, 8,
        LIELF_ST_INFO(LIELF_STB_LOCAL, LIELF_STT_OBJECT),
        (uint16_t)data_idx, 0);
    if (!slotsym) { cup_error("cup_output_object: symbol alloc failed"); lielf_free(bin); return -1; }
    int slotidx = lielf_symbol_index(bin, slotsym, 0);

    if (!lielf_add_reloc(bin, ".text", r->offset, (uint32_t)slotidx,
          LIELF_R_X86_64_64, 0, 0)) {
      cup_error("cup_output_object: reloc alloc failed");
      lielf_free(bin);
      return -1;
    }

    size_t fn_off;
    lielf_symbol_t *target;
    if (r->var && find_internal_offset(m, r->var, &fn_off)) {
      const char *vname = find_var_name(state, r->var);
      if (vname) snprintf(nm, sizeof(nm), "%s", vname);
      else snprintf(nm, sizeof(nm), "__cup_fn_%zu", fn_off);
      target = lielf_find_symbol(bin, nm, 0);
      if (!target)
        target = lielf_add_symbol(bin, nm, fn_off, 0,
            LIELF_ST_INFO(LIELF_STB_GLOBAL, LIELF_STT_FUNC),
            (uint16_t)text_idx, 0);
    } else {
      const char *vname = r->var ? find_var_name(state, r->var) : NULL;
      if (vname) snprintf(nm, sizeof(nm), "%s", vname);
      else snprintf(nm, sizeof(nm), "__cup_ext_%zu", slot_off);
      target = lielf_find_symbol(bin, nm, 0);
      if (!target)
        target = lielf_add_symbol(bin, nm, 0, 0,
            LIELF_ST_INFO(LIELF_STB_GLOBAL, LIELF_STT_FUNC),
            LIELF_SHN_UNDEF, 0);
    }
    if (!target) { cup_error("cup_output_object: symbol alloc failed"); lielf_free(bin); return -1; }
    int targetidx = lielf_symbol_index(bin, target, 0);

    if (!lielf_add_reloc(bin, ".data", slot_off, (uint32_t)targetidx,
          LIELF_R_X86_64_64, 0, 0)) {
      cup_error("cup_output_object: reloc alloc failed");
      lielf_free(bin);
      return -1;
    }
  }

  int rc = lielf_write(bin, filename);
  lielf_free(bin);
  if (rc != 0) {
    cup_errorf("cup_output_object: failed to write '%s'", filename);
    return -1;
  }
  return 0;
}
#else
int cup_output_object(CUPState *state, const char *filename) {
  (void)state; (void)filename;
  cup_error("cup_output_object: ELF object output is only implemented on Linux/ELF");
  return -1;
}
#endif