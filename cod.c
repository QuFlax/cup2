#include "../include/cup.h"
#include <stdbool.h>

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
  "R15"
};

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

// MOV reg, imm64
static void emit_mov_reg_imm64(CUPModule *buf, X64Reg reg, uint64_t imm) {
#if DEBUG_LOG
    printf("OP: mov %s, 0x%016llx\n", X64Reg_names[reg], (unsigned long long)imm);
#endif
  emit_rexw(buf, 0, reg);
  emit8(buf, 0xB8 + (reg & 7));
  emit64(buf, imm);
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
  emit_rex(buf, 0, reg);
  emit8(buf, 0x0F);
  emit8(buf, 0x94);
  emit_modrm(buf, 3, 0, reg);
}

// SETLE reg(set if less or equal)
static void emit_setle_reg(CUPModule *buf, X64Reg reg) {
#if DEBUG_LOG
    printf("OP: setle %s\n", X64Reg_names[reg]);
#endif
  emit_rex(buf, 0, reg);
  emit8(buf, 0x0F);
  emit8(buf, 0x9E);
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

// JMP rel32. Returns the offset of the rel32 field for later patching.
static size_t emit_jmp_rel32(CUPModule *buf, int32_t offset) {
  emit8(buf, 0xE9);
  size_t patch = buf->size;
  emit32(buf, (uint32_t)offset);
  return patch;
}

// JE rel32. Returns the offset of the rel32 field for later patching.
static size_t emit_je_rel32(CUPModule *buf, int32_t offset) {
  emit8(buf, 0x0F);
  emit8(buf, 0x84);
  size_t patch = buf->size;
  emit32(buf, (uint32_t)offset);
  return patch;
}

static void patch_rel32(CUPModule *buf, size_t patch, size_t target) {
  int64_t delta = (int64_t)target - (int64_t)(patch + sizeof(int32_t));
  if (delta < INT32_MIN || delta > INT32_MAX) {
    cup_error("relative branch target is out of range");
    exit(1);
  }
  int32_t rel = (int32_t)delta;
  memcpy(buf->data + patch, &rel, sizeof(rel));
}

// TEST reg, reg
static void emit_test_reg_reg(CUPModule *buf, X64Reg reg1, X64Reg reg2) {
  emit_rexw(buf, reg1, reg2);
  emit8(buf, 0x85);
  emit_modrm(buf, 3, reg1, reg2);
}

const X64Reg sysv_arg_regs[] = {RDI, RSI, RDX, RCX, R8, R9};

static void codegen_op(CUPModule *buf, X64Reg target, X64Reg temp_reg, const CTType type) {
  switch (type) {
  case T_LESSEQ:
    emit_cmp_reg_reg(buf, target, temp_reg);
    /* MOV does not change flags; clear the full destination so SETcc produces
     * a canonical 64-bit 0/1 instead of preserving stale upper bits. */
    emit_mov_reg_imm64(buf, target, 0);
    emit_setle_reg(buf, target);
    break;
  case T_EQEQ:
    emit_cmp_reg_reg(buf, target, temp_reg);
    emit_mov_reg_imm64(buf, target, 0);
    emit_sete_reg(buf, target);
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
  }
}

typedef struct LoopCodegen {
  size_t continue_target;
  CVector break_patches;
  struct LoopCodegen *parent;
} LoopCodegen;

typedef struct CodegenContext {
  CVector return_patches;
  LoopCodegen *loop;
} CodegenContext;

static size_t codegen_expr_ctx(CUPState *state, CUPModule *buf,
                               X64Reg treg, X64Reg sreg,
                               CodegenContext *ctx);

static void codegen_tree(CUPState *state, CUPModule *buf, X64Reg target,
                         CodegenContext *ctx) {
  CTType token = buf->range.it < buf->range.end ? buf->range.it->token : T_EOF;
  size_t count = codegen_expr_ctx(state, buf, target, target, ctx);
  if (token == N_BLOCK) {
    for (size_t i = 0; i < count; i++)
      codegen_tree(state, buf, target, ctx);
  }
}

static size_t codegen_expr_ctx(CUPState* state, CUPModule *buf, X64Reg treg, X64Reg sreg, CodegenContext *ctx) {
  assert(buf != NULL);
  CTType ntype = getNode(&buf->range)->token;
  //cup_errorf("codegen node '%s'", token_names[ntype]);
  size_t value = 0;
  switch (ntype) {
    case N_BLOCK:
    case T_COMMA:
      return getNode(&buf->range)->value;
    case T_NUMBER: {
      emit_mov_reg_imm64(buf, treg, getNode(&buf->range)->value);
      return 0;
    }
    case T_STRING: {
      emit_mov_reg_imm64(buf, treg, (uint64_t)getData(state, getNode(&buf->range)->value));
      return 0;
    }
    case N_VARIABLE: {
      value = getNode(&buf->range)->value;
      state->nodes.value = value;
      CVariable* var = getVarScoped(state, value, CVARS_MAX);
      //CVariable* var = &(state->vars)[value];
      state->nodes.node.variable = var;
      emit_mov_reg_imm64(buf, treg, (uint64_t)&(var->value));
      if (sreg != ANY) {
        //printf("treg = %s, sreg = %s\n", X64Reg_names[treg], X64Reg_names[sreg]);
        emit_mov_reg_deref(buf, treg, sreg);
      }
      return 0;
    }
    case T_EQ: {
      size_t i = codegen_expr_ctx(state, buf, RBX, RBX, ctx);
      if (i) {
        cup_error("N_ASSIGN 1");
        exit(1);
      }
      if (treg == RBX) {
        emit_push_reg(buf, RAX);
        emit_push_reg(buf, treg);
      }
      i = codegen_expr_ctx(state, buf, RAX, ANY, ctx);
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
    case T_ADD:
    case T_SUB:
    case T_MUL:
    case T_EQEQ:
    case T_LESSEQ: {
      X64Reg temp_reg = R10;
      //printf("add1 treg = %s, sreg = %s\n", X64Reg_names[treg], X64Reg_names[sreg]);
      value = codegen_expr_ctx(state, buf, treg, sreg, ctx);
      if (value) {
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
      value = codegen_expr_ctx(state, buf, temp_reg, temp_reg, ctx);
      if (value) {
        cup_error("N_OP 2 codegen right is multiple values");
        exit(1);
        //return 1;
      }
      //emit_pop_reg(buf, treg);
      codegen_op(buf, treg, temp_reg, ntype);
      emit_pop_reg(buf, temp_reg);
      return 0;
    }
    case T_CALL: {
      // System V AMD64 ABI calling convention
      // Arguments in order: RDI, RSI, RDX, RCX, R8, R9, then stack
      // RAX contains return value
      // Caller-saved: RAX, R10, R11, RDI, RSI, RDX, RCX, R8, R9

      // const CUPType *type = defType(state, buf->it, NULL);
      value = codegen_expr_ctx(state, buf, RAX, RAX, ctx);
      if (value) {
        cup_error("N_CALL 1");
        exit(1);
      }
      emit_push_reg(buf, RAX);
      size_t count = 0;
#if CCHECK
      const CUPType *type = state->nodes.node.variable->type;
      if (type->realtype != CUP_TYPE_GENERATOR) {
        while(type->elements[count + 1]) count++;
        if (count > 6) {
          cup_error("More than 6 arguments not yet supported in JIT");
          count = 6;
        }
        value = codegen_expr_ctx(state, buf, RAX, RAX, ctx);
        if (value != count) {
          cup_errorf("N_CALL ARGS %ld %ld", value, count);
          exit(1);
        }
#if DEBUG_LOG
        printf("call = %d, %i\n", count, type->data[0]->realtype);
#endif
      } else
        count = codegen_expr_ctx(state, buf, RAX, RAX, ctx);
#else
      count = codegen_expr_ctx(state, buf, RAX, RAX, ctx);
#endif
      if (count > sizeof(sysv_arg_regs) / sizeof(sysv_arg_regs[0])) {
        cup_errorf("More than %zu arguments are not yet supported in JIT",
                   sizeof(sysv_arg_regs) / sizeof(sysv_arg_regs[0]));
        exit(1);
      }
#if DEBUG_LOG
      printf("i = %ld\n", value);
#endif
      for (value = 0; value < count; value++) {
        if (codegen_expr_ctx(state, buf, sysv_arg_regs[value], sysv_arg_regs[value], ctx)) {
          cup_error("N_CALL 1 (REAL)");
          exit(1);
        }
      }
      emit_pop_reg(buf, RAX);
      emit_call_reg(buf, RAX);
      if (treg != RAX) {
        emit_mov_reg_reg(buf, treg, RAX);
      }
      return 0;
    }
    case T_IF: {
      codegen_tree(state, buf, RAX, ctx);
      emit_test_reg_reg(buf, RAX, RAX);
      size_t false_patch = emit_je_rel32(buf, 0);
      codegen_tree(state, buf, treg, ctx);
      patch_rel32(buf, false_patch, buf->size);
      return 0;
    }
    case T_ELSE: {
      /* Parser representation: T_ELSE, T_IF, condition, then, else. */
      if (buf->range.it >= buf->range.end || buf->range.it->token != T_IF) {
        cup_error("T_ELSE is not followed by T_IF");
        exit(1);
      }
      (void)getNode(&buf->range); /* consume wrapped T_IF */
      codegen_tree(state, buf, RAX, ctx);
      emit_test_reg_reg(buf, RAX, RAX);
      size_t else_patch = emit_je_rel32(buf, 0);
      codegen_tree(state, buf, treg, ctx);
      size_t end_patch = emit_jmp_rel32(buf, 0);
      patch_rel32(buf, else_patch, buf->size);
      codegen_tree(state, buf, treg, ctx);
      patch_rel32(buf, end_patch, buf->size);
      return 0;
    }
    case T_WHILE: {
      size_t condition_target = buf->size;
      codegen_tree(state, buf, RAX, ctx);
      emit_test_reg_reg(buf, RAX, RAX);
      size_t end_patch = emit_je_rel32(buf, 0);

      LoopCodegen loop = {.continue_target = condition_target,
                          .break_patches = {0}, .parent = ctx->loop};
      ctx->loop = &loop;
      codegen_tree(state, buf, treg, ctx);
      ctx->loop = loop.parent;

      size_t back_patch = emit_jmp_rel32(buf, 0);
      patch_rel32(buf, back_patch, condition_target);
      size_t end_target = buf->size;
      patch_rel32(buf, end_patch, end_target);
      size_t breaks = loop.break_patches.size / sizeof(size_t);
      size_t *patches = loop.break_patches.data;
      for (size_t i = 0; i < breaks; i++)
        patch_rel32(buf, patches[i], end_target);
      cup_free(loop.break_patches.data);
      return 0;
    }
    case T_BREAK: {
      if (!ctx->loop) {
        cup_error("break used outside a loop");
        exit(1);
      }
      size_t patch = emit_jmp_rel32(buf, 0);
      vector_pushT(ctx->loop->break_patches, patch);
      return 0;
    }
    case T_CONTINUE: {
      if (!ctx->loop) {
        cup_error("continue used outside a loop");
        exit(1);
      }
      size_t patch = emit_jmp_rel32(buf, 0);
      patch_rel32(buf, patch, ctx->loop->continue_target);
      return 0;
    }
    case T_AT: {
      /*size_t i = codegen_expr(state, nodes, nullptr, RAX);
    if (i) {
      cup_error(state, "N_FUNCTION 1");
      exit(i);
    }
    CVariable *var = state->variable;
    codegen_func(buf, nodes, &var->value, SIZE_MAX);
    break;*/
      size_t size = buf->size;
#if DEBUG_LOG
      printf("--: Save --\n");
#endif
      size_t i = codegen_expr_ctx(state, buf, RAX, RAX, ctx);
#if DEBUG_LOG
      printf("--: RESTORE --\n");
#endif
      buf->size = size;
      if (i) {
        cup_error("N_FUNCTION 1");
        exit(1);
      }
      CVariable* var = state->nodes.node.variable;
      if (var->type == NULL) {
        cup_error("N_FUNCTION var type is NULL");
        exit(1);
      }
      if (var->type->realtype != CUP_TYPE_FUNCTION) {
        cup_error("N_FUNCTION var type is not FUNCTION");
        exit(1);
      }
      size_t arg_count = 0;
      while (var->type->elements[arg_count + 1]) arg_count++;
      codegen_func(state, buf, &var->value, arg_count);
      return 0;
    }
    case T_RETURN: {
      size_t i = codegen_expr_ctx(state, buf, RAX, RAX, ctx);
      if (i) {
        cup_error("N_RETURN 1");
        exit(i);
      }
      size_t patch = emit_jmp_rel32(buf, 0);
      vector_pushT(ctx->return_patches, patch);
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

void codegen_func(CUPState* state, CUPModule* buf, size_t *value, size_t arg_count) {
#if DEBUG_LOG
  printf("---------\n");
#endif
  size_t save = buf->size;
  CodegenContext ctx = {0};
  CRealloc r = {value, 0};
  vector_pushT(buf->reallocs.vec, r);

  emit_push_reg(buf, RBP);
  emit_mov_reg_reg(buf, RBP, RSP);
  emit_xor_reg_reg(buf, RAX, RAX);

#if DEBUG_LOG
  printf("args count = %ld\n", i);
#endif
  if (buf->range.it < buf->range.end && buf->range.it->token == T_COMMA) {
    size_t parsed_arg_count = codegen_expr_ctx(state, buf, RAX, ANY, &ctx);
    if (arg_count != parsed_arg_count) {
      cup_errorf("function argument count mismatch: expected %zu, AST has %zu",
                 arg_count, parsed_arg_count);
      exit(17);
    }
  } else if (arg_count != 0) {
    cup_error("function argument list is missing from AST");
    exit(17);
  }
  for (size_t j = 0; j < arg_count; j++) {
    codegen_expr_ctx(state, buf, RAX, ANY, &ctx);
    emit_mov_deref_reg(buf, sysv_arg_regs[j], RAX);
  }
#if DEBUG_LOG
  printf("args end = %ld\n", i);
#endif

  arg_count = codegen_expr_ctx(state, buf, RETURN_REG, ANY, &ctx); // get codegen left count
  for (size_t j = 0; j < arg_count; j++)
    codegen_expr_ctx(state, buf, RETURN_REG, ANY, &ctx);

  size_t epilogue = buf->size;
  size_t return_count = ctx.return_patches.size / sizeof(size_t);
  size_t *return_patches = ctx.return_patches.data;
  for (size_t i = 0; i < return_count; i++)
    patch_rel32(buf, return_patches[i], epilogue);
  cup_free(ctx.return_patches.data);

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
  size_t count = buf->reallocs.size / sizeof(CRealloc);
#if DEBUG_LOG
  printf("\n--- %ld %ld %ld %ld ---\n", n, save, buf->size, count);
#endif
  count--;
  for (size_t j = 0; j < count; j++) {
    buf->reallocs.data[j].ptr += n;
  }
}