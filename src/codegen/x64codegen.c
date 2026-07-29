#include "../../include/cup.h"
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

size_t reg_alloc[16] = {0}; // 0 = free, 1 = used

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

/*
// JMP rel32
static void emit_jmp_rel32(CUPModule *buf, int32_t offset) {
#if DEBUG_LOG
    printf("OP: jmp %d\n", offset);
#endif
  emit8(buf, 0xE9);
  emit32(buf, offset);
}

// JE rel32 (jump if equal)
static void emit_je_rel32(CUPModule *buf, int32_t offset) {
#if DEBUG_LOG
    printf("OP: je %d\n", offset);
#endif
  emit8(buf, 0x0F);
  emit8(buf, 0x84);
  emit32(buf, offset);
}

// JNE rel32 (jump if not equal)
static void emit_jne_rel32(CUPModule *buf, int32_t offset) {
#if DEBUG_LOG
    printf("OP: jne %d\n", offset);
#endif
  emit8(buf, 0x0F);
  emit8(buf, 0x85);
  emit32(buf, offset);
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

const X64Reg sysv_arg_regs[] = {RDI, RSI, RDX, RCX, R8, R9};

static void codegen_op(CUPModule *buf, X64Reg target, X64Reg temp_reg, const CTType type) {
  switch (type) {
  case T_LESSEQ:
    emit_cmp_reg_reg(buf, target, temp_reg);
    emit_setle_reg(buf, target);
    break;
  case T_EQEQ:
    emit_cmp_reg_reg(buf, target, temp_reg);
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

size_t codegen_expr(CUPState* state, CUPModule *buf, X64Reg treg, X64Reg sreg) {
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
      size_t i = codegen_expr(state, buf, RBX, RBX);
      if (i) {
        cup_error("N_ASSIGN 1");
        exit(1);
      }
      if (treg == RBX) {
        emit_push_reg(buf, RAX);
        emit_push_reg(buf, treg);
      }
      i = codegen_expr(state, buf, RAX, ANY);
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
      value = codegen_expr(state, buf, treg, sreg);
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
      value = codegen_expr(state, buf, temp_reg, temp_reg);
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
      value = codegen_expr(state, buf, RAX, RAX);
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
#else
      count = codegen_expr(state, buf, RAX, RAX);
#endif
#if DEBUG_LOG
      printf("i = %ld\n", value);
#endif
      for (value = 0; value < count; value++) {
        if (codegen_expr(state, buf, sysv_arg_regs[value], sysv_arg_regs[value])) {
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
      size_t i = codegen_expr(state, buf, RAX, RAX);
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
      while (var->type->elements[arg_count + 1] != &cup_type_void) arg_count++;
      codegen_func(state, buf, &var->value, arg_count);
      return 0;
    }
    case T_RETURN: {
      size_t i = codegen_expr(state, buf, RAX, RAX);
      if (i) {
        cup_error("N_RETURN 1");
        exit(i);
      }
      //emit_mov_reg_reg(buf, RSP, RBP);
      //emit_pop_reg(buf, RBP);
      //emit_ret(buf);
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

void codegen_func(CUPState* state, CUPModule* buf, size_t *value, size_t arg_count) {
#if DEBUG_LOG
  printf("---------\n");
#endif
  size_t save = buf->size;
  CRealloc r = {value, 0};
  vector_pushT(buf->reallocs.vec, r);

  emit_push_reg(buf, RBP);
  emit_mov_reg_reg(buf, RBP, RSP);
  emit_xor_reg_reg(buf, RAX, RAX);

#if DEBUG_LOG
  printf("args count = %ld\n", i);
#endif
  if (arg_count) {
    size_t i_save = codegen_expr(state, buf, RAX, ANY);
    if (arg_count != i_save) {
      exit(17);
    }
  }
  for (size_t j = 0; j < arg_count; j++) {
    codegen_expr(state, buf, RAX, ANY);
    emit_mov_deref_reg(buf, sysv_arg_regs[j], RAX);
  }
#if DEBUG_LOG
  printf("args end = %ld\n", i);
#endif

  arg_count = codegen_expr(state, buf, RETURN_REG, ANY); // get codegen left count
  for (size_t j = 0; j < arg_count; j++)
    codegen_expr(state, buf, RETURN_REG, ANY);

  emit_mov_reg_reg(buf, RSP, RBP);
  emit_pop_reg(buf, RBP);
  emit_ret(buf);
#if DEBUG_LOG
  printf("---------\n");
#endif

  if (save == 0)
    return;
  size_t n = buf->size - save;
  if (n < 256) {
    bufmove256(buf, save, n);
  } else if (n < 512) {
    cup_error("Function too large for current implementation (max 256 bytes)");
    bufmove512(buf, save, n);
  } else {
    cup_error("Function too large for current implementation (max 512 bytes)");
    exit(22);
  }
  size_t count = buf->reallocs.size / sizeof(CRealloc);
#if DEBUG_LOG
  printf("\n--- %ld %ld %ld %ld ---\n", n, save, buf->size, count);
#endif
  count--;
  for (size_t j = 0; j < count; j++) {
    buf->reallocs.data[j].ptr += n;
  }
}