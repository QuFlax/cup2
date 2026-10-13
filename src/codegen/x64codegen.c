#include "../../include/cup.h"

/*
 * x86-64 (System V) JIT code generator.
 *
 * Conventions
 *   codegen_expr(treg, sreg, ...) evaluates one expression subtree into
 *   `treg`.  sreg == ANY selects ADDRESS mode (lvalues yield their address),
 *   anything else selects VALUE mode (lvalues are loaded).
 *
 * Register usage
 *   RAX      result / scratch for statements, zeroed at function entry
 *   R11      callee address of a call (RAX must stay free: AL = #vector regs
 *            for variadic callees)
 *   RDI RSI RDX RCX R8 R9
 *            call arguments.  Leaf arguments are loaded straight into their
 *            argument register after every complex argument was evaluated, so
 *            they cost no push/pop at all.
 *   scratch pool  R10 R9 R8 RSI RDI RCX RDX  (caller-saved)
 *            Only ever held while a *leaf* operand is evaluated, i.e. never
 *            across a call, so they need no saving.
 *   saved pool    R12 R13 R14 R15 RBX        (callee-saved)
 *            Hold a value across the evaluation of a complex subexpression
 *            (which may contain calls).  The prologue stores them in fixed
 *            slots below RBP; the slots of registers a function never uses
 *            are patched to NOPs after the body is generated, and only the
 *            used ones are restored in the epilogue.
 *
 * Operand shapes are decided by peeking at the node stream: immediates are
 * folded into the instruction, leaves use a scratch register, only complex
 * operands go through the saved pool.
 */

#define DEBUG_LOG 0

enum {
  ANY = 16,
  RAX = 0, RCX = 1, RDX = 2, RBX = 3, RSP = 4, RBP = 5, RSI = 6, RDI = 7,
  R8 = 8, R9 = 9, R10 = 10, R11 = 11, R12 = 12, R13 = 13, R14 = 14, R15 = 15,
};
typedef uint8_t X64Reg;

#define RETURN_REG RAX
#define CALLEE_REG R11

const char* X64Reg_names[] = {
  "RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI",
  "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15", "ERROR"
};
const X64Reg sysv_arg_regs[] = {RDI, RSI, RDX, RCX, R8, R9};
#define ARG_REGS_N (sizeof(sysv_arg_regs) / sizeof(sysv_arg_regs[0]))

/* condition codes (low nibble of Jcc/SETcc) */
enum { CC_E = 0x4, CC_NE = 0x5, CC_L = 0xC, CC_GE = 0xD, CC_LE = 0xE, CC_G = 0xF };

/* ========================================================================== */
/*                        PER-FUNCTION GENERATOR STATE                        */
/* ========================================================================== */

typedef struct FnState {
  uint16_t busy;     /* bit r: register r is reserved by pick() */
  uint16_t cs_used;  /* bit r: callee-saved register r was used  */
  uint32_t depth;    /* pushes currently outstanding (stack parity) */
} FnState;

static FnState fn_state;

static const X64Reg pool_scratch[] = {R10, R9, R8, RSI, RDI, RCX, RDX};
static const X64Reg pool_saved[] = {R12, R13, R14, R15, RBX};
#define POOL_SCRATCH_N (sizeof(pool_scratch) / sizeof(pool_scratch[0]))
#define POOL_SAVED_N (sizeof(pool_saved) / sizeof(pool_saved[0]))

static X64Reg reg_pick(const X64Reg* pool, size_t n, X64Reg avoid) {
  for (size_t i = 0; i < n; i++) {
    X64Reg r = pool[i];
    if (r == avoid || (fn_state.busy & (1u << r)))
      continue;
    fn_state.busy |= (1u << r);
    return r;
  }
  cup_error("reg_pick: register pool exhausted");
  exit(1);
}

static X64Reg pick_scratch(X64Reg avoid) {
  return reg_pick(pool_scratch, POOL_SCRATCH_N, avoid);
}

static X64Reg pick_saved(X64Reg avoid) {
  X64Reg r = reg_pick(pool_saved, POOL_SAVED_N, avoid);
  fn_state.cs_used |= (1u << r);
  return r;
}

static void reg_free(X64Reg reg) {
  fn_state.busy &= ~(1u << reg);
}

/* ========================================================================== */
/*                               INSTRUCTIONS                                 */
/* ========================================================================== */

/* REX prefix: W, R (reg field), X (index), B (rm/base). `force` keeps a bare
 * 0x40, needed for byte access to SPL/BPL/SIL/DIL. */
static void emit_rex(CUPModule* buf, int w, X64Reg reg, X64Reg idx, X64Reg rm, int force) {
  uint8_t rex = 0x40 | (w ? 8 : 0) | ((reg & 8) ? 4 : 0) | ((idx & 8) ? 2 : 0) | ((rm & 8) ? 1 : 0);
  if (rex != 0x40 || force)
    emit8(buf, rex);
}

static void emit_modrm_rr(CUPModule* buf, X64Reg reg, X64Reg rm) {
  emit8(buf, 0xC0 | ((reg & 7) << 3) | (rm & 7));
}

/* ModRM (+SIB, +disp) for [base + disp] */
static void emit_modrm_mem(CUPModule* buf, X64Reg reg, X64Reg base, int32_t disp) {
  uint8_t mod = (disp == 0 && (base & 7) != 5) ? 0 : (disp >= -128 && disp <= 127) ? 1 : 2;
  emit8(buf, (mod << 6) | ((reg & 7) << 3) | (base & 7));
  if ((base & 7) == 4) /* RSP / R12 need a SIB byte */
    emit8(buf, 0x24);
  if (mod == 1)
    emit8(buf, (uint8_t)(int8_t)disp);
  else if (mod == 2)
    emiti32(buf, disp);
}

/* 64-bit  op r/m, r   /  op r, r/m  with register-register operands */
static void emit_rr(CUPModule* buf, uint8_t op, X64Reg reg, X64Reg rm) {
  emit_rex(buf, 1, reg, 0, rm, 0);
  emit8(buf, op);
  emit_modrm_rr(buf, reg, rm);
}

static void emit_mov_rr(CUPModule* buf, X64Reg dst, X64Reg src) {
  if (dst != src)
    emit_rr(buf, 0x89, src, dst); /* mov dst, src */
}

/* mov dst, [base + disp] */
static void emit_load(CUPModule* buf, X64Reg dst, X64Reg base, int32_t disp) {
  emit_rex(buf, 1, dst, 0, base, 0);
  emit8(buf, 0x8B);
  emit_modrm_mem(buf, dst, base, disp);
}

/* mov [base + disp], src */
static void emit_store(CUPModule* buf, X64Reg base, int32_t disp, X64Reg src) {
  emit_rex(buf, 1, src, 0, base, 0);
  emit8(buf, 0x89);
  emit_modrm_mem(buf, src, base, disp);
}

/* mov qword [base + disp], simm32 */
static void emit_store_imm32(CUPModule* buf, X64Reg base, int32_t disp, int32_t imm) {
  emit_rex(buf, 1, 0, 0, base, 0);
  emit8(buf, 0xC7);
  emit_modrm_mem(buf, 0, base, disp);
  emiti32(buf, imm);
}

/* add [base + disp], src */
static void emit_add_mem_reg(CUPModule* buf, X64Reg base, int32_t disp, X64Reg src) {
  emit_rex(buf, 1, src, 0, base, 0);
  emit8(buf, 0x01);
  emit_modrm_mem(buf, src, base, disp);
}

/* lea dst, [base + index*8] */
static void emit_lea_idx8(CUPModule* buf, X64Reg dst, X64Reg base, X64Reg index) {
  uint8_t mod = ((base & 7) == 5) ? 1 : 0; /* rbp/r13 base has no disp-less form */
  emit_rex(buf, 1, dst, index, base, 0);
  emit8(buf, 0x8D);
  emit8(buf, (mod << 6) | ((dst & 7) << 3) | 4);
  emit8(buf, (3 << 6) | ((index & 7) << 3) | (base & 7));
  if (mod)
    emit8(buf, 0);
}

/* movabs reg, imm64 — always the full 10-byte form: pointer immediates may be
 * patched/relocated later. */
static size_t emit_mov_imm64(CUPModule* buf, X64Reg reg, uint64_t imm) {
  emit_rex(buf, 1, 0, 0, reg, 0);
  emit8(buf, 0xB8 + (reg & 7));
  size_t off = buf->size;
  emit64(buf, imm);
  return off;
}

/* mov reg, constant — shortest encoding for a plain integer literal */
static void emit_mov_const(CUPModule* buf, X64Reg reg, uint64_t imm) {
  if (imm <= UINT32_MAX) { /* mov r32, imm32 zero-extends */
    emit_rex(buf, 0, 0, 0, reg, 0);
    emit8(buf, 0xB8 + (reg & 7));
    emit32(buf, (uint32_t)imm);
  } else if ((int64_t)imm >= INT32_MIN && (int64_t)imm < 0) { /* sign-extended imm32 */
    emit_rex(buf, 1, 0, 0, reg, 0);
    emit8(buf, 0xC7);
    emit_modrm_rr(buf, 0, reg);
    emiti32(buf, (int32_t)(int64_t)imm);
  } else {
    emit_mov_imm64(buf, reg, imm);
  }
}

/* movabs rax, [addr]  /  movabs [addr], rax */
static void emit_load_rax_abs(CUPModule* buf, uint64_t addr) {
  emit8(buf, 0x48);
  emit8(buf, 0xA1);
  emit64(buf, addr);
}

static void emit_store_rax_abs(CUPModule* buf, uint64_t addr) {
  emit8(buf, 0x48);
  emit8(buf, 0xA3);
  emit64(buf, addr);
}

/* group-1 ALU with immediate: /0 add, /5 sub, /7 cmp */
static void emit_alu_imm(CUPModule* buf, uint8_t ext, X64Reg reg, int32_t imm) {
  emit_rex(buf, 1, 0, 0, reg, 0);
  if (imm >= -128 && imm <= 127) {
    emit8(buf, 0x83);
    emit_modrm_rr(buf, ext, reg);
    emit8(buf, (uint8_t)(int8_t)imm);
  } else {
    emit8(buf, 0x81);
    emit_modrm_rr(buf, ext, reg);
    emiti32(buf, imm);
  }
}

static void emit_add_rr(CUPModule* buf, X64Reg dst, X64Reg src) { emit_rr(buf, 0x01, src, dst); }
static void emit_sub_rr(CUPModule* buf, X64Reg dst, X64Reg src) { emit_rr(buf, 0x29, src, dst); }
static void emit_cmp_rr(CUPModule* buf, X64Reg dst, X64Reg src) { emit_rr(buf, 0x39, src, dst); }
static void emit_test_rr(CUPModule* buf, X64Reg a, X64Reg b) { emit_rr(buf, 0x85, b, a); }
static void emit_xor32_rr(CUPModule* buf, X64Reg dst, X64Reg src) {
  emit_rex(buf, 0, src, 0, dst, 0);
  emit8(buf, 0x31);
  emit_modrm_rr(buf, src, dst);
}

/* imul dst, dst, imm32 */
static void emit_imul_imm(CUPModule* buf, X64Reg reg, int32_t imm) {
  emit_rex(buf, 1, reg, 0, reg, 0);
  emit8(buf, 0x69);
  emit_modrm_rr(buf, reg, reg);
  emiti32(buf, imm);
}

static void emit_imul_rr(CUPModule* buf, X64Reg dst, X64Reg src) {
  emit_rex(buf, 1, dst, 0, src, 0);
  emit8(buf, 0x0F);
  emit8(buf, 0xAF);
  emit_modrm_rr(buf, dst, src);
}

/* shl reg, imm8 */
static void emit_shl_imm(CUPModule* buf, X64Reg reg, uint8_t n) {
  emit_rex(buf, 1, 0, 0, reg, 0);
  emit8(buf, 0xC1);
  emit_modrm_rr(buf, 4, reg);
  emit8(buf, n);
}

/* setcc reg8 ; movzx reg, reg8 */
static void emit_setcc_zx(CUPModule* buf, uint8_t cc, X64Reg reg) {
  emit_rex(buf, 0, 0, 0, reg, reg >= 4); /* REX picks SIL/DIL/.. over DH/BH */
  emit8(buf, 0x0F);
  emit8(buf, 0x90 | cc);
  emit_modrm_rr(buf, 0, reg);
  emit_rex(buf, 1, reg, 0, reg, 0);
  emit8(buf, 0x0F);
  emit8(buf, 0xB6);
  emit_modrm_rr(buf, reg, reg);
}

static void emit_push(CUPModule* buf, X64Reg reg) {
  if (reg >= 8)
    emit8(buf, 0x41);
  emit8(buf, 0x50 + (reg & 7));
  fn_state.depth++;
}

static void emit_pop(CUPModule* buf, X64Reg reg) {
  if (reg >= 8)
    emit8(buf, 0x41);
  emit8(buf, 0x58 + (reg & 7));
  fn_state.depth--;
}

static void emit_call_reg(CUPModule* buf, X64Reg reg) {
  emit_rex(buf, 0, 0, 0, reg, 0);
  emit8(buf, 0xFF);
  emit_modrm_rr(buf, 2, reg);
}

static void emit_ret(CUPModule* buf) {
  emit8(buf, 0xC3);
}

/* sub/add rsp, 8 (stack alignment around a call) */
static void emit_rsp_adjust(CUPModule* buf, int8_t delta) {
  emit_alu_imm(buf, delta < 0 ? 5 : 0, RSP, delta < 0 ? -delta : delta);
}

static size_t emit_jmp_rel32(CUPModule* buf) {
  emit8(buf, 0xE9);
  size_t off = buf->size;
  emiti32(buf, 0);
  return off;
}

static size_t emit_jcc_rel32(CUPModule* buf, uint8_t cc) {
  emit8(buf, 0x0F);
  emit8(buf, 0x80 | cc);
  size_t off = buf->size;
  emiti32(buf, 0);
  return off;
}

static void patch_rel(CUPModule* buf, size_t patch, size_t target) {
  int64_t delta = (int64_t)target - (int64_t)(patch + sizeof(int32_t));
  if (delta < INT32_MIN || delta > INT32_MAX) {
    cup_error("relative branch target is out of range");
    exit(1);
  }
  int32_t d = (int32_t)delta;
  memcpy(buf->data + patch, &d, sizeof(d));
}

/* ========================================================================== */
/*                                NODE STREAM                                 */
/* ========================================================================== */

static inline Node* getNode(NRange* cc) {
  if (cc->it >= cc->end) {
    cup_error("Incorrect Nodes");
    exit(1);
  }
  return cc->it++;
}

static inline const Node* peekNode(const NRange* cc) {
  if (cc->it >= cc->end) {
    cup_error("Incorrect Nodes");
    exit(1);
  }
  return cc->it;
}

/* Number of nodes of a single-node-tree leaf starting at `n`, 0 if `n` starts
 * a compound expression. */
static size_t leaf_size(const NRange* range) {
  if (range->it >= range->end)
    return 0;
  size_t sz;
  switch (range->it->token) {
  case T_NUMBER: case T_STRING: case T_MSTRING: case N_VARIABLE: case N_ADDRESS:
    sz = 2;
    break;
  case T_THIS:
    sz = 1;
    break;
  default:
    return 0;
  }
  return (size_t)(range->end - range->it) >= sz ? sz : 0;
}

/* Immediate foldable into an ALU instruction? (T_NUMBER fitting simm32) */
static int is_small_imm(const NRange* range, int32_t* out) {
  if (range->it + 2 > range->end || range->it->token != T_NUMBER)
    return 0;
  size_t v = range->it[1].value;
  if (v > (size_t)INT32_MAX)
    return 0;
  *out = (int32_t)v;
  return 1;
}

/* Address of a variable-like leaf (N_VARIABLE, T_THIS, N_ADDRESS) at the
 * start of `range`; 0 if it is not one. */
static int leaf_address(CUPState* state, const NRange* range, size_t* addr, CVariable** var) {
  if (range->it >= range->end)
    return 0;
  const Node* n = range->it;
  CVariable* v = NULL;
  switch (n->token) {
  case N_ADDRESS:
    if (range->it + 2 > range->end)
      return 0;
    *addr = n[1].value;
    if (var) *var = NULL;
    return 1;
  case N_VARIABLE:
    if (range->it + 2 > range->end)
      return 0;
    v = varLookup(state, n[1].svalue, state->scope);
    break;
  case T_THIS:
    v = varLookup(state, "$", state->scope);
    break;
  default:
    return 0;
  }
  if (v == NULL) {
    cup_error("codegen: unknown variable");
    exit(1);
  }
  *addr = (size_t)&v->value;
  if (var) *var = v;
  return 1;
}

size_t codegen_expr(CUPState* state, CUPModule* buf, X64Reg treg, X64Reg sreg,
  CReallocs* reallocs, NRange* range, CReallocs* returns, CVariable** rvalue);

#define EXPR(treg, sreg, rv) \
  codegen_expr(state, buf, (treg), (sreg), reallocs, range, returns, (rv))

/* evaluate a single leaf that starts at `leaf` (size `sz`) into `reg` */
static void gen_leaf(CUPState* state, CUPModule* buf, X64Reg reg, const Node* leaf, size_t sz,
  CReallocs* reallocs, CReallocs* returns) {
  NRange tmp = {(Node*)leaf, (Node*)leaf + sz};
  codegen_expr(state, buf, reg, reg, reallocs, &tmp, returns, NULL);
}

static void expect_single(size_t count, const char* what) {
  if (count) {
    cup_errorf("codegen: %s is a multiple-value expression", what);
    exit(1);
  }
}

/* ========================================================================== */
/*                         BINARY OPERATORS / COMPARES                        */
/* ========================================================================== */

static int is_compare(CTType t) {
  return t == T_LESS || t == T_LESSEQ || t == T_EQEQ;
}

static uint8_t compare_cc(CTType t) {
  return t == T_LESS ? CC_L : t == T_LESSEQ ? CC_LE : CC_E;
}

static void gen_alu_imm(CUPModule* buf, CTType t, X64Reg dst, int32_t imm) {
  switch (t) {
  case T_ADD:
    if (imm) emit_alu_imm(buf, 0, dst, imm);
    break;
  case T_SUB:
    if (imm) emit_alu_imm(buf, 5, dst, imm);
    break;
  case T_MUL:
    if (imm == 1) break;
    if (imm > 0 && (imm & (imm - 1)) == 0) {
      uint8_t sh = 0;
      while ((1 << sh) != imm) sh++;
      emit_shl_imm(buf, dst, sh);
    } else {
      emit_imul_imm(buf, dst, imm);
    }
    break;
  default:
    cup_error("codegen: bad immediate operator");
    exit(1);
  }
}

static void gen_alu_rr(CUPModule* buf, CTType t, X64Reg dst, X64Reg src) {
  switch (t) {
  case T_ADD: emit_add_rr(buf, dst, src); break;
  case T_SUB: emit_sub_rr(buf, dst, src); break;
  case T_MUL: emit_imul_rr(buf, dst, src); break;
  default:
    cup_error("codegen: bad operator");
    exit(1);
  }
}

/* Evaluate `left OP right` into treg.  With flags_only (compares) only the
 * flags are produced and the condition code that means "true" is returned,
 * so conditionals can branch without materialising a boolean. */
static uint8_t gen_binary(CUPState* state, CUPModule* buf, CTType nt, X64Reg treg,
  int flags_only, CReallocs* reallocs, NRange* range, CReallocs* returns) {
  expect_single(EXPR(treg, treg, NULL), "left operand");
  int cmp = is_compare(nt);
  X64Reg res = treg; /* register that holds the result of the operation */
  int32_t imm;
  size_t lsz;
  if (is_small_imm(range, &imm)) {
    range->it += 2;
    if (cmp) emit_alu_imm(buf, 7, treg, imm);
    else gen_alu_imm(buf, nt, treg, imm);
  } else if ((lsz = leaf_size(range)) != 0) {
    X64Reg r = pick_scratch(treg);
    gen_leaf(state, buf, r, range->it, lsz, reallocs, returns);
    range->it += lsz;
    if (cmp) emit_cmp_rr(buf, treg, r);
    else gen_alu_rr(buf, nt, treg, r);
    reg_free(r);
  } else {
    /* complex right operand (may call): keep the left value in a saved reg */
    X64Reg l = pick_saved(treg);
    emit_mov_rr(buf, l, treg);
    expect_single(EXPR(treg, treg, NULL), "right operand");
    if (cmp) emit_cmp_rr(buf, l, treg);
    else gen_alu_rr(buf, nt, l, treg);
    res = l;
    reg_free(l);
  }
  if (cmp) {
    if (flags_only)
      return compare_cc(nt);
    emit_setcc_zx(buf, compare_cc(nt), treg);
    return 0;
  }
  emit_mov_rr(buf, treg, res);
  return 0;
}

/* Emit "jump if the condition at the head of `range` is false"; returns the
 * rel32 patch offset. */
static size_t gen_jump_if_false(CUPState* state, CUPModule* buf,
  CReallocs* reallocs, NRange* range, CReallocs* returns) {
  const Node* n = peekNode(range);
  if (is_compare(n->token)) {
    CTType t = n->token;
    range->it++;
    uint8_t cc = gen_binary(state, buf, t, RAX, 1, reallocs, range, returns);
    return emit_jcc_rel32(buf, cc ^ 1);
  }
  expect_single(EXPR(RAX, RAX, NULL), "condition");
  emit_test_rr(buf, RAX, RAX);
  return emit_jcc_rel32(buf, CC_E);
}

/* ========================================================================== */
/*                               EXPRESSIONS                                  */
/* ========================================================================== */

size_t codegen_expr(CUPState* state, CUPModule* buf, X64Reg treg, X64Reg sreg,
  CReallocs* reallocs, NRange* range, CReallocs* returns, CVariable** rvalue) {
  assert(buf != NULL);
  const int want_value = (sreg != ANY);
  CTType ntype = getNode(range)->token;
  switch (ntype) {
  case N_BLOCK:
    state->scope++;
    /* fallthrough */
  case T_COMMA:
    return getNode(range)->value;

  case T_NUMBER:
    emit_mov_const(buf, treg, getNode(range)->value);
    return 0;

  case T_STRING: case T_MSTRING:
    emit_mov_imm64(buf, treg, (uint64_t)getData(state, getNode(range)->value));
    return 0;

  case T_THIS: case N_VARIABLE: case N_ADDRESS: {
    /* leaf_address() wants the token back */
    range->it--;
    size_t addr = 0;
    CVariable* var = NULL;
    if (!leaf_address(state, range, &addr, &var)) {
      cup_error("codegen: bad variable node");
      exit(1);
    }
    range->it += (ntype == T_THIS) ? 1 : 2;
    if (rvalue) *rvalue = var;
    if (want_value && treg == RAX) {
      emit_load_rax_abs(buf, addr);
    } else {
      emit_mov_imm64(buf, treg, addr);
      if (want_value)
        emit_load(buf, treg, treg, 0);
    }
    return 0;
  }

  case N_ARRAY: {
    size_t data = getNode(range)->value;
    const Node* c = peekNode(range);
    if (c->token != T_COMMA) {
      cup_error("codegen: N_ARRAY without element list");
      exit(1);
    }
    range->it++;
    size_t count = getNode(range)->value;
    X64Reg p = pick_saved(treg);
    emit_mov_imm64(buf, p, (uint64_t)getData(state, data));
    for (size_t i = 0; i < count; i++) {
      int32_t disp = (int32_t)(i * sizeof(size_t)), imm;
      if (is_small_imm(range, &imm)) {
        range->it += 2;
        emit_store_imm32(buf, p, disp, imm);
      } else {
        X64Reg e = pick_scratch(p);
        expect_single(EXPR(e, e, NULL), "array element");
        emit_store(buf, p, disp, e);
        reg_free(e);
      }
    }
    emit_mov_rr(buf, treg, p);
    reg_free(p);
    return 0;
  }

  case T_OSB: {
    /* every array slot is a size_t: element i lives at base + i*8 */
    expect_single(EXPR(treg, treg, NULL), "array base");
    int32_t imm;
    size_t lsz;
    if (is_small_imm(range, &imm) && imm <= INT32_MAX / (int32_t)sizeof(size_t)) {
      range->it += 2;
      if (imm)
        emit_alu_imm(buf, 0, treg, imm * (int32_t)sizeof(size_t));
    } else if ((lsz = leaf_size(range)) != 0) {
      X64Reg r = pick_scratch(treg);
      gen_leaf(state, buf, r, range->it, lsz, reallocs, returns);
      range->it += lsz;
      emit_lea_idx8(buf, treg, treg, r);
      reg_free(r);
    } else {
      X64Reg l = pick_saved(treg);
      emit_mov_rr(buf, l, treg);
      expect_single(EXPR(treg, treg, NULL), "array index");
      emit_lea_idx8(buf, treg, l, treg);
      reg_free(l);
    }
    if (want_value)
      emit_load(buf, treg, treg, 0);
    return 0;
  }

  case T_EQ: {
    CVariable* lhs_var = NULL;
    size_t addr;
    if (leaf_address(state, range, &addr, &lhs_var)) {
      /* constant destination: no address register at all when the value is
       * (or can be computed straight into) RAX */
      range->it += (range->it->token == T_THIS) ? 1 : 2;
      expect_single(EXPR(treg, treg, NULL), "assignment value");
      if (treg == RAX) {
        emit_store_rax_abs(buf, addr);
      } else {
        X64Reg a = pick_scratch(treg);
        emit_mov_imm64(buf, a, addr);
        emit_store(buf, a, 0, treg);
        reg_free(a);
      }
      return 0;
    }
    expect_single(EXPR(treg, ANY, NULL), "assignment target");
    size_t lsz;
    if ((lsz = leaf_size(range)) != 0) {
      X64Reg v = pick_scratch(treg);
      gen_leaf(state, buf, v, range->it, lsz, reallocs, returns);
      range->it += lsz;
      emit_store(buf, treg, 0, v);
      emit_mov_rr(buf, treg, v);
      reg_free(v);
    } else {
      X64Reg a = pick_saved(treg);
      emit_mov_rr(buf, a, treg);
      expect_single(EXPR(treg, treg, NULL), "assignment value");
      emit_store(buf, a, 0, treg);
      reg_free(a);
    }
    return 0;
  }

  case T_ADDEQ: {
    expect_single(EXPR(treg, ANY, NULL), "+= target");
    size_t lsz;
    if ((lsz = leaf_size(range)) != 0) {
      X64Reg v = pick_scratch(treg);
      gen_leaf(state, buf, v, range->it, lsz, reallocs, returns);
      range->it += lsz;
      emit_add_mem_reg(buf, treg, 0, v);
      reg_free(v);
      emit_load(buf, treg, treg, 0);
    } else {
      X64Reg a = pick_saved(treg);
      emit_mov_rr(buf, a, treg);
      expect_single(EXPR(treg, treg, NULL), "+= value");
      emit_add_mem_reg(buf, a, 0, treg);
      emit_load(buf, treg, a, 0);
      reg_free(a);
    }
    return 0;
  }

  case T_ADD: case T_SUB: case T_MUL:
  case T_EQEQ: case T_LESS: case T_LESSEQ:
    gen_binary(state, buf, ntype, treg, 0, reallocs, range, returns);
    return 0;

  case T_CALL: {
    /* callee: a leaf is loaded last, into R11, with no stack traffic */
    size_t csz = leaf_size(range);
    const Node* callee = range->it;
    if (csz) {
      range->it += csz;
    } else {
      expect_single(EXPR(RAX, RAX, NULL), "callee");
      emit_push(buf, RAX);
    }
    const Node* c = peekNode(range);
    if (c->token != T_COMMA) {
      cup_error("codegen: call without argument list");
      exit(1);
    }
    range->it++;
    size_t count = getNode(range)->value;
    if (count > ARG_REGS_N) {
      cup_error("More than 6 arguments not yet supported in JIT");
      exit(1);
    }
    const Node* leaf[ARG_REGS_N] = {};
    size_t leaf_sz[ARG_REGS_N] = {};
    for (size_t i = 0; i < count; i++) {
      size_t sz = leaf_size(range);
      if (sz) {
        leaf[i] = range->it;
        leaf_sz[i] = sz;
        range->it += sz;
      } else {
        expect_single(EXPR(RAX, RAX, NULL), "call argument");
        emit_push(buf, RAX);
      }
    }
    for (size_t i = count; i-- > 0;)
      if (!leaf[i])
        emit_pop(buf, sysv_arg_regs[i]);
    for (size_t i = 0; i < count; i++)
      if (leaf[i])
        gen_leaf(state, buf, sysv_arg_regs[i], leaf[i], leaf_sz[i], reallocs, returns);
    if (csz)
      gen_leaf(state, buf, CALLEE_REG, callee, csz, reallocs, returns);
    else
      emit_pop(buf, CALLEE_REG);
    int align = fn_state.depth & 1; /* rsp must be 16-byte aligned at the call */
    if (align)
      emit_rsp_adjust(buf, -8);
    emit_xor32_rr(buf, RAX, RAX); /* AL = 0 vector registers (variadic callees) */
    emit_call_reg(buf, CALLEE_REG);
    if (align)
      emit_rsp_adjust(buf, 8);
    emit_mov_rr(buf, treg, RAX);
    return 0;
  }

  case T_DOT:
    cup_error("codegen: unresolved T_DOT (the resolver must fold member access)");
    exit(1);

  case T_AT: {
    size_t size = buf->size;
    CVariable* var = NULL;
    expect_single(EXPR(RAX, RAX, &var), "function name");
    if (var == NULL || var->typee == NULL) {
      cup_error("N_FUNCTION var type is NULL");
      exit(1);
    }
    if (var->typee->cuptype != CUP_TYPE_FUNCTION) {
      cup_error("N_FUNCTION var type is not FUNCTION");
      exit(1);
    }
    state->scope++;
    size_t arg_count = var->typee->size;
    if (arg_count == 0 && arg_count != EXPR(RAX, ANY, NULL))
      exit(17);
    buf->size = size;
    codegen_func(state, buf, reallocs, range, &var->value, arg_count);
    return 0;
  }

  case T_IF: {
    size_t patch = gen_jump_if_false(state, buf, reallocs, range, returns);
    size_t count = 1;
    for (size_t j = 0; j < count; j++) {
      size_t i = EXPR(RETURN_REG, ANY, NULL);
      if (i)
        count += i;
    }
    patch_rel(buf, patch, buf->size);
    return 0;
  }

  case T_WHILE: {
    size_t condition_target = buf->size;
    size_t exit_patch = gen_jump_if_false(state, buf, reallocs, range, returns);
    size_t count = 1;
    for (size_t j = 0; j < count; j++) {
      size_t i = EXPR(RETURN_REG, ANY, NULL);
      if (i)
        count += i;
    }
    patch_rel(buf, emit_jmp_rel32(buf), condition_target);
    patch_rel(buf, exit_patch, buf->size);
    return 0;
  }

  case T_RETURN: {
    size_t i = EXPR(RAX, RAX, NULL);
    if (i) {
      cup_error("N_RETURN 1");
      exit(i);
    }
    CRealloc r = {NULL, emit_jmp_rel32(buf)};
    vector_pushT(returns->vec, r);
    return 0;
  }

  default:
    cup_errorf("codegen UNKNOWN node '%s'\n", token_names[ntype]);
    exit(1);
  }
  return 0;
}

/* ========================================================================== */
/*                                 FUNCTIONS                                  */
/* ========================================================================== */

static void bufmove_dynamic(CUPModule* buf, size_t start, size_t length) {
  if (length == 0 || start == 0)
    return;
  uint8_t* temp = cup_malloc(length);
  memcpy(temp, buf->data + start, length);
  memmove(buf->data + length, buf->data, start);
  memcpy(buf->data, temp, length);
  cup_free(temp);
}

/* Frame: [rbp-8*k] holds the k-th callee-saved register of
 * CS_SAVE_ORDER; the frame is a fixed 48 bytes so rsp stays 16-byte aligned. */
static const X64Reg CS_SAVE_ORDER[] = {RBX, R12, R13, R14, R15};
#define CS_N (sizeof(CS_SAVE_ORDER) / sizeof(CS_SAVE_ORDER[0]))
#define FRAME_SIZE 48
#define SAVE_SLOT_SIZE 4 /* mov [rbp-d8], r64 is always 4 bytes */

static void emit_nop4(CUPModule* buf, size_t at) {
  static const uint8_t nop4[4] = {0x0F, 0x1F, 0x40, 0x00};
  memcpy(buf->data + at, nop4, 4);
}

void codegen_func(CUPState* state, CUPModule* buf, CReallocs* reallocs, NRange* range, size_t* value, size_t arg_count) {
  FnState outer = fn_state; /* T_AT nests a function inside an expression */
  fn_state = (FnState){};

  state->nodes.value = buf->size;
  size_t save = buf->size;
  CRealloc rel = {value, 0};
  vector_pushT(reallocs->vec, rel);

  /* prologue: push rbp; mov rbp,rsp; sub rsp,FRAME; save slots; xor eax,eax */
  emit_push(buf, RBP);
  fn_state.depth = 0; /* rbp push is the frame, not an outstanding temporary */
  emit_mov_rr(buf, RBP, RSP);
  size_t sub_at = buf->size;
  emit_alu_imm(buf, 5, RSP, FRAME_SIZE);
  size_t slots_at = buf->size;
  for (size_t k = 0; k < CS_N; k++)
    emit_store(buf, RBP, -(int32_t)(8 * (k + 1)), CS_SAVE_ORDER[k]);
  emit_xor32_rr(buf, RAX, RAX);

  CReallocs returns = {};

  if (arg_count) {
    if (arg_count != codegen_expr(state, buf, RAX, ANY, reallocs, range, &returns, NULL)) {
      exit(17);
    }
    for (size_t j = 0; j < arg_count; j++) {
      codegen_expr(state, buf, RAX, ANY, reallocs, range, &returns, NULL);
      emit_store(buf, RAX, 0, sysv_arg_regs[j]);
    }
  }
  size_t count = 1;
  for (size_t j = 0; j < count; j++) {
    size_t i = codegen_expr(state, buf, RETURN_REG, ANY, reallocs, range, &returns, NULL);
    if (i)
      count += i;
  }

  /* only keep the saves of callee-saved registers the body really used */
  size_t used = 0;
  for (size_t k = 0; k < CS_N; k++) {
    if (fn_state.cs_used & (1u << CS_SAVE_ORDER[k]))
      used++;
    else
      emit_nop4(buf, slots_at + k * SAVE_SLOT_SIZE);
  }
  if (!used) /* no frame needed: rsp is already aligned after push rbp */
    emit_nop4(buf, sub_at);

  /* a `return` jumping to the epilogue that directly follows it is a no-op */
  if (returns.size >= sizeof(CRealloc)) {
    CRealloc* last = &returns.data[returns.size / sizeof(CRealloc) - 1];
    if (last->ptr + sizeof(int32_t) == buf->size && buf->size >= 5 &&
        ((uint8_t*)buf->data)[last->ptr - 1] == 0xE9) {
      buf->size -= 5;
      returns.size -= sizeof(CRealloc);
    }
  }
  for (size_t i = returns.size / sizeof(CRealloc); i-- > 0;)
    patch_rel(buf, returns.data[i].ptr, buf->size);
  if (returns.data) cup_free(returns.data);

  for (size_t k = 0; k < CS_N; k++)
    if (fn_state.cs_used & (1u << CS_SAVE_ORDER[k]))
      emit_load(buf, CS_SAVE_ORDER[k], RBP, -(int32_t)(8 * (k + 1)));
  emit8(buf, 0xC9); /* leave */
  emit_ret(buf);
  cup_dbg("---------");

  fn_state = outer;

  if (save == 0)
    return;
  size_t n = buf->size - save;
  bufmove_dynamic(buf, save, n);
  size_t rcount = reallocs->size / sizeof(CRealloc);
  rcount--;
  for (size_t j = 0; j < rcount; j++) {
    reallocs->data[j].ptr += n;
  }
}
