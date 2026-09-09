#include "../../include/cup.h"
#include <stdbool.h>

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
  "R15"
};
const X64Reg sysv_arg_regs[] = {RDI, RSI, RDX, RCX, R8, R9};

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

static uint16_t reg_reserved = 0; /* bit i set <=> reg_pool[i] is held */

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

// ModRM byte: [mod(2) | reg(3) | rm(3)]
static void emit_modrm(CUPModule *buf, uint8_t mod, X64Reg reg, X64Reg rm) {
  emit8(buf, (mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

// PUSH reg
static void emit_push_reg(CUPModule *buf, X64Reg reg) {
#if DEBUG_LOG
    printf("OP: push %s\n", X64Reg_names[reg]);
#endif
  if (reg >= 8)
    emit8(buf, 0x41);
  emit8(buf, 0x50 + (reg & 7));
}

// POP reg
static void emit_pop_reg(CUPModule *buf, X64Reg reg) {
#if DEBUG_LOG
    printf("OP: pop %s\n", X64Reg_names[reg]);
#endif
  if (reg >= 8)
    emit8(buf, 0x41);
  emit8(buf, 0x58 + (reg & 7));
}

// MOV reg, imm64 — full 8-byte immediate slot. Required whenever the
// immediate offset gets patched later (CObjRef / CRealloc pointer fixups
// write a fixed 8-byte value at `off`), so this encoding must not shrink.
static size_t emit_mov_reg_imm64(CUPModule *buf, X64Reg reg, uint64_t imm) {
#if DEBUG_LOG
    printf("OP: mov %s, 0x%016llx\n", X64Reg_names[reg], (unsigned long long)imm);
#endif
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
  return emit_mov_reg_imm64(buf, reg, imm);
}

// MOV reg, reg
static void emit_mov_reg_reg(CUPModule *buf, X64Reg dst, X64Reg src) {
#if DEBUG_LOG
    printf("OP: mov %s, %s\n", X64Reg_names[dst], X64Reg_names[src]);
#endif
  emit_rexw(buf, src, dst); // REX.R = src, REX.B = dst
  emit8(buf, 0x89);
  emit_modrm(buf, 3, src, dst);
}

static void emit_mov_reg_deref(CUPModule *buf, X64Reg dst, X64Reg src) {
#if DEBUG_LOG
    printf("OP: mov %s, [%s]\n", X64Reg_names[dst], X64Reg_names[src]);
#endif
  emit_rexw(buf, dst, src); // REX.W = 1, REX.R = dst, REX.B = src
  emit8(buf, 0x8B);              // opcode for MOV r64, r/m64
  emit_modrm(buf, 0, dst, src);  // mod=0 → [rm], reg=dst, rm=src
}

static void emit_mov_deref_reg(CUPModule *buf, X64Reg dst, X64Reg src) {
#if DEBUG_LOG
    printf("OP: mov [%s], %s\n", X64Reg_names[src], X64Reg_names[dst]);
#endif
  emit_rexw(buf, dst, src);
  emit8(buf, 0x89); // MOV r/m64, r64
  emit_modrm(buf, 0, dst, src);
}

// ADD reg, reg
static void emit_add_reg_reg(CUPModule *buf, X64Reg dst, X64Reg src) {
#if DEBUG_LOG
    printf("OP: add %s, %s\n", X64Reg_names[dst], X64Reg_names[src]);
#endif
  emit_rexw(buf, src, dst);
  emit8(buf, 0x01);
  emit_modrm(buf, 3, src, dst);
}

// SUB reg, reg
static void emit_sub_reg_reg(CUPModule *buf, X64Reg dst, X64Reg src) {
#if DEBUG_LOG
    printf("OP: sub %s, %s\n", X64Reg_names[dst], X64Reg_names[src]);
#endif
  emit_rexw(buf, src, dst);
  emit8(buf, 0x29);
  emit_modrm(buf, 3, src, dst);
}

// ADD reg, imm32 (sign-extended)
static void emit_add_reg_imm(CUPModule *buf, X64Reg reg, int32_t imm) {
#if DEBUG_LOG
    printf("OP: add %s, %d\n", X64Reg_names[reg], imm);
#endif
  emit_rexw(buf, 0, reg);
  emit8(buf, 0x81);
  emit_modrm(buf, 3, 0, reg); // /0 = ADD
  emiti32(buf, imm);
}

// SHL reg, imm8
static void emit_shl_reg_imm8(CUPModule *buf, X64Reg reg, uint8_t imm) {
#if DEBUG_LOG
    printf("OP: shl %s, %u\n", X64Reg_names[reg], imm);
#endif
  emit_rexw(buf, 0, reg);
  emit8(buf, 0xC1);
  emit_modrm(buf, 3, 4, reg); // /4 = SHL
  emit8(buf, imm);
}

// IMUL reg, reg
static void emit_imul_reg_reg(CUPModule *buf, X64Reg dst, X64Reg src) {
#if DEBUG_LOG
    printf("OP: imul %s, %s\n", X64Reg_names[dst], X64Reg_names[src]);
#endif
  emit_rexw(buf, dst, src);
  emit8(buf, 0x0F);
  emit8(buf, 0xAF);
  emit_modrm(buf, 3, dst, src);
}

// XOR reg, reg
static void emit_xor_reg_reg(CUPModule *buf, X64Reg dst, X64Reg src) {
#if DEBUG_LOG
    printf("OP: xor %s, %s\n", X64Reg_names[dst], X64Reg_names[src]);
#endif
  emit_rexw(buf, src, dst);
  emit8(buf, 0x31);
  emit_modrm(buf, 3, src, dst);
}

// CMP reg, reg
static void emit_cmp_reg_reg(CUPModule *buf, X64Reg dst, X64Reg src) {
#if DEBUG_LOG
    printf("OP: cmp %s, %s\n", X64Reg_names[dst], X64Reg_names[src]);
#endif
  emit_rexw(buf, src, dst);
  emit8(buf, 0x39);
  emit_modrm(buf, 3, src, dst);
}

// SETE reg (set if equal)
static void emit_sete_reg(CUPModule *buf, X64Reg reg) {
#if DEBUG_LOG
    printf("OP: sete %s\n", X64Reg_names[reg]);
#endif
  emit_rex8(buf, 0, reg);
  emit8(buf, 0x0F);
  emit8(buf, 0x94);
  emit_modrm(buf, 3, 0, reg);
}

// SETLE reg(set if less or equal)
static void emit_setle_reg(CUPModule *buf, X64Reg reg) {
#if DEBUG_LOG
    printf("OP: setle %s\n", X64Reg_names[reg]);
#endif
  emit_rex8(buf, 0, reg);
  emit8(buf, 0x0F);
  emit8(buf, 0x9E);
  emit_modrm(buf, 3, 0, reg);
}

// SETL reg(set if less)
static void emit_setl_reg(CUPModule *buf, X64Reg reg) {
#if DEBUG_LOG
    printf("OP: setl %s\n", X64Reg_names[reg]);
#endif
  emit_rex8(buf, 0, reg);
  emit8(buf, 0x0F);
  emit8(buf, 0x9C);
  emit_modrm(buf, 3, 0, reg);
}

// RET
static void emit_ret(CUPModule *buf) {
#if DEBUG_LOG
    printf("OP: ret\n");
#endif
  emit8(buf, 0xC3);
}

// CALL reg
static void emit_call_reg(CUPModule *buf, X64Reg reg) {
#if DEBUG_LOG
    printf("OP: call %s\n", X64Reg_names[reg]);
#endif
  if (reg >= 8)
    emit8(buf, 0x41);
  emit8(buf, 0xFF);
  emit_modrm(buf, 3, 2, reg);
}

static void emit_jmp_reg(CUPModule *buf, X64Reg reg) {
  if (reg >= 8)
    emit8(buf, 0x41);

  emit8(buf, 0xFF);
  emit_modrm(buf, 3, 4, reg); // /4 = JMP r/m64
}

// JMP rel32
static void emit_jmp_rel32(CUPModule *buf) {
#if DEBUG_LOG
    printf("OP: jmp rel32\n");
#endif
  emit8(buf, 0xE9);
}

// JE rel32 (jump if equal)
static void emit_je_rel32(CUPModule *buf) {
#if DEBUG_LOG
    printf("OP: je rel32\n");
#endif
  emit8(buf, 0x0F);
  emit8(buf, 0x84);
}

// TEST reg, reg
static void emit_test_reg_reg(CUPModule *buf, X64Reg reg1, X64Reg reg2) {
#if DEBUG_LOG
    printf("OP: test %s, %s\n", X64Reg_names[reg1], X64Reg_names[reg2]);
#endif
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
#if DEBUG_LOG
    printf("OP: movzx %s, %sL\n", X64Reg_names[dst], X64Reg_names[src]);
#endif
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

static int32_t patch_rel(size_t patch, size_t target) {
  int64_t delta = (int64_t)target - (int64_t)(patch + sizeof(int32_t));
  if (delta < INT32_MIN || delta > INT32_MAX) {
    cup_error("relative branch target is out of range");
    exit(1);
  }
  return (int32_t)delta;
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

static inline Node* getNode(NRange *cc) {
  if (cc->it >= cc->end) {
    cup_error("Incorrect Nodes");
    exit(1);
  }
  return cc->it++;
}

size_t codegen_expr(CUPState* state, CUPModule *buf,
  X64Reg treg, X64Reg sreg,
  NRange* range, CVariable **rvalue) {
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
      if (codegen_expr(state, buf, treg, sreg, range, NULL)) {
        cup_error("N_OP 1 codegen left is multiple values");
        exit(1);
      }
      X64Reg temp_reg = reg_pick(treg, sreg);
      size_t i = codegen_expr(state, buf, temp_reg, temp_reg, range, NULL);
      if (i) {
        cup_error("N_OP 2 codegen right is multiple values");
        exit(1);
      }
      emit_shl_reg_imm8(buf, temp_reg, 3); // index *= sizeof(size_t)
      emit_add_reg_reg(buf, treg, temp_reg);
      emit_mov_reg_deref(buf, treg, sreg);
      reg_free(temp_reg);
      return 0;
    }
    case N_ARRAY: {
      // The array literal's own node carries only {token, loc} (same
      // one-slot shape as T_OSB/T_EQ/T_CALL — confirmed by decoding the
      // "garbage" value bytecode dumps print for it: it's a packed
      // Loc{col,line}, a real source position, not a corrupted payload).
      // getNode() below consumes the slot that follows it, which is
      // currently always zero in traces seen so far — reserved but not
      // yet used for anything (perhaps a future element-type/size tag).
      // Consumed and ignored rather than dropped, to keep the node
      // stream aligned for whatever the parser expects to follow.
      (void)getNode(range)->value;

      // Element count comes right after, via the exact same convention
      // T_CALL's argument list uses: a T_COMMA node whose codegen_expr
      // case hands the count back through the C return value (treg/sreg
      // are unused for T_COMMA, so what's passed here doesn't matter).
      //
      // The previous implementation instead read this count slot
      // directly as `getNode(range)->value` — which actually landed on
      // the T_COMMA *token* node itself (not its count payload) — then
      // used that raw number as an index into the string/data pool via
      // getData(), and finally threw away whatever bytes that produced
      // by resetting buf->size = save. Net effect: `a = [123, 91]`
      // compiled to a no-op RHS; `a` never held a real array.
      size_t count = codegen_expr(state, buf, treg, sreg, range, NULL);

      // Build the array on the current function's stack: evaluate each
      // element left-to-right (stream order) and push it. Pushes grow
      // the stack downward, so after all `count` pushes the FIRST
      // element sits at the HIGHEST address — the base (element[0])
      // address is RSP + (count-1)*8, not RSP itself.
      //
      // Lifetime: this storage is reclaimed at function exit — the
      // epilogue resets RSP unconditionally (`mov rsp, rbp`), so an
      // array literal's address is only valid within the function that
      // created it; don't return it or stash it somewhere longer-lived.
      X64Reg elem_reg = reg_pick(treg, sreg);
      for (size_t idx = 0; idx < count; idx++) {
        if (codegen_expr(state, buf, elem_reg, elem_reg, range, NULL)) {
          cup_error("N_ARRAY element");
          exit(1);
        }
        emit_push_reg(buf, elem_reg);
      }
      reg_free(elem_reg);

      emit_mov_reg_reg(buf, treg, RSP);
      if (count > 1)
        emit_add_reg_imm(buf, treg, (int32_t)((count - 1) * sizeof(size_t)));
      if (rvalue)
        *rvalue = NULL; // array literal has no backing CVariable
      return 0;
    }
    case T_NUMBER: {
      emit_mov_reg_imm(buf, treg, getNode(range)->value);
      return 0;
    }
    case T_STRING: {
      size_t idx = getNode(range)->value;
      size_t off = emit_mov_reg_imm64(buf, treg, (uint64_t)getData(state, idx));
      CObjRef ref = {off, 1, NULL, idx};
      vector_pushT(buf->objrefs.vec, ref);
      return 0;
    }
    case N_VARIABLE: {
      size_t value = getNode(range)->value;
      //state->nodes.value = value;
      CVariable *var = getVars(state, value, state->scope);
      if (var == NULL) {
        assert(0);
        exit(1);
      }
      //CVariable* var = &(state->vars)[value];
      //state->nodes.variable = var;
      if (rvalue) *rvalue = var;
      {
        size_t off = emit_mov_reg_imm64(buf, treg, (uint64_t)&(var->value));
        CObjRef ref = {off, 0, var, 0};
        vector_pushT(buf->objrefs.vec, ref);
      }
      if (sreg != ANY) {
        emit_mov_reg_deref(buf, treg, sreg);
      }
      return 0;
    }
    case T_ADDEQ: { 
      size_t i = codegen_expr(state, buf, RBX, ANY, range, NULL);
      if (i) {
        cup_error("N_ADDEQ lhs");
        exit(1);
      }
      emit_push_reg(buf, RBX);
      emit_mov_reg_deref(buf, RAX, RBX);
      emit_push_reg(buf, RAX);
      i = codegen_expr(state, buf, RAX, RAX, range, NULL);
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
      size_t i = codegen_expr(state, buf, RBX, RBX, range, NULL);
      if (i) {
        cup_error("N_ASSIGN 1");
        exit(1);
      }
      if (treg == RBX) {
        emit_push_reg(buf, RAX);
        emit_push_reg(buf, treg);
      }
      i = codegen_expr(state, buf, RAX, ANY, range, NULL);
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
      size_t i = codegen_expr(state, buf, treg, sreg, range, NULL);
      if (i) {
        cup_error("N_OP 1 codegen left is multiple values");
        exit(1);
      }
      X64Reg temp_reg = reg_pick(treg, sreg);
#if DEBUG_LOG
      printf("codegen_op: treg=%s, temp_reg=%s, ntype=%d\n", X64Reg_names[treg], X64Reg_names[temp_reg], ntype);
#endif
      i = codegen_expr(state, buf, temp_reg, temp_reg, range, NULL);
      if (i) {
        cup_error("N_OP 2 codegen right is multiple values");
        exit(1);
      }
      codegen_op(buf, treg, temp_reg, ntype);
      reg_free(temp_reg);
      return 0;
    }
    case T_CALL: {
      // System V AMD64 ABI calling convention
      // Arguments in order: RDI, RSI, RDX, RCX, R8, R9, then stack
      // RAX contains return value
      // Caller-saved: RAX, R10, R11, RDI, RSI, RDX, RCX, R8, R9

      // const CUPType *type = defType(state, buf->it, NULL);
      CVariable* var = NULL;
      size_t i = codegen_expr(state, buf, RAX, RAX, range, &var);
      if (i) {
        cup_error("N_CALL 1");
        exit(1);
      }
      const CUPType *fn_type = var != NULL ? var->type : NULL;
      if (var == NULL || fn_type == NULL) {
        cup_error("T_CALL: missing function variable");
        exit(1);
      }
      if (fn_type->realtype == CUP_TYPE_GENERATOR) {

      } else if (fn_type->realtype == CUP_TYPE_FUNCTION) {
        
      } else {
        char tname[256];
        cup_type_snname(tname, sizeof(tname), fn_type);
        cup_errorf("T_CALL: not Function or Generator get %s", tname);
        exit(1);
      }
      emit_push_reg(buf, RAX);
      size_t count = codegen_expr(state, buf, RAX, RAX, range, NULL);
      for (size_t i = 0; i < count; i++) {
        if (codegen_expr(state, buf, RAX, RAX, range, NULL)) {
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
      size_t i = codegen_expr(state, buf, RAX, RAX, range, &var);
      if (i) {
        cup_error("N_FUNCTION 1");
        exit(1);
      }
      if (var->type == NULL) {
        cup_error("N_FUNCTION var type is NULL");
        exit(1);
      }
      if (var->type->realtype != CUP_TYPE_FUNCTION) {
        cup_error("N_FUNCTION var type is not FUNCTION");
        exit(1);
      }
      state->scope++;
      size_t arg_count = 0;
      while (var->type->elements[arg_count + 1] != &cup_type_void) arg_count++;
      if (arg_count == 0 && arg_count != codegen_expr(state, buf, RAX, ANY, range, NULL))
          exit(17);
      buf->size = size;
      codegen_func(state, buf, range, &var->value, arg_count);
      return 0;
    }
    case T_IF: {
      size_t i = codegen_expr(state, buf, RAX, RAX, range, NULL);
      if (i) {
        cup_error("N_IF 1");
        exit(1);
      }
      emit_test_reg_reg(buf, RAX, RAX);
      emit_je_rel32(buf);
      size_t patch = buf->size;
      emit32(buf, 0);
      size_t count = 1;
      for (size_t j = 0; j < count; j++) {
        size_t i = codegen_expr(state, buf, RETURN_REG, ANY, range, NULL);
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
      size_t i = codegen_expr(state, buf, RAX, RAX, range, NULL);
      if (i) {
        cup_error("T_WHILE 1");
        exit(1);
      }
      emit_test_reg_reg(buf, RAX, RAX);
      emit_je_rel32(buf);
      size_t if_patch = buf->size;
      emit32(buf, 0);

      size_t count = 1;
      for (size_t j = 0; j < count; j++) {
        size_t i = codegen_expr(state, buf, RETURN_REG, ANY, range, NULL);
        if (i)
          count += i;
      }
      emit_jmp_rel32(buf);
      // patch_rel32(buf, back_patch, condition_target);
      int32_t if_pos = patch_rel(buf->size, condition_target);
      emiti32(buf, if_pos);
      //patch_rel32(buf, end_patch, end_target);
      int32_t end_pos = patch_rel(if_patch, buf->size);
      memcpy(buf->data + if_patch, &end_pos, sizeof(end_pos));
      return 0;
    }
    case T_RETURN: {
      size_t i = codegen_expr(state, buf, RAX, RAX, range, NULL);
      if (i) {
        cup_error("N_RETURN 1");
        exit(i);
      }
      emit_jmp_rel32(buf);
#if DEBUG_LOG
      printf("RRRRR - %ld : %ld\n", state->nodes.value, buf->size);
#endif
      CRealloc r = { NULL, buf->size };
      vector_pushT(state->vector_, r);
      emit32(buf, 0);
      return i;
    }
    default:
      cup_errorf("codegen UNKNOWN node '%s'\n", token_names[ntype]);
      exit(1);
  }
  return 0;
}


static void bufmove_dynamic(CUPModule *buf, size_t start, size_t length) {
  if (length == 0 || start == 0)
    return;
  uint8_t *temp = cup_malloc(length);
  memcpy(temp, buf->data + start, length);
  memmove(buf->data + length, buf->data, start);
  memcpy(buf->data, temp, length);
  cup_free(temp);
}


void codegen_func(CUPState* state, CUPModule* buf, NRange* range, size_t *value, size_t arg_count) {
  state->nodes.value = buf->size;
  size_t save = buf->size;
  CRealloc r = {value, 0};
  vector_pushT(buf->reallocs.vec, r);
#if DEBUG_LOG
  printf("\n--- %ld %ld %ld ---\n", save, buf->size, buf->reallocs.size / sizeof(CRealloc));
#endif

  emit_push_reg(buf, RBP);
  emit_mov_reg_reg(buf, RBP, RSP);
  emit_xor_reg_reg(buf, RAX, RAX);

  if (arg_count) {
    if (arg_count != codegen_expr(state, buf, RAX, ANY, range, NULL)) {
      exit(17);
    }
    for (size_t j = 0; j < arg_count; j++) {
      codegen_expr(state, buf, RAX, ANY, range, NULL);
      emit_mov_deref_reg(buf, sysv_arg_regs[j], RAX);
    }
  }
  size_t reljump_begin = 0;
  size_t count = 1;
  for (size_t j = 0; j < count; j++) {
    size_t i = codegen_expr(state, buf, RETURN_REG, ANY, range, NULL);
    if (i)
      count += i;
  }
  size_t epilogue = buf->size;
  size_t reljump_end = state->vector_.size / sizeof(CRealloc);
  for (size_t i = reljump_begin; i < reljump_end; i++) {
    CRealloc r = ((CRealloc*)state->vector_.data)[i];
    int64_t delta = (int64_t)epilogue - (int64_t)(r.ptr + sizeof(int32_t));
    if (delta < INT32_MIN || delta > INT32_MAX) {
      cup_error("return jump out of rel32 range");
      exit(1);
    }
    int32_t rel = (int32_t)delta;
    memcpy(buf->data + r.ptr, &rel, sizeof(rel));
#if DEBUG_LOG
    printf("VVVVV - %ld : %ld & %i : %ld\n", state->nodes.value, buf->size, rel, r.ptr);
#endif
  }
  state->vector_.size = 0;

  emit_mov_reg_reg(buf, RSP, RBP);
  emit_pop_reg(buf, RBP);
  emit_ret(buf);
#if DEBUG_LOG
  printf("---------\n");
#endif

  if (save == 0)
    return;
  size_t n = buf->size - save;
  bufmove_dynamic(buf, save, n);
  size_t rcount = buf->reallocs.size / sizeof(CRealloc);
#if DEBUG_LOG
  printf("\n--- %ld %ld %ld %ld ---\n", n, save, buf->size, rcount);
#endif
  rcount--;
  for (size_t j = 0; j < rcount; j++) {
    buf->reallocs.data[j].ptr += n;
  }
}