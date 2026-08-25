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

uint8_t reg_alloc[16] = {0}; // 0 = free, 1 = used

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

// SETL reg(set if less)
static void emit_setl_reg(CUPModule *buf, X64Reg reg) {
#if DEBUG_LOG
    printf("OP: setl %s\n", X64Reg_names[reg]);
#endif
  emit_rex(buf, 0, reg);
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

static void emit_movzx_eax_al(CUPModule *buf) {
  emit8(buf, 0x0F);
  emit8(buf, 0xB6);
  emit8(buf, 0xC0);
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
    emit_movzx_eax_al(buf);
    break;
  case T_LESSEQ:
    emit_cmp_reg_reg(buf, target, temp_reg);
    emit_setle_reg(buf, target);
    emit_movzx_eax_al(buf);
    break;
  case T_EQEQ:
    emit_cmp_reg_reg(buf, target, temp_reg);
    emit_sete_reg(buf, target);
    emit_movzx_eax_al(buf);
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
      X64Reg temp_reg = R10;
      if (codegen_expr(state, buf, treg, sreg, range, NULL)) {
        cup_error("N_OP 1 codegen left is multiple values");
        exit(1);
      }
      emit_push_reg(buf, temp_reg);
      size_t i = codegen_expr(state, buf, temp_reg, temp_reg, range, NULL);
      if (i) {
        cup_error("N_OP 2 codegen right is multiple values");
        exit(1);
      }
      //emit_mov_imm64(buf, R11, 8);
      //emit_imul_reg_reg(buf, RAX, R10);
      emit_add_reg_reg(buf, treg, temp_reg);
      emit_mov_reg_deref(buf, treg, sreg);
      emit_pop_reg(buf, temp_reg);
      return 0;
    }
    case N_ARRAY: {
      size_t value = getNode(range)->value;
      size_t off = emit_mov_reg_imm64(buf, treg, (uint64_t)getData(state, value));
      CObjRef ref = {off, 1, NULL, value};
      vector_pushT(buf->objrefs.vec, ref);
      size_t save = buf->size;
      size_t i = codegen_expr(state, buf, treg, sreg, range, rvalue);
      buf->size = save;
      return i;
    }
    case T_NUMBER: {
      emit_mov_reg_imm64(buf, treg, getNode(range)->value);
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
      X64Reg temp_reg = R10;
      //printf("add1 treg = %s, sreg = %s\n", X64Reg_names[treg], X64Reg_names[sreg]);
      size_t i = codegen_expr(state, buf, treg, sreg, range, NULL);
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
      i = codegen_expr(state, buf, temp_reg, temp_reg, range, NULL);
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
#if DEBUG_LOG
      printf("i = %ld\n", value);
#endif
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
      printf("RRRRR - %ld : %ld\n", state->nodes.value, buf->size);
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

void codegen_func(CUPState* state, CUPModule* buf, NRange* range, size_t *value, size_t arg_count) {
  state->nodes.value = buf->size;
  size_t save = buf->size;
  CRealloc r = {value, 0};
  vector_pushT(buf->reallocs.vec, r);
  printf("\n--- %ld %ld %ld ---\n", save, buf->size, buf->reallocs.size / sizeof(CRealloc));

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
    printf("VVVVV - %ld : %ld & %i : %ld\n", state->nodes.value, buf->size, rel, r.ptr);
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
  printf("\n--- %ld %ld %ld %ld ---\n", n, save, buf->size, rcount);
  rcount--;
  for (size_t j = 0; j < rcount; j++) {
    buf->reallocs.data[j].ptr += n;
  }
}