#include "../../include/cup.h"
//#include <stdbool.h>

#define CCHECK 0
#define DEBUG_LOG 0

enum {
  ANY = 16,
  RAX = 0,
  RCX = 1,
  RDX = 2,
  RBX = 3,
  RSP = 4,
  RBP = 5,
  RSI = 6,
  RDI = 7,
  R8 = 8,
  R9 = 9,
  R10 = 10,
  R11 = 11,
  R12 = 12,
  R13 = 13,
  R14 = 14,
  R15 = 15,
};
typedef uint8_t X64Reg;

#define RETURN_REG RAX

const char* X64Reg_names[] = {
  "RAX",
  "RCX",
  "RDX",
  "RBX",
  "RSP",
  "RBP",
  "RSI",
  "RDI",
  "R8",
  "R9",
  "R10",
  "R11",
  "R12",
  "R13",
  "R14",
  "R15",
  "ERROR"
};
const X64Reg sysv_arg_regs[] = {RDI, RSI, RDX, RCX, R8, R9};
const X64Reg win64_arg_regs[] = {RCX, RDX, R8, R9};

static size_t reg_reserved = 0;



/* ========================================================================
 *                    SCRATCH REGISTER RESERVATION
 * ========================================================================
 * Replaces the old "X64Reg temp_reg = R10; if (treg==R10) temp_reg=R11"
 * dance plus a push/pop around every use. That push/pop scheme was
 * actually sound (symmetric save/restore around each level's own use is
 * correct regardless of nesting depth), just wasteful: every binary op,
 * array element, and index computation paid for a push+pop it didn't
 * need, purely to protect a register nothing else was still using.
 *
 * This is a real reservation instead: reg_pick() hands back a pool
 * register that is neither `treg`/`sreg` (the caller's live operands)
 * nor already reserved by an enclosing reg_pick() call, and marks it
 * reserved. reg_free() releases it. Discipline: whoever calls reg_pick()
 * owns that register until it calls reg_free() on it — a register handed
 * down as a callee's treg/sreg is *borrowed*, never independently freed
 * by the callee. Every site below follows that rule, so no push/pop is
 * needed at all for pool registers.
 *
 * Pool order is R12..R15 before R10/R11: the first four are callee-saved
 * per the SysV ABI, so a well-behaved external function invoked mid-
 * expression (T_CALL/T_AT) won't clobber a reservation living in them,
 * whereas R10/R11 are caller-saved and fair game for any callee to trash.
 * This does NOT make it fully safe — this file's JIT prologue/epilogue
 * never saves/restores R12-R15 (or RBX, which T_EQ already uses
 * unprotected) for ITS OWN caller, and a reservation still spanning an
 * embedded call can in principle be clobbered — just markedly less often
 * than always defaulting to the volatile pair. Full safety needs either
 * spilling live pool registers around every `call` site or callee-saved
 * preservation in the prologue; out of scope here.
 *
 * Only R10-R15 are pooled: RAX/RBX/RSP/RBP/sysv_arg_regs already carry
 * fixed meaning elsewhere in this file (return value, T_EQ's
 * address/value pair, frame pointer, call-argument staging) and must
 * stay untouched by this allocator.
 */
static const X64Reg reg_pool[] = {R12, R13, R14, R15, R10, R11};
#define REG_POOL_N (sizeof(reg_pool) / sizeof(reg_pool[0]))

//static uint16_t reg_reserved = 0; /* bit i set <=> reg_pool[i] is held */

static X64Reg reg_pick(X64Reg avoid_a, X64Reg avoid_b) {
  for (size_t i = 0; i < REG_POOL_N; i++) {
    X64Reg r = reg_pool[i];
    if (r == avoid_a || r == avoid_b)
      continue;
    if (reg_reserved & (1u << i))
      continue;
    reg_reserved |= (1u << i);
    return r;
  }
  cup_error("reg_pick: scratch register pool exhausted");
  exit(1);
}

static void reg_free(X64Reg reg) {
  for (size_t i = 0; i < REG_POOL_N; i++) {
    if (reg_pool[i] == reg) {
      reg_reserved &= ~(1u << i);
      return;
    }
  }
}

#if 1

// Emit REX prefix for 64-bit operations
static void emit_rex(CUPModule *buf, X64Reg reg, X64Reg rm) {
  uint8_t rex = 0x40;
  if (reg & 8)
    rex |= 0x04; // REX.R
  if (rm & 8)
    rex |= 0x01; // REX.B
  if (rex != 0x40)
    emit8(buf, rex);
}

static void emit_rexw(CUPModule* buf, X64Reg reg, X64Reg rm) {
  uint8_t rex = 0x40 | 0x08; // REX.W
  if (reg & 8)
    rex |= 0x04; // REX.R
  if (rm & 8)
    rex |= 0x01; // REX.B
  if (rex != 0x40)
    emit8(buf, rex);
}

#if 0
// REX for 8-bit r/m operands (SETcc, byte MOV). Without *any* REX prefix,
// r/m8 = 4..7 aliases AH/CH/DH/BH instead of SPL/BPL/SIL/DIL — a REX byte
// (even the bare 0x40) is what switches the encoding to the low byte of
// RSP/RBP/RSI/RDI. Must be forced whenever rm is 4..7, not just when the
// REX.R/B bits are needed.
static void emit_rex8(CUPModule *buf, X64Reg reg, X64Reg rm) {
  uint8_t rex = 0x40;
  if (reg & 8)
    rex |= 0x04;
  if (rm & 8)
    rex |= 0x01;
  if (rex != 0x40 || (rm >= RSP && rm <= RDI))
    emit8(buf, rex);
}
#endif

// ModRM byte: [mod(2) | reg(3) | rm(3)]
static void emit_modrm(CUPModule *buf, uint8_t mod, X64Reg reg, X64Reg rm) {
  emit8(buf, (mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

// PUSH reg
static void emit_push_reg(CUPModule *buf, X64Reg reg) {
  if (reg >= 8)
    emit8(buf, 0x41);
  emit8(buf, 0x50 + (reg & 7));
}

// POP reg
static void emit_pop_reg(CUPModule *buf, X64Reg reg) {
  if (reg >= 8)
    emit8(buf, 0x41);
  emit8(buf, 0x58 + (reg & 7));
}

// MOV reg, imm64
static size_t emit_mov_reg_imm64(CUPModule *buf, X64Reg reg, uint64_t imm) {
  emit_rexw(buf, 0, reg);
  emit8(buf, 0xB8 + (reg & 7));
  size_t off = buf->size;
  emit64(buf, imm);
  return off;
}

// MOV reg, imm — shortest correct encoding for constants that are NOT
// going to be patched later (i.e. plain integer literals, not the
// pointer immediates behind CObjRef fixups, which must keep the full
// 10-byte form above so the 8-byte patch slot stays intact).
// mov r32, imm32 zero-extends into the full r64 on x86-64, so it's a
// free 5-byte substitute for any literal that fits unsigned 32 bits.
static size_t emit_mov_reg_imm(CUPModule *buf, X64Reg reg, uint64_t imm) {
#if 0
  if (imm <= UINT32_MAX) {
#if DEBUG_LOG
    printf("OP: mov %s, 0x%08x  (32-bit form)\n", X64Reg_names[reg], (uint32_t)imm);
#endif
    if (reg & 8)
      emit8(buf, 0x41);
    emit8(buf, 0xB8 + (reg & 7));
    size_t off = buf->size;
    emit32(buf, (uint32_t)imm);
    return off;
  }
#endif
  return emit_mov_reg_imm64(buf, reg, imm);
}

// MOV reg, reg
static void emit_mov_reg_reg(CUPModule *buf, X64Reg dst, X64Reg src) {
  emit_rexw(buf, src, dst); // REX.R = src, REX.B = dst
  emit8(buf, 0x89);
  emit_modrm(buf, 3, src, dst);
}

// MOV reg, QWORD PTR [reg]
static void emit_mov_reg_deref(CUPModule *buf, X64Reg dst, X64Reg src) {
  emit_rexw(buf, dst, src); // REX.W = 1, REX.R = dst, REX.B = src
  emit8(buf, 0x8B);              // opcode for MOV r64, r/m64
  emit_modrm(buf, 0, dst, src);  // mod=0 → [rm], reg=dst, rm=src
}

// MOV QWORD PTR [reg], reg
static void emit_mov_deref_reg(CUPModule *buf, X64Reg dst, X64Reg src) {
  emit_rexw(buf, dst, src);
  emit8(buf, 0x89); // MOV r/m64, r64
  emit_modrm(buf, 0, dst, src);
}

// ADD reg, reg
static void emit_add_reg_reg(CUPModule *buf, X64Reg dst, X64Reg src) {
  emit_rexw(buf, src, dst);
  emit8(buf, 0x01);
  emit_modrm(buf, 3, src, dst);
}

// SUB reg, reg
static void emit_sub_reg_reg(CUPModule *buf, X64Reg dst, X64Reg src) {
  emit_rexw(buf, src, dst);
  emit8(buf, 0x29);
  emit_modrm(buf, 3, src, dst);
}

// ADD reg, imm32 (sign-extended)
static void emit_add_reg_imm(CUPModule *buf, X64Reg reg, int32_t imm) {
  emit_rexw(buf, 0, reg);
  emit8(buf, 0x81);
  emit_modrm(buf, 3, 0, reg); // /0 = ADD
  emiti32(buf, imm);
}

#if 0
// SHL reg, imm8
static void emit_shl_reg_imm8(CUPModule *buf, X64Reg reg, uint8_t imm) {
  emit_rexw(buf, 0, reg);
  emit8(buf, 0xC1);
  emit_modrm(buf, 3, 4, reg); // /4 = SHL
  emit8(buf, imm);
}
#endif

// IMUL reg, reg
static void emit_imul_reg_reg(CUPModule *buf, X64Reg dst, X64Reg src) {
  emit_rexw(buf, dst, src);
  emit8(buf, 0x0F);
  emit8(buf, 0xAF);
  emit_modrm(buf, 3, dst, src);
}

// XOR reg, reg
static void emit_xor_reg_reg(CUPModule *buf, X64Reg dst, X64Reg src) {
  emit_rexw(buf, src, dst);
  emit8(buf, 0x31);
  emit_modrm(buf, 3, src, dst);
}

// CMP reg, reg
static void emit_cmp_reg_reg(CUPModule *buf, X64Reg dst, X64Reg src) {
  emit_rexw(buf, src, dst);
  emit8(buf, 0x39);
  emit_modrm(buf, 3, src, dst);
}

// SETE reg (set if equal)
static void emit_sete_reg(CUPModule *buf, X64Reg reg) {
  emit_rex(buf, 0, reg);
  emit8(buf, 0x0F);
  emit8(buf, 0x94);
  emit_modrm(buf, 3, 0, reg);
}

// SETLE reg(set if less or equal)
static void emit_setle_reg(CUPModule *buf, X64Reg reg) {
  emit_rex(buf, 0, reg);
  emit8(buf, 0x0F);
  emit8(buf, 0x9E);
  emit_modrm(buf, 3, 0, reg);
}

// SETL reg(set if less)
static void emit_setl_reg(CUPModule *buf, X64Reg reg) {
  emit_rex(buf, 0, reg);
  emit8(buf, 0x0F);
  emit8(buf, 0x9C);
  emit_modrm(buf, 3, 0, reg);
}

// RET
static void emit_ret(CUPModule *buf) {
  emit8(buf, 0xC3);
}

// CALL reg
static void emit_call_reg(CUPModule *buf, X64Reg reg) {
  if (reg >= 8)
    emit8(buf, 0x41);
  emit8(buf, 0xFF);
  emit_modrm(buf, 3, 2, reg);
}

#if 0
static void emit_jmp_reg(CUPModule *buf, X64Reg reg) {
  if (reg >= 8)
    emit8(buf, 0x41);

  emit8(buf, 0xFF);
  emit_modrm(buf, 3, 4, reg); // /4 = JMP r/m64
}
#endif

// JMP rel32
static size_t emit_jmp_rel32(CUPModule *buf, int32_t rel) {
  emit8(buf, 0xE9);
  size_t off = buf->size;
  emiti32(buf, rel);
  return off;
}

// JE rel32 (jump if equal)
static size_t emit_je_rel32(CUPModule *buf, int32_t rel) {
  emit8(buf, 0x0F);
  emit8(buf, 0x84);
  size_t off = buf->size;
  emiti32(buf, rel);
  return off;
}

// TEST reg, reg
static void emit_test_reg_reg(CUPModule *buf, X64Reg reg1, X64Reg reg2) {
  emit_rexw(buf, reg1, reg2);
  emit8(buf, 0x85);
  emit_modrm(buf, 3, reg1, reg2);
}

// MOVZX r64, r/m8 — zero-extend the low byte of `src` into `dst`.
// Was previously hardcoded as "movzx eax, al": harmless when the SETcc
// target happened to be RAX, silently wrong (clobbers EAX, leaves the
// real target register with garbage upper bits) for any other target —
// which T_LESS/T_LESSEQ/T_EQEQ hit constantly since `target` is whatever
// register the caller passed in, not always RAX.
static void emit_movzx_reg_reg8(CUPModule *buf, X64Reg dst, X64Reg src) {
  emit_rexw(buf, dst, src);
  emit8(buf, 0x0F);
  emit8(buf, 0xB6);
  emit_modrm(buf, 3, dst, src);
}

/*

// JNE rel32 (jump if not equal)
static void emit_jne_rel32(CUPModule *buf, int32_t offset) {
#if DEBUG_LOG
    printf("OP: jne %d\n", offset);
#endif
  emit8(buf, 0x0F);
  emit8(buf, 0x85);
  emit32(buf, offset);
}

// MOV [rbp+offset], reg (store variable)
static void emit_mov_mem_reg(CUPModule *buf, int32_t offset, X64Reg src) {
  emit_rexw(buf, src, RBP);
  emit8(buf, 0x89);
  if (offset >= -128 && offset <= 127) {
    // Use 8-bit displacement
    emit_modrm(buf, 1, src, 5); // [rbp + disp8]
    emit8(buf, offset & 0xFF);
  } else {
    // Use 32-bit displacement
    emit_modrm(buf, 2, src, 5); // [rbp + disp32]
    emit32(buf, offset);
  }
}

// MOV reg, [rbp+offset] (load variable)
static void emit_mov_reg_mem(CUPModule *buf, X64Reg dst, int32_t offset) {
  emit_rexw(buf, dst, RBP);
  emit8(buf, 0x8B);

  if (offset >= -128 && offset <= 127) {
    emit_modrm(buf, 1, dst, 5); // [rbp+disp8]
    emit8(buf, offset & 0xFF);
  } else {
    emit_modrm(buf, 2, dst, 5); // [rbp+disp32]
    emit32(buf, offset);
  }
}
*/

#endif

static void patch_rel(CUPModule *buf, size_t patch, size_t target) {
  int64_t delta = (int64_t)target - (int64_t)(patch + sizeof(int32_t));
  if (delta < INT32_MIN || delta > INT32_MAX) {
    cup_error("relative branch target is out of range");
    exit(1);
  }
  int32_t d = (int32_t)delta;
  memcpy(buf->data + patch, &d, sizeof(d));
}

static inline Node* getNode(NRange *cc) {
  if (cc->it >= cc->end) {
    cup_error("Incorrect Nodes");
    exit(1);
  }
  return cc->it++;
}

static void codegen_op(CUPModule *buf, X64Reg target, X64Reg temp_reg, const CTType type) {
  switch (type) {
  case T_LESS:
    emit_cmp_reg_reg(buf, target, temp_reg);
    emit_setl_reg(buf, target);
    emit_movzx_reg_reg8(buf, target, target);
    break;
  case T_LESSEQ:
    emit_cmp_reg_reg(buf, target, temp_reg);
    emit_setle_reg(buf, target);
    emit_movzx_reg_reg8(buf, target, target);
    break;
  case T_EQEQ:
    emit_cmp_reg_reg(buf, target, temp_reg);
    emit_sete_reg(buf, target);
    emit_movzx_reg_reg8(buf, target, target);
    break;
  case T_MUL:
    emit_imul_reg_reg(buf, target, temp_reg);
    break;
  case T_ADD:
    emit_add_reg_reg(buf, target, temp_reg);
    break;
  case T_SUB:
    emit_sub_reg_reg(buf, target, temp_reg);
    break;
  default:
    cup_error("codegen_op: UN");
    exit(1);
  }
}

static size_t codegen_dot(CUPState* state, CUPModule *buf, CReallocs* reallocs,
  NRange* range, CReallocs* returns, CVariable *var) {
  CVariable* right = NULL;
  if (N_VARIABLE != getNode(range)->token) {
    cup_error("codegen_dot 1");
    exit(1);
  }
  const char* name = getNode(range)->svalue;
  cup_dbgf("codegen_dot %s", name);
  assert(0);
  //CUPGenContext ccc = {state, {}};
  //CMapItem *map_get(const CMap *m, size_t key, CMapItem *prev, size_t k_null);
  //size_t index = ((size_t*)var)[-2];
  //CUP_META meta = {.member.name = name};
  //CUP_GEN_RESULT r = var->typee->metatype.gen(&ccc, CUP_MOP_MEMBER, (void*)var->value, meta);
  cup_dbgf("%p", &state->priv_stream);
  //if (r == CUP_GEN_ERROR) {
  //  //cup_free(right.as.list.values);
  //  cup_errorf("%s: r == CUP_GEN_ERROR", __FILE__);
  //  assert(0);
  //  exit(1);
  //}
  //if (r == CUP_GEN_CONTINUE) {
  //  assert(0);
  //}
  cup_error("T_DOT 4");
  exit(1);
  return 0;
}

size_t codegen_expr(CUPState* state, CUPModule *buf,
  X64Reg treg, X64Reg sreg,
  CReallocs* reallocs,
  NRange* range, CReallocs* returns, CVariable **rvalue) {
  assert(buf != NULL);
  CTType ntype = getNode(range)->token;
  //cup_errorf("codegen node '%s'", token_names[ntype]);
  //size_t value = 0;
  switch (ntype) {
    case N_BLOCK:
      state->scope++;
    case T_COMMA:
      return getNode(range)->value;
    case T_OSB: {
      // Was unscaled (base + raw index, byte addressing) — every array
      // this file builds (see N_ARRAY below) stores size_t-sized
      // (8-byte) elements, so index i must land at base + i*8.
      if (codegen_expr(state, buf, treg, sreg, reallocs, range, returns, NULL)) {
        cup_error("N_OP 1 codegen left is multiple values");
        exit(1);
      }
      X64Reg temp_reg = reg_pick(treg, sreg);
      size_t i = codegen_expr(state, buf, temp_reg, temp_reg, reallocs, range, returns, NULL);
      if (i) {
        cup_error("N_OP 2 codegen right is multiple values");
        exit(1);
      }
      //emit_push_reg(buf, temp_reg);
#if 0
      emit_shl_reg_imm8(buf, temp_reg, 3); // index *= sizeof(size_t)    
#endif
      emit_add_reg_reg(buf, treg, temp_reg);
      emit_mov_reg_deref(buf, treg, sreg);
      reg_free(temp_reg);
      return 0;
    }
    case N_ARRAY: {
      size_t value = getNode(range)->value;
      size_t off = emit_mov_reg_imm64(buf, treg, (uint64_t)getData(state, value));
      (void)off;
      //CObjRef ref = {off, 1, NULL, value};
      //vector_pushT(buf->objrefs.vec, ref);
      size_t count = codegen_expr(state, buf, treg, sreg, reallocs, range, returns, NULL);
      cup_dbgf("N_ARRAY count = %ld", count);
      for (size_t i = 0; i < count; i++) {
        emit_push_reg(buf, treg);
        emit_add_reg_imm(buf, treg, i * sizeof(size_t));
        X64Reg temp_reg = RAX;
        //X64Reg temp_reg = reg_pick(treg, sreg);
        if (codegen_expr(state, buf, temp_reg, temp_reg, reallocs, range, returns, NULL)) {
          cup_error("N_ARRAY ARG");
          exit(1);
        }
        emit_mov_deref_reg(buf, temp_reg, treg);
        emit_pop_reg(buf, treg);
        //reg_free(temp_reg);
      }
      return 0;
    }
    case T_NUMBER: {
      emit_mov_reg_imm(buf, treg, getNode(range)->value);
      return 0;
    }
    case T_STRING: {
      size_t idx = getNode(range)->value;
      size_t off = emit_mov_reg_imm64(buf, treg, (uint64_t)getData(state, idx));
      (void)off;
      //CObjRef ref = {off, 1, NULL, idx};
      //vector_pushT(buf->objrefs.vec, ref);
      return 0;
    }
    case T_THIS: {
      CVariable *var = varLookup(state, "$", state->scope);
      if (var == NULL) {
        assert(0);
        exit(1);
      }
      if (rvalue) *rvalue = var;
      {
        size_t off = emit_mov_reg_imm64(buf, treg, (uint64_t)&(var->value));
        (void)off;
        //CObjRef ref = {off, 0, var, 0};
        //vector_pushT(buf->objrefs.vec, ref);
      }
      if (sreg != ANY) {
        emit_mov_reg_deref(buf, treg, sreg);
      }
      return 0;
    }
    case N_VARIABLE: {
      const char* name = getNode(range)->svalue;
      cup_dbgf("codegen_expr N_VARIABLE %s", name);
      //state->nodes.value = value;
      CVariable *var = varLookup(state, name, state->scope);
      if (var == NULL) {
        assert(0);
        exit(1);
      }
      //CVariable* var = &(state->vars)[value];
      //state->nodes.variable = var;
      cup_dbgf("N_VARIABLE: %zu", var->value);
      if (rvalue) *rvalue = var;
      {
        size_t off = emit_mov_reg_imm64(buf, treg, (uint64_t)&(var->value));
        (void)off;
        //CObjRef ref = {off, 0, var, 0};
        //vector_pushT(buf->objrefs.vec, ref);
      }
      if (sreg != ANY) {
        emit_mov_reg_deref(buf, treg, sreg);
      }
      return 0;
    }
    case T_ADDEQ: {
      size_t i = codegen_expr(state, buf, RBX, ANY, reallocs, range, returns, NULL);
      if (i) {
        cup_error("N_ADDEQ lhs");
        exit(1);
      }
      emit_push_reg(buf, RBX);
      emit_mov_reg_deref(buf, RAX, RBX);
      emit_push_reg(buf, RAX);
      i = codegen_expr(state, buf, RAX, RAX, reallocs, range, returns, NULL);
      if (i) {
        cup_error("N_ADDEQ rhs");
        exit(1);
      }
      emit_pop_reg(buf, RBX);
      emit_add_reg_reg(buf, RBX, RAX);
      emit_pop_reg(buf, RAX);
      emit_mov_deref_reg(buf, RBX, RAX);
      if (treg != RBX)
        emit_mov_reg_reg(buf, treg, RBX);
      break;
    }
    case T_EQ: {
      size_t i = codegen_expr(state, buf, RBX, RBX, reallocs, range, returns, NULL);
      if (i) {
        cup_error("N_ASSIGN 1");
        exit(1);
      }
      if (treg == RBX) {
        emit_push_reg(buf, RAX);
        emit_push_reg(buf, treg);
      }
      i = codegen_expr(state, buf, RAX, ANY, reallocs, range, returns, NULL);
      if (i) {
        cup_error("N_ASSIGN 1");
        exit(1);
      }
      if (treg == RBX) {
        emit_pop_reg(buf, RAX);
        emit_mov_deref_reg(buf, RAX, treg);
        emit_pop_reg(buf, RAX);
      } else {
        emit_mov_deref_reg(buf, RBX, treg);
      }
      break;
    }
    case T_ADD: case T_SUB: case T_MUL: case T_EQEQ:
    case T_LESS:
    case T_LESSEQ: {
      X64Reg temp_reg = R10;
      //printf("add1 treg = %s, sreg = %s\n", X64Reg_names[treg], X64Reg_names[sreg]);
      size_t i = codegen_expr(state, buf, treg, sreg, reallocs, range, returns, NULL);
      if (i) {
        cup_error("N_OP 1 codegen left is multiple values");
        exit(1);
      }
      // TODO: ADD check empty register for temp_reg
      if (treg == R10) {
        temp_reg = R11; // Use R11 if treg is R10 to avoid clobbering
      }
#if DEBUG_LOG
      printf("codegen_op: treg=%s, temp_reg=%s, ntype=%d\n", X64Reg_names[treg], X64Reg_names[temp_reg], ntype);
#endif
      emit_push_reg(buf, temp_reg);
      //emit_push_reg(buf, treg);
      //printf("add2 treg = %s, sreg = %s\n", X64Reg_names[treg], X64Reg_names[sreg]);
      i = codegen_expr(state, buf, temp_reg, temp_reg, reallocs, range, returns, NULL);
      if (i) {
        cup_error("N_OP 2 codegen right is multiple values");
        exit(1);
        //return 1;
      }
      //emit_pop_reg(buf, treg);
      codegen_op(buf, treg, temp_reg, ntype);
      emit_pop_reg(buf, temp_reg);
      return 0;
    }
    case T_DOT: {
      CVariable* var = NULL;
      size_t i = codegen_expr(state, buf, RAX, RAX, reallocs, range, returns, &var);
      if (i) {
        cup_error("T_DOT 1");
        exit(1);
      }
      if (var == NULL || var->typee->cuptype != CUP_TYPE_META) {
        cup_error("T_DOT 2");
        exit(1);
      }
      return codegen_dot(state, buf, reallocs, range, returns, var);
      //emit_push_reg(buf, RAX);
      //size_t count = codegen_expr(state, buf, RAX, RAX, reallocs, range, returns, NULL);
    }
    case T_CALL: {
      // System V AMD64 ABI calling convention
      // Arguments in order: RDI, RSI, RDX, RCX, R8, R9, then stack
      // RAX contains return value
      // Caller-saved: RAX, R10, R11, RDI, RSI, RDX, RCX, R8, R9

      // const CUPType *type = defType(state, buf->it, NULL);
      CVariable* var = NULL;
      size_t i = codegen_expr(state, buf, RAX, RAX, reallocs, range, returns, &var);
      if (i) {
        cup_error("N_CALL 1");
        exit(1);
      }
#if 0
      const CUPType *fn_type = var != NULL ? var->type : NULL;
      if (var == NULL || fn_type == NULL) {
        cup_error("T_CALL: missing function variable");
        exit(1);
      }
      if (fn_type->type == CUP_TYPE_GENERATOR) {

      } else if (fn_type->type == CUP_TYPE_FUNCTION) {
        
      } else {
        char tname[256];
        cup_type_snname(tname, sizeof(tname), fn_type);
        cup_errorf("T_CALL: not Function or Generator get %s", tname);
        exit(1);
      }
#endif
      emit_push_reg(buf, RAX);
      size_t count = codegen_expr(state, buf, RAX, RAX, reallocs, range, returns, NULL);
#if CCHECK
      const CUPType *type = state->nodes.node.variable->type;
      if (type->realtype != CUP_TYPE_GENERATOR) {
        while(type->elements[count + 1]) count++;
        if (count > 6) {
          cup_error("More than 6 arguments not yet supported in JIT");
          count = 6;
        }
        value = codegen_expr(state, buf, RAX, RAX);
        if (value != count) {
          cup_errorf("N_CALL ARGS %ld %ld", value, count);
          exit(1);
        }
#if DEBUG_LOG
        printf("call = %d, %i\n", count, type->data[0]->realtype);
#endif
      } else
        count = codegen_expr(state, buf, RAX, RAX);
#endif
      cup_dbgf("T_CALL count = %ld", count);
      for (size_t i = 0; i < count; i++) {
        if (codegen_expr(state, buf, RAX, RAX, reallocs, range, returns, NULL)) {
            cup_error("N_CALL ARG");
            exit(1);
        }

        emit_push_reg(buf, RAX);
      }
      for (size_t i = count; i-- > 0;) {
        emit_pop_reg(buf, sysv_arg_regs[i]);
      }
      emit_pop_reg(buf, RAX);
      emit_call_reg(buf, RAX);
      if (treg != RAX) {
        emit_mov_reg_reg(buf, treg, RAX);
      }
      return 0;
    }
    case T_AT: {
      size_t size = buf->size;
      CVariable* var = NULL;
      size_t i = codegen_expr(state, buf, RAX, RAX, reallocs, range, returns, &var);
      if (i) {
        cup_error("N_FUNCTION 1");
        exit(1);
      }
      if (var->typee == NULL) {
        cup_error("N_FUNCTION var type is NULL");
        exit(1);
      }
      if (var->typee->cuptype != CUP_TYPE_FUNCTION) {
        cup_error("N_FUNCTION var type is not FUNCTION");
        exit(1);
      }
      state->scope++;
      size_t arg_count = var->typee->size;
      //while (var->typee->elements[arg_count + 1] != &cup_type_void) arg_count++;
      if (arg_count == 0 && arg_count != codegen_expr(state, buf, RAX, ANY, reallocs, range, returns, NULL))
        exit(17);
      buf->size = size;
      codegen_func(state, buf, reallocs, range, &var->value, arg_count);
      return 0;
    }
    case T_IF: {
      size_t i = codegen_expr(state, buf, RAX, RAX, reallocs, range, returns, NULL);
      if (i) {
        cup_error("N_IF 1");
        exit(1);
      }
      emit_test_reg_reg(buf, RAX, RAX);
      size_t patch = emit_je_rel32(buf, 0);
      size_t count = 1;
      for (size_t j = 0; j < count; j++) {
        size_t i = codegen_expr(state, buf, RETURN_REG, ANY, reallocs, range, returns, NULL);
        if (i)
          count += i;
      }
      //patch_rel32(buf, patch, buf->size);
      int64_t delta = (int64_t)buf->size - (int64_t)(patch + sizeof(int32_t));
      if (delta < INT32_MIN || delta > INT32_MAX) {
        cup_error("relative branch target is out of range");
        exit(1);
      }
      int32_t rel = (int32_t)delta;
      memcpy(buf->data + patch, &rel, sizeof(rel));

      return 0;
    }
    case T_WHILE: {
      size_t condition_target = buf->size;
      size_t i = codegen_expr(state, buf, RAX, RAX, reallocs, range, returns, NULL);
      if (i) {
        cup_error("T_WHILE 1");
        exit(1);
      }
      emit_test_reg_reg(buf, RAX, RAX);
      size_t if_patch = emit_je_rel32(buf, 0);
      size_t count = 1;
      for (size_t j = 0; j < count; j++) {
        size_t i = codegen_expr(state, buf, RETURN_REG, ANY, reallocs, range, returns, NULL);
        if (i)
          count += i;
      }
      patch_rel(buf, emit_jmp_rel32(buf, 0), condition_target);
      patch_rel(buf, if_patch, buf->size);
      return 0;
    }
    case T_RETURN: {
      size_t i = codegen_expr(state, buf, RAX, RAX, reallocs, range, returns, NULL);
      if (i) {
        cup_error("N_RETURN 1");
        exit(i);
      }
      CRealloc r = { NULL, emit_jmp_rel32(buf, 0) };
      cup_dbgf("RRRRR - %ld : %ld", state->nodes.value, r.ptr);
      vector_pushT(returns->vec, r);
      return i;
    }
    default:
      cup_errorf("codegen UNKNOWN node '%s'\n", token_names[ntype]);
      exit(1);
  }
  return 0;
}

/*
// left
    size_t save = nodes->pos;
    const CType *type = defType(state, nodes->nodes, &save, nullptr);
    size_t i = codegen_expr(state, nodes, buf, RAX);
    if (i) {
      cup_error(state, "N_CALL 1");
      exit(1);
    }
    // Check if there's a N_COMMA node with arguments
    uint8_t count = type->as.func.args->as.complex.count;
    printf("call = %d, %i\n ", count, type->type);
    if (count > 6) {
      cup_error(state, "More than 6 arguments not yet supported in JIT");
      count = 6;
    }

    emit_push_reg(buf, RAX);
    i = codegen_expr(state, nodes, nullptr, RAX);
    if (i != count) {
      cup_errorf(state, "N_CALL ARGS %ld %ld", i, count);
      exit(1);
    }
    printf("i = %ld\n", i);
    for (i = 0; i < count; i++) {
      if (codegen_expr(state, nodes, buf, sysv_arg_regs[i])) {
        cup_error(state, "N_CALL 1 (REAL)");
        exit(1);
      }
    }
    emit_pop_reg(buf, RAX);

    emit_call_reg(buf, RAX);
    if (target != RAX) {
      emit_mov_reg_reg(buf, target, RAX);
    }
    break;
  }
// right
    size_t save = nodes->pos;
    const CType *type = defType(state, nodes->nodes, &save, nullptr);
    size_t i = codegen_expr(state, nodes, buf, RAX);
    if (i) {
      cup_error(state, "N_CALL 1");
      exit(1);
    }
    // Check if there's a N_COMMA node with arguments
    uint8_t count = type->as.func.args->as.complex.count;
    printf("call = %d, %i\n", count, type->type);
    if (count > 6) {
      cup_error(state, "More than 6 arguments not yet supported in JIT");
      count = 6;
    }

    emit_push_reg(buf, RAX);
    i = codegen_expr(state, nodes, buf, RAX);
    if (i != count) {
      cup_errorf(state, "N_CALL ARGS %ld %ld", i, count);
      exit(1);
    }
    printf("i = %ld\n", i);
    for (i = 0; i < count; i++) {
      if (codegen_expr(state, nodes, buf, sysv_arg_regs[i])) {
        cup_error(state, "N_CALL 1 (REAL)");
        exit(1);
      }
    }
    emit_pop_reg(buf, RAX);

    emit_call_reg(buf, RAX);
    if (target != RAX) {
      emit_mov_reg_reg(buf, target, RAX);
    }
    break;
  }
*/

static void bufmove_dynamic(CUPModule *buf, size_t start, size_t length) {
  if (length == 0 || start == 0)
    return;
  uint8_t *temp = cup_malloc(length);
  memcpy(temp, buf->data + start, length);
  memmove(buf->data + length, buf->data, start);
  memcpy(buf->data, temp, length);
  cup_free(temp);
}

/*
void bufmove256(CUPModule* buf, size_t start, size_t length) {
  uint8_t temp[256];
  memcpy(temp, buf->data + start, length);
  memmove(buf->data + length, buf->data, start);
  memcpy(buf->data, temp, length);
}
void bufmove512(CUPModule* buf, size_t start, size_t length) {
  uint8_t temp[512];
  memcpy(temp, buf->data + start, length);
  memmove(buf->data + length, buf->data, start);
  memcpy(buf->data, temp, length);
}
*/

void codegen_func(CUPState* state, CUPModule* buf, CReallocs* reallocs, NRange* range, size_t *value, size_t arg_count) {
  state->nodes.value = buf->size;
  size_t save = buf->size;
  CRealloc rel = {value, 0};
  vector_pushT(reallocs->vec, rel);
  cup_dbgf("--- %ld %ld %ld ---", save, buf->size, reallocs->size / sizeof(CRealloc));

  emit_push_reg(buf, RBP);
  emit_mov_reg_reg(buf, RBP, RSP);
  emit_xor_reg_reg(buf, RAX, RAX);

  CReallocs returns = {};

  if (arg_count) {
    if (arg_count != codegen_expr(state, buf, RAX, ANY, reallocs, range, &returns, NULL)) {
      exit(17);
    }
    for (size_t j = 0; j < arg_count; j++) {
      codegen_expr(state, buf, RAX, ANY, reallocs, range, &returns, NULL);
      emit_mov_deref_reg(buf, sysv_arg_regs[j], RAX);
    }
  }
  size_t count = 1;
  for (size_t j = 0; j < count; j++) {
    size_t i = codegen_expr(state, buf, RETURN_REG, ANY, reallocs, range, &returns, NULL);
    if (i)
      count += i;
  }

  for (size_t i = returns.size / sizeof(CRealloc); i-- > 0;) {
    patch_rel(buf, returns.data[i].ptr, buf->size);
    cup_dbgf("VVVVV - %ld : %ld & %i : %ld", state->nodes.value, buf->size,
      *(int32_t*)(buf->data + returns.data[i].ptr), returns.data[i].ptr);
  }
  if (returns.data) cup_free(returns.data);

  emit_mov_reg_reg(buf, RSP, RBP);
  emit_pop_reg(buf, RBP);
  emit_ret(buf);
  cup_dbg("---------");

  if (save == 0)
    return;
  size_t n = buf->size - save;
  bufmove_dynamic(buf, save, n);
  size_t rcount = reallocs->size / sizeof(CRealloc);
  cup_dbgf("--- %ld %ld %ld %ld ---", n, save, buf->size, rcount);
  rcount--;
  for (size_t j = 0; j < rcount; j++) {
    reallocs->data[j].ptr += n;
  }
}