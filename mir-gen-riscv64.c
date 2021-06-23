/* This file is a part of MIR project.
   Copyright (C) 2020-2021 Vladimir Makarov <vmakarov.gcc@gmail.com>.
*/

static void fancy_abort (int code) {
  if (!code) abort ();
}
#undef gen_assert
#define gen_assert(c) fancy_abort (c)

#include <limits.h>

#define HREG_EL(h) h##_HARD_REG
#define REP_SEP ,
enum {
  REP8 (HREG_EL, R0, R1, R2, R3, R4, R5, R6, R7),
  REP8 (HREG_EL, R8, R9, R10, R11, R12, R13, R14, R15),
  REP8 (HREG_EL, R16, R17, R18, R19, R20, R21, R22, R23),
  REP8 (HREG_EL, R24, R25, R26, R27, R28, R29, R30, R31),
  /*Aliases: */ ZERO_HARD_REG = R0_HARD_REG,
  REP7 (HREG_EL, RA, SP, GP, TP, T0, T1, T2),
  REP8 (HREG_EL, FP, S1, A0, A1, A2, A3, A4, A5),
  REP8 (HREG_EL, A6, A7, S2, S3, S4, S5, S6, S7),
  REP8 (HREG_EL, S8, S9, S10, S11, T3, T4, T5, T6),

  REP8 (HREG_EL, F0, F1, F2, F3, F4, F5, F6, F7),
  REP8 (HREG_EL, F8, F9, F10, F11, F12, F13, F14, F15),
  REP8 (HREG_EL, F16, F17, F18, F19, F20, F21, F22, F23),
  REP8 (HREG_EL, F24, F25, F26, F27, F28, F29, F30, F31),
  /* Aliases: */ FT0_HARD_REG = F0_HARD_REG,
  REP7 (HREG_EL, FT1, FT2, FT3, FT4, FT5, FT6, FT7),
  REP8 (HREG_EL, FS0, FS1, FA0, FA1, FA2, FA3, FA4, FA5),
  REP8 (HREG_EL, FA6, FA7, FS2, FS3, FS4, FS5, FS6, FS7),
  REP8 (HREG_EL, FS8, FS9, FS10, FS11, FT8, FT9, FT10, FT11),
};
#undef REP_SEP

static const MIR_reg_t MAX_HARD_REG = F31_HARD_REG;
static const MIR_reg_t LINK_HARD_REG = RA_HARD_REG;

static int target_locs_num (MIR_reg_t loc, MIR_type_t type) {
  return loc > MAX_HARD_REG && type == MIR_T_LD ? 2 : 1;
}

static inline MIR_reg_t target_nth_loc (MIR_reg_t loc, MIR_type_t type, int n) { return loc + n; }

/* Hard regs not used in machinized code, preferably call used ones. */
const MIR_reg_t TEMP_INT_HARD_REG1 = T5_HARD_REG, TEMP_INT_HARD_REG2 = T6_HARD_REG;
const MIR_reg_t TEMP_FLOAT_HARD_REG1 = FT10_HARD_REG, TEMP_FLOAT_HARD_REG2 = FT11_HARD_REG;
const MIR_reg_t TEMP_DOUBLE_HARD_REG1 = FT10_HARD_REG, TEMP_DOUBLE_HARD_REG2 = FT11_HARD_REG;
/* we use only builtins for long double ops: */
const MIR_reg_t TEMP_LDOUBLE_HARD_REG1 = MIR_NON_HARD_REG;
const MIR_reg_t TEMP_LDOUBLE_HARD_REG2 = MIR_NON_HARD_REG;

static inline int target_hard_reg_type_ok_p (MIR_reg_t hard_reg, MIR_type_t type) {
  assert (hard_reg <= MAX_HARD_REG);
  if (type == MIR_T_LD) return FALSE; /* long double can be in hard regs only for arg passing */
  return MIR_fp_type_p (type) ? hard_reg >= F0_HARD_REG : hard_reg < F0_HARD_REG;
}

static inline int target_fixed_hard_reg_p (MIR_reg_t hard_reg) {
  assert (hard_reg <= MAX_HARD_REG);
  return (hard_reg == ZERO_HARD_REG || hard_reg == FP_HARD_REG || hard_reg == SP_HARD_REG
          || hard_reg == GP_HARD_REG || hard_reg == TP_HARD_REG  // ???
          || hard_reg == TEMP_INT_HARD_REG1 || hard_reg == TEMP_INT_HARD_REG2
          || hard_reg == TEMP_FLOAT_HARD_REG1 || hard_reg == TEMP_FLOAT_HARD_REG2
          || hard_reg == TEMP_DOUBLE_HARD_REG1 || hard_reg == TEMP_DOUBLE_HARD_REG2
          || hard_reg == TEMP_LDOUBLE_HARD_REG1 || hard_reg == TEMP_LDOUBLE_HARD_REG2);
}

static inline int target_call_used_hard_reg_p (MIR_reg_t hard_reg, MIR_type_t type) {
  assert (hard_reg <= MAX_HARD_REG);
  if (hard_reg <= R31_HARD_REG)
    return !(hard_reg == R8_HARD_REG || hard_reg == R9_HARD_REG
             || (hard_reg >= R18_HARD_REG && hard_reg <= R27_HARD_REG));
  return type == MIR_T_LD
         || !(hard_reg == F8_HARD_REG || hard_reg == F9_HARD_REG
              || (hard_reg >= F18_HARD_REG && hard_reg <= F27_HARD_REG));
}

/* Stack layout (sp refers to the last reserved stack slot address)
   from higher address to lower address memory:

   | ...           |  prev func stack (start aligned to 16 bytes)
   |---------------|
   | gr save area  |  optional area for vararg func reg save area
   |               |  (int arg regs corresponding to varargs)
   |---------------|
   | saved regs    |  callee saved regs used in the func (known only after RA), rounded 16 bytes
   |---------------|
   | slots assigned|  can be absent for small functions (known only after RA), rounded 16 bytes
   |   to pseudos  |
   |---------------|
   |   previous    |  (sp right after call) 16-bytes setup in prolog, used only for varag func or
   | stack start   |   args passed on stack to move args and to setup va_start on machinize pass
   |---------------|
   | RA            |  sp before prologue and after saving RA = start sp
   |---------------|
   | old FP        |  frame pointer for previous func stack frame; new FP refers for here
   |---------------|
   |  small aggreg |
   |  save area    |  optional
   |---------------|
   | alloca areas  |  optional
   |---------------|
   | slots for     |  dynamically allocated/deallocated by caller
   |  passing args |

   size of slots and saved regs is multiple of 16 bytes

 */

static const MIR_insn_code_t target_io_dup_op_insn_codes[] = {MIR_INSN_BOUND};

static MIR_insn_code_t get_ext_code (MIR_type_t type) {
  switch (type) {
  case MIR_T_I8: return MIR_EXT8;
  case MIR_T_U8: return MIR_UEXT8;
  case MIR_T_I16: return MIR_EXT16;
  case MIR_T_U16: return MIR_UEXT16;
  case MIR_T_I32: return MIR_EXT32;
  case MIR_T_U32: return MIR_UEXT32;
  default: return MIR_INVALID_INSN;
  }
}

static MIR_reg_t get_arg_reg (MIR_type_t arg_type, int vararg_p, size_t *int_arg_num,
                              size_t *fp_arg_num, MIR_insn_code_t *mov_code) {
  MIR_reg_t arg_reg;

  if (!vararg_p && (arg_type == MIR_T_F || arg_type == MIR_T_D)) {
    switch (*fp_arg_num) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7: arg_reg = FA0_HARD_REG + *fp_arg_num; break;
    default: arg_reg = MIR_NON_HARD_REG; break;
    }
    (*fp_arg_num)++;
    *mov_code = arg_type == MIR_T_F ? MIR_FMOV : MIR_DMOV;
  } else { /* including LD, BLK, RBLK: */
    if (arg_type == MIR_T_LD && *int_arg_num % 2 != 0) (*int_arg_num)++;
    switch (*int_arg_num) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7: arg_reg = A0_HARD_REG + *int_arg_num; break;
    default: arg_reg = MIR_NON_HARD_REG; break;
    }
    (*int_arg_num)++;
    if (arg_type != MIR_T_LD) {
      *mov_code = MIR_MOV;
    } else {
      (*int_arg_num)++;
      *mov_code = MIR_LDMOV;
    }
  }
  return arg_reg;
}

static void mir_blk_mov (uint64_t *to, uint64_t *from, uint64_t nwords) {
  for (; nwords > 0; nwords--) *to++ = *from++;
}

static MIR_insn_t gen_mov (gen_ctx_t gen_ctx, MIR_insn_t anchor, MIR_insn_code_t code,
                           MIR_op_t dst_op, MIR_op_t src_op) {
  MIR_insn_t insn = MIR_new_insn (gen_ctx->ctx, code, dst_op, src_op);
  gen_add_insn_before (gen_ctx, anchor, insn);
  return insn;
}

static const char *BLK_MOV = "mir.blk_mov";
static const char *BLK_MOV_P = "mir.blk_mov.p";

static void gen_blk_mov (gen_ctx_t gen_ctx, MIR_insn_t anchor, size_t to_disp,
                         MIR_reg_t to_base_hard_reg, MIR_reg_t from_base_reg, size_t qwords,
                         int save_regs) {
  size_t from_disp = 0;
  MIR_context_t ctx = gen_ctx->ctx;
  MIR_func_t func = curr_func_item->u.func;
  MIR_item_t proto_item, func_import_item;
  MIR_insn_t new_insn;
  MIR_op_t ops[5], freg_op, treg_op, treg_op2, treg_op3, treg_op4;

  treg_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
  treg_op2 = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
  if (qwords <= 16) {
    gen_mov (gen_ctx, anchor, MIR_MOV, treg_op2, MIR_new_int_op (ctx, to_disp));
    gen_add_insn_before (gen_ctx, anchor,
                         MIR_new_insn (gen_ctx->ctx, MIR_ADD, treg_op2, treg_op2,
                                       _MIR_new_hard_reg_op (ctx, to_base_hard_reg)));
    for (; qwords > 0; qwords--, to_disp += 8, from_disp += 8) {
      gen_mov (gen_ctx, anchor, MIR_MOV, treg_op,
               MIR_new_mem_op (ctx, MIR_T_I64, from_disp, from_base_reg, 0, 1));
      gen_mov (gen_ctx, anchor, MIR_MOV,
               _MIR_new_hard_reg_mem_op (ctx, MIR_T_I64, to_disp, to_base_hard_reg,
                                         MIR_NON_HARD_REG, 1),
               treg_op);
    }
    return;
  }
  treg_op3 = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
  /* Save arg regs: */
  if (save_regs > 0)
    gen_mov (gen_ctx, anchor, MIR_MOV, treg_op, _MIR_new_hard_reg_op (ctx, A0_HARD_REG));
  if (save_regs > 1)
    gen_mov (gen_ctx, anchor, MIR_MOV, treg_op2, _MIR_new_hard_reg_op (ctx, A1_HARD_REG));
  if (save_regs > 2)
    gen_mov (gen_ctx, anchor, MIR_MOV, treg_op3, _MIR_new_hard_reg_op (ctx, A2_HARD_REG));
  /* call blk move: */
  proto_item = _MIR_builtin_proto (ctx, curr_func_item->module, BLK_MOV_P, 0, NULL, 3, MIR_T_I64,
                                   "to", MIR_T_I64, "from", MIR_T_I64, "nwords");
  func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, BLK_MOV, mir_blk_mov);
  freg_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
  new_insn = MIR_new_insn (ctx, MIR_MOV, freg_op, MIR_new_ref_op (ctx, func_import_item));
  gen_add_insn_before (gen_ctx, anchor, new_insn);
  treg_op4 = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
  gen_mov (gen_ctx, anchor, MIR_MOV, treg_op4, MIR_new_int_op (ctx, to_disp));
  gen_add_insn_before (gen_ctx, anchor,
                       MIR_new_insn (gen_ctx->ctx, MIR_ADD, _MIR_new_hard_reg_op (ctx, A0_HARD_REG),
                                     _MIR_new_hard_reg_op (ctx, to_base_hard_reg), treg_op4));
  gen_add_insn_before (gen_ctx, anchor,
                       MIR_new_insn (gen_ctx->ctx, MIR_ADD, _MIR_new_hard_reg_op (ctx, A1_HARD_REG),
                                     MIR_new_reg_op (ctx, from_base_reg),
                                     MIR_new_int_op (ctx, from_disp)));
  gen_mov (gen_ctx, anchor, MIR_MOV, _MIR_new_hard_reg_op (ctx, A2_HARD_REG),
           MIR_new_int_op (ctx, qwords));
  ops[0] = MIR_new_ref_op (ctx, proto_item);
  ops[1] = freg_op;
  ops[2] = _MIR_new_hard_reg_op (ctx, A0_HARD_REG);
  ops[3] = _MIR_new_hard_reg_op (ctx, A1_HARD_REG);
  ops[4] = _MIR_new_hard_reg_op (ctx, A2_HARD_REG);
  new_insn = MIR_new_insn_arr (ctx, MIR_CALL, 5, ops);
  gen_add_insn_before (gen_ctx, anchor, new_insn);
  /* Restore arg regs: */
  if (save_regs > 0)
    gen_mov (gen_ctx, anchor, MIR_MOV, _MIR_new_hard_reg_op (ctx, A0_HARD_REG), treg_op);
  if (save_regs > 1)
    gen_mov (gen_ctx, anchor, MIR_MOV, _MIR_new_hard_reg_op (ctx, A1_HARD_REG), treg_op2);
  if (save_regs > 2)
    gen_mov (gen_ctx, anchor, MIR_MOV, _MIR_new_hard_reg_op (ctx, R2_HARD_REG), treg_op3);
}

#define FMVXW_CODE 0
#define FMVXD_CODE 1

static void machinize_call (gen_ctx_t gen_ctx, MIR_insn_t call_insn) {
  MIR_context_t ctx = gen_ctx->ctx;
  MIR_func_t func = curr_func_item->u.func;
  MIR_proto_t proto = call_insn->ops[0].u.ref->u.proto;
  int float_p;
  size_t nargs, nops = MIR_insn_nops (ctx, call_insn), start = proto->nres + 2;
  size_t int_arg_num = 0, fp_arg_num = 0, mem_size = 0, blk_offset = 0, qwords;
  MIR_type_t type, mem_type;
  MIR_op_mode_t mode;
  MIR_var_t *arg_vars = NULL;
  MIR_reg_t arg_reg;
  MIR_op_t arg_op, temp_op, arg_reg_op, ret_reg_op, mem_op, treg_op;
  MIR_insn_code_t new_insn_code, ext_code;
  MIR_insn_t new_insn, prev_insn, next_insn, ext_insn;
  MIR_insn_t prev_call_insn = DLIST_PREV (MIR_insn_t, call_insn);
  MIR_insn_t curr_prev_call_insn = prev_call_insn;

  assert (__SIZEOF_LONG_DOUBLE__ == 16);
  if (call_insn->code == MIR_INLINE) call_insn->code = MIR_CALL;
  if (proto->args == NULL) {
    nargs = 0;
  } else {
    gen_assert (nops >= VARR_LENGTH (MIR_var_t, proto->args)
                && (proto->vararg_p || nops - start == VARR_LENGTH (MIR_var_t, proto->args)));
    nargs = VARR_LENGTH (MIR_var_t, proto->args);
    arg_vars = VARR_ADDR (MIR_var_t, proto->args);
  }
  if (call_insn->ops[1].mode != MIR_OP_REG && call_insn->ops[1].mode != MIR_OP_HARD_REG) {
    // ??? to optimize (can be immediate operand for func call)
    temp_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
    new_insn = MIR_new_insn (ctx, MIR_MOV, temp_op, call_insn->ops[1]);
    call_insn->ops[1] = temp_op;
    gen_add_insn_before (gen_ctx, call_insn, new_insn);
  }
  for (size_t i = start; i < nops; i++) { /* calculate offset for blk params */
    if (i - start < nargs) {
      type = arg_vars[i - start].type;
    } else if (call_insn->ops[i].mode == MIR_OP_MEM) {
      type = call_insn->ops[i].u.mem.type;
      gen_assert (MIR_all_blk_type_p (type));
    } else {
      mode = call_insn->ops[i].value_mode;  // ??? smaller ints
      gen_assert (mode == MIR_OP_INT || mode == MIR_OP_UINT || mode == MIR_OP_FLOAT
                  || mode == MIR_OP_DOUBLE || mode == MIR_OP_LDOUBLE);
      if (mode == MIR_OP_FLOAT)
        (*MIR_get_error_func (ctx)) (MIR_call_op_error,
                                     "passing float variadic arg (should be passed as double)");
      type = mode == MIR_OP_DOUBLE ? MIR_T_D : mode == MIR_OP_LDOUBLE ? MIR_T_LD : MIR_T_I64;
    }
    gen_assert (!MIR_all_blk_type_p (type) || call_insn->ops[i].mode == MIR_OP_MEM);
    if ((MIR_T_I8 <= type && type <= MIR_T_U64) || type == MIR_T_P || type == MIR_T_LD
        || MIR_all_blk_type_p (type)) {
      if (type == MIR_T_BLK + 2 && (qwords = (call_insn->ops[i].u.mem.disp + 7) / 8) <= 2) {
        if (fp_arg_num + qwords > 8)
          blk_offset += (qwords - (fp_arg_num + qwords == 9 ? 1 : 0)) * 8;
        fp_arg_num += qwords;
      } else if (MIR_blk_type_p (type) && (qwords = (call_insn->ops[i].u.mem.disp + 7) / 8) <= 2) {
        if (type == MIR_T_BLK + 1) int_arg_num = (int_arg_num + 1) / 2 * 2; /* Make even */
        if (int_arg_num + qwords > 8)
          blk_offset += (qwords - (int_arg_num + qwords == 9 ? 1 : 0)) * 8;
        int_arg_num += qwords;
      } else { /* blocks here are passed by address */
        if (type == MIR_T_LD) int_arg_num = (int_arg_num + 1) / 2 * 2; /* Make even */
        if (int_arg_num >= 8) blk_offset += 8 + (type == MIR_T_LD ? 8 : 0);
        int_arg_num++;
        if (type == MIR_T_LD) int_arg_num++;
      }
    } else if (type == MIR_T_F || type == MIR_T_D) {
      if (i - start >= nargs) { /* varargs are passed by int regs */
        if (int_arg_num >= 8) blk_offset += 8;
        int_arg_num++;
      } else {
        if (fp_arg_num >= 8) blk_offset += 8;
        fp_arg_num++;
      }
    } else {
      MIR_get_error_func (ctx) (MIR_call_op_error, "wrong type of arg value");
    }
  }
  blk_offset = (blk_offset + 15) / 16 * 16; /* align stack */
  int_arg_num = fp_arg_num = 0;
  for (size_t i = start; i < nops; i++) {
    arg_op = call_insn->ops[i];
    gen_assert (arg_op.mode == MIR_OP_REG || arg_op.mode == MIR_OP_HARD_REG
                || (arg_op.mode == MIR_OP_MEM && MIR_all_blk_type_p (arg_op.u.mem.type)));
    if (i - start < nargs) {
      type = arg_vars[i - start].type;
    } else if (call_insn->ops[i].mode == MIR_OP_MEM) {
      type = call_insn->ops[i].u.mem.type;
      gen_assert (MIR_all_blk_type_p (type));
    } else {
      mode = call_insn->ops[i].value_mode;  // ??? smaller ints
      type = mode == MIR_OP_DOUBLE ? MIR_T_D : mode == MIR_OP_LDOUBLE ? MIR_T_LD : MIR_T_I64;
    }
    ext_insn = NULL;
    if ((ext_code = get_ext_code (type)) != MIR_INVALID_INSN) { /* extend arg if necessary */
      temp_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
      ext_insn = MIR_new_insn (ctx, ext_code, temp_op, arg_op);
      call_insn->ops[i] = arg_op = temp_op;
    }
    gen_assert (
      !MIR_all_blk_type_p (type)
      || (arg_op.mode == MIR_OP_MEM && arg_op.u.mem.disp >= 0 && arg_op.u.mem.index == 0));
    if (MIR_blk_type_p (type)) {
      qwords = (arg_op.u.mem.disp + 7) / 8;
      if (qwords <= 2) {
        arg_reg = A0_HARD_REG + int_arg_num;
        if (type == MIR_T_BLK + 1) int_arg_num = (int_arg_num + 1) / 2 * 2; /* Make even */
        for (int n = 0; n < qwords; n++) {
          if (type == MIR_T_BLK + 2) {
            if (fp_arg_num < 8) {
              new_insn
                = MIR_new_insn (ctx, MIR_DMOV,
                                _MIR_new_hard_reg_op (ctx, FA0_HARD_REG + fp_arg_num),
                                MIR_new_mem_op (ctx, MIR_T_D, n * 8, arg_op.u.mem.base, 0, 1));
              gen_add_insn_before (gen_ctx, call_insn, new_insn);
              setup_call_hard_reg_args (gen_ctx, call_insn, FA0_HARD_REG + fp_arg_num);
              fp_arg_num++;
            } else { /* put word on stack */
              treg_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_D, func));
              new_insn
                = MIR_new_insn (ctx, MIR_DMOV, treg_op,
                                MIR_new_mem_op (ctx, MIR_T_D, n * 8, arg_op.u.mem.base, 0, 1));
              gen_add_insn_before (gen_ctx, call_insn, new_insn);
              mem_op = _MIR_new_hard_reg_mem_op (ctx, MIR_T_D, mem_size, SP_HARD_REG,
                                                 MIR_NON_HARD_REG, 1);
              new_insn = MIR_new_insn (ctx, MIR_DMOV, mem_op, treg_op);
              gen_add_insn_before (gen_ctx, call_insn, new_insn);
              mem_size += 8;
            }
          } else if (int_arg_num < 8) {
            new_insn
              = MIR_new_insn (ctx, MIR_MOV, _MIR_new_hard_reg_op (ctx, A0_HARD_REG + int_arg_num),
                              MIR_new_mem_op (ctx, MIR_T_I64, n * 8, arg_op.u.mem.base, 0, 1));
            gen_add_insn_before (gen_ctx, call_insn, new_insn);
            setup_call_hard_reg_args (gen_ctx, call_insn, A0_HARD_REG + int_arg_num);
            int_arg_num++;
          } else { /* put word on stack */
            treg_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
            new_insn
              = MIR_new_insn (ctx, MIR_MOV, treg_op,
                              MIR_new_mem_op (ctx, MIR_T_I64, n * 8, arg_op.u.mem.base, 0, 1));
            gen_add_insn_before (gen_ctx, call_insn, new_insn);
            mem_op = _MIR_new_hard_reg_mem_op (ctx, MIR_T_I64, mem_size, SP_HARD_REG,
                                               MIR_NON_HARD_REG, 1);
            new_insn = MIR_new_insn (ctx, MIR_MOV, mem_op, treg_op);
            gen_add_insn_before (gen_ctx, call_insn, new_insn);
            mem_size += 8;
          }
        }
        continue;
      }
      /* Put on stack and pass the address: */
      gen_blk_mov (gen_ctx, call_insn, blk_offset, SP_HARD_REG, arg_op.u.mem.base, qwords,
                   int_arg_num);
      arg_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
      gen_assert (curr_prev_call_insn
                  != NULL); /* call_insn should not be 1st after simplification */
      new_insn
        = MIR_new_insn (gen_ctx->ctx, MIR_ADD, arg_op, _MIR_new_hard_reg_op (ctx, SP_HARD_REG),
                        MIR_new_int_op (ctx, blk_offset));
      gen_add_insn_after (gen_ctx, curr_prev_call_insn, new_insn);
      curr_prev_call_insn = DLIST_NEXT (MIR_insn_t, new_insn);
      blk_offset += qwords * 8;
    }
    if ((arg_reg
         = get_arg_reg (type, i - start >= nargs, &int_arg_num, &fp_arg_num, &new_insn_code))
        != MIR_NON_HARD_REG) {
      /* put arguments to argument hard regs */
      if (ext_insn != NULL) gen_add_insn_before (gen_ctx, call_insn, ext_insn);
      arg_reg_op = _MIR_new_hard_reg_op (ctx, arg_reg);
      if (type != MIR_T_RBLK) {
        if (new_insn_code == MIR_MOV && (type == MIR_T_F || type == MIR_T_D)) {
          new_insn
            = _MIR_new_unspec_insn (ctx, 3,
                                    MIR_new_int_op (ctx, type == MIR_T_F ? FMVXW_CODE : FMVXD_CODE),
                                    arg_reg_op, arg_op);
        } else {
          new_insn = MIR_new_insn (ctx, new_insn_code, arg_reg_op, arg_op);
        }
      } else {
        assert (arg_op.mode == MIR_OP_MEM);
        new_insn = MIR_new_insn (ctx, MIR_MOV, arg_reg_op, MIR_new_reg_op (ctx, arg_op.u.mem.base));
        arg_reg_op = _MIR_new_hard_reg_mem_op (ctx, MIR_T_RBLK, arg_op.u.mem.disp, arg_reg,
                                               MIR_NON_HARD_REG, 1);
      }
      gen_add_insn_before (gen_ctx, call_insn, new_insn);
      call_insn->ops[i] = arg_reg_op;
      if (type == MIR_T_LD) /* long double is passed in 2 int hard regs: */
        setup_call_hard_reg_args (gen_ctx, call_insn, arg_reg + 1);
    } else { /* put arguments on the stack */
      mem_type = type == MIR_T_F || type == MIR_T_D || type == MIR_T_LD ? type : MIR_T_I64;
      new_insn_code = (type == MIR_T_F    ? MIR_FMOV
                       : type == MIR_T_D  ? MIR_DMOV
                       : type == MIR_T_LD ? MIR_LDMOV
                                          : MIR_MOV);
      mem_op = _MIR_new_hard_reg_mem_op (ctx, mem_type, mem_size, SP_HARD_REG, MIR_NON_HARD_REG, 1);
      if (type != MIR_T_RBLK) {
        new_insn = MIR_new_insn (ctx, new_insn_code, mem_op, arg_op);
      } else {
        assert (arg_op.mode == MIR_OP_MEM);
        new_insn
          = MIR_new_insn (ctx, new_insn_code, mem_op, MIR_new_reg_op (ctx, arg_op.u.mem.base));
      }
      gen_assert (curr_prev_call_insn != NULL); /* call should not be 1st after simplification */
      MIR_insert_insn_after (ctx, curr_func_item, curr_prev_call_insn, new_insn);
      prev_insn = DLIST_PREV (MIR_insn_t, new_insn);
      next_insn = DLIST_NEXT (MIR_insn_t, new_insn);
      create_new_bb_insns (gen_ctx, prev_insn, next_insn, call_insn);
      call_insn->ops[i] = mem_op;
      mem_size += type == MIR_T_LD ? 16 : 8;
      if (ext_insn != NULL) gen_add_insn_after (gen_ctx, curr_prev_call_insn, ext_insn);
    }
  }
  blk_offset = (blk_offset + 15) / 16 * 16;
  if (blk_offset != 0) mem_size = blk_offset;
  int_arg_num = fp_arg_num = 0;
  for (size_t i = 0; i < proto->nres; i++) {
    ret_reg_op = call_insn->ops[i + 2];
    gen_assert (ret_reg_op.mode == MIR_OP_REG || ret_reg_op.mode == MIR_OP_HARD_REG);
    type = proto->res_types[i];
    float_p = type == MIR_T_F || type == MIR_T_D;
    if (float_p && fp_arg_num < 2) {
      new_insn = MIR_new_insn (ctx, type == MIR_T_F ? MIR_FMOV : MIR_DMOV, ret_reg_op,
                               _MIR_new_hard_reg_op (ctx, FA0_HARD_REG + fp_arg_num));
      fp_arg_num++;
    } else if (type == MIR_T_LD && int_arg_num < 2) {
      new_insn = MIR_new_insn (ctx, MIR_LDMOV, ret_reg_op,
                               _MIR_new_hard_reg_op (ctx, A0_HARD_REG + int_arg_num));
      int_arg_num += 2;
    } else if (!float_p && int_arg_num < 2) {
      new_insn = MIR_new_insn (ctx, MIR_MOV, ret_reg_op,
                               _MIR_new_hard_reg_op (ctx, A0_HARD_REG + int_arg_num));
      int_arg_num++;
    } else {
      (*MIR_get_error_func (ctx)) (MIR_ret_error,
                                   "riscv can not handle this combination of return values");
    }
    MIR_insert_insn_after (ctx, curr_func_item, call_insn, new_insn);
    call_insn->ops[i + 2] = new_insn->ops[1];
    if ((ext_code = get_ext_code (type)) != MIR_INVALID_INSN) {
      MIR_insert_insn_after (ctx, curr_func_item, new_insn,
                             MIR_new_insn (ctx, ext_code, ret_reg_op, ret_reg_op));
      new_insn = DLIST_NEXT (MIR_insn_t, new_insn);
    }
    create_new_bb_insns (gen_ctx, call_insn, DLIST_NEXT (MIR_insn_t, new_insn), call_insn);
  }
  if (mem_size != 0) { /* allocate/deallocate stack for args passed on stack */
    temp_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
    mem_size = (mem_size + 15) / 16 * 16; /* make it of several 16 bytes */
    new_insn = MIR_new_insn (ctx, MIR_SUB, _MIR_new_hard_reg_op (ctx, SP_HARD_REG),
                             _MIR_new_hard_reg_op (ctx, SP_HARD_REG), temp_op);
    MIR_insert_insn_after (ctx, curr_func_item, prev_call_insn, new_insn);
    next_insn = DLIST_NEXT (MIR_insn_t, new_insn);
    new_insn = MIR_new_insn (ctx, MIR_MOV, temp_op, MIR_new_int_op (ctx, mem_size));
    MIR_insert_insn_after (ctx, curr_func_item, prev_call_insn, new_insn);
    create_new_bb_insns (gen_ctx, prev_call_insn, next_insn, call_insn);
    temp_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
    new_insn = MIR_new_insn (ctx, MIR_MOV, temp_op, MIR_new_int_op (ctx, mem_size));
    MIR_insert_insn_after (ctx, curr_func_item, call_insn, new_insn);
    next_insn = DLIST_NEXT (MIR_insn_t, new_insn);
    new_insn = MIR_new_insn (ctx, MIR_ADD, _MIR_new_hard_reg_op (ctx, SP_HARD_REG),
                             _MIR_new_hard_reg_op (ctx, SP_HARD_REG), temp_op);
    MIR_insert_insn_before (ctx, curr_func_item, next_insn, new_insn);
    create_new_bb_insns (gen_ctx, call_insn, next_insn, call_insn);
  }
}

static long double mir_i2ld (int64_t i) { return i; }
static const char *I2LD = "mir.i2ld";
static const char *I2LD_P = "mir.i2ld.p";

static long double mir_ui2ld (uint64_t i) { return i; }
static const char *UI2LD = "mir.ui2ld";
static const char *UI2LD_P = "mir.ui2ld.p";

static long double mir_f2ld (float f) { return f; }
static const char *F2LD = "mir.f2ld";
static const char *F2LD_P = "mir.f2ld.p";

static long double mir_d2ld (double d) { return d; }
static const char *D2LD = "mir.d2ld";
static const char *D2LD_P = "mir.d2ld.p";

static int64_t mir_ld2i (long double ld) { return ld; }
static const char *LD2I = "mir.ld2i";
static const char *LD2I_P = "mir.ld2i.p";

static float mir_ld2f (long double ld) { return ld; }
static const char *LD2F = "mir.ld2f";
static const char *LD2F_P = "mir.ld2f.p";

static double mir_ld2d (long double ld) { return ld; }
static const char *LD2D = "mir.ld2d";
static const char *LD2D_P = "mir.ld2d.p";

static long double mir_ldadd (long double d1, long double d2) { return d1 + d2; }
static const char *LDADD = "mir.ldadd";
static const char *LDADD_P = "mir.ldadd.p";

static long double mir_ldsub (long double d1, long double d2) { return d1 - d2; }
static const char *LDSUB = "mir.ldsub";
static const char *LDSUB_P = "mir.ldsub.p";

static long double mir_ldmul (long double d1, long double d2) { return d1 * d2; }
static const char *LDMUL = "mir.ldmul";
static const char *LDMUL_P = "mir.ldmul.p";

static long double mir_lddiv (long double d1, long double d2) { return d1 / d2; }
static const char *LDDIV = "mir.lddiv";
static const char *LDDIV_P = "mir.lddiv.p";

static long double mir_ldneg (long double d) { return -d; }
static const char *LDNEG = "mir.ldneg";
static const char *LDNEG_P = "mir.ldneg.p";

static const char *VA_ARG_P = "mir.va_arg.p";
static const char *VA_ARG = "mir.va_arg";
static const char *VA_BLOCK_ARG_P = "mir.va_block_arg.p";
static const char *VA_BLOCK_ARG = "mir.va_block_arg";

static int64_t mir_ldeq (long double d1, long double d2) { return d1 == d2; }
static const char *LDEQ = "mir.ldeq";
static const char *LDEQ_P = "mir.ldeq.p";

static int64_t mir_ldne (long double d1, long double d2) { return d1 != d2; }
static const char *LDNE = "mir.ldne";
static const char *LDNE_P = "mir.ldne.p";

static int64_t mir_ldlt (long double d1, long double d2) { return d1 < d2; }
static const char *LDLT = "mir.ldlt";
static const char *LDLT_P = "mir.ldlt.p";

static int64_t mir_ldge (long double d1, long double d2) { return d1 >= d2; }
static const char *LDGE = "mir.ldge";
static const char *LDGE_P = "mir.ldge.p";

static int64_t mir_ldgt (long double d1, long double d2) { return d1 > d2; }
static const char *LDGT = "mir.ldgt";
static const char *LDGT_P = "mir.ldgt.p";

static int64_t mir_ldle (long double d1, long double d2) { return d1 <= d2; }
static const char *LDLE = "mir.ldle";
static const char *LDLE_P = "mir.ldle.p";

static int get_builtin (gen_ctx_t gen_ctx, MIR_insn_code_t code, MIR_item_t *proto_item,
                        MIR_item_t *func_import_item) {
  MIR_context_t ctx = gen_ctx->ctx;
  MIR_type_t res_type;

  *func_import_item = *proto_item = NULL; /* to remove uninitialized warning */
  switch (code) {
  case MIR_I2LD:
    res_type = MIR_T_LD;
    *proto_item
      = _MIR_builtin_proto (ctx, curr_func_item->module, I2LD_P, 1, &res_type, 1, MIR_T_I64, "v");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, I2LD, mir_i2ld);
    return 1;
  case MIR_UI2LD:
    res_type = MIR_T_LD;
    *proto_item
      = _MIR_builtin_proto (ctx, curr_func_item->module, UI2LD_P, 1, &res_type, 1, MIR_T_I64, "v");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, UI2LD, mir_ui2ld);
    return 1;
  case MIR_F2LD:
    res_type = MIR_T_LD;
    *proto_item
      = _MIR_builtin_proto (ctx, curr_func_item->module, F2LD_P, 1, &res_type, 1, MIR_T_F, "v");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, F2LD, mir_f2ld);
    return 1;
  case MIR_D2LD:
    res_type = MIR_T_LD;
    *proto_item
      = _MIR_builtin_proto (ctx, curr_func_item->module, D2LD_P, 1, &res_type, 1, MIR_T_D, "v");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, D2LD, mir_d2ld);
    return 1;
  case MIR_LD2I:
    res_type = MIR_T_I64;
    *proto_item
      = _MIR_builtin_proto (ctx, curr_func_item->module, LD2I_P, 1, &res_type, 1, MIR_T_LD, "v");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, LD2I, mir_ld2i);
    return 1;
  case MIR_LD2F:
    res_type = MIR_T_F;
    *proto_item
      = _MIR_builtin_proto (ctx, curr_func_item->module, LD2F_P, 1, &res_type, 1, MIR_T_LD, "v");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, LD2F, mir_ld2f);
    return 1;
  case MIR_LD2D:
    res_type = MIR_T_D;
    *proto_item
      = _MIR_builtin_proto (ctx, curr_func_item->module, LD2D_P, 1, &res_type, 1, MIR_T_LD, "v");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, LD2D, mir_ld2d);
    return 1;
  case MIR_LDADD:
    res_type = MIR_T_LD;
    *proto_item = _MIR_builtin_proto (ctx, curr_func_item->module, LDADD_P, 1, &res_type, 2,
                                      MIR_T_LD, "d1", MIR_T_LD, "d2");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, LDADD, mir_ldadd);
    return 2;
  case MIR_LDSUB:
    res_type = MIR_T_LD;
    *proto_item = _MIR_builtin_proto (ctx, curr_func_item->module, LDSUB_P, 1, &res_type, 2,
                                      MIR_T_LD, "d1", MIR_T_LD, "d2");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, LDSUB, mir_ldsub);
    return 2;
  case MIR_LDMUL:
    res_type = MIR_T_LD;
    *proto_item = _MIR_builtin_proto (ctx, curr_func_item->module, LDMUL_P, 1, &res_type, 2,
                                      MIR_T_LD, "d1", MIR_T_LD, "d2");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, LDMUL, mir_ldmul);
    return 2;
  case MIR_LDDIV:
    res_type = MIR_T_LD;
    *proto_item = _MIR_builtin_proto (ctx, curr_func_item->module, LDDIV_P, 1, &res_type, 2,
                                      MIR_T_LD, "d1", MIR_T_LD, "d2");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, LDDIV, mir_lddiv);
    return 2;
  case MIR_LDNEG:
    res_type = MIR_T_LD;
    *proto_item
      = _MIR_builtin_proto (ctx, curr_func_item->module, LDNEG_P, 1, &res_type, 1, MIR_T_LD, "d");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, LDNEG, mir_ldneg);
    return 1;
  case MIR_LDEQ:
    res_type = MIR_T_I64;
    *proto_item = _MIR_builtin_proto (ctx, curr_func_item->module, LDEQ_P, 1, &res_type, 2,
                                      MIR_T_LD, "d1", MIR_T_LD, "d2");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, LDEQ, mir_ldeq);
    return 2;
  case MIR_LDNE:
    res_type = MIR_T_I64;
    *proto_item = _MIR_builtin_proto (ctx, curr_func_item->module, LDNE_P, 1, &res_type, 2,
                                      MIR_T_LD, "d1", MIR_T_LD, "d2");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, LDNE, mir_ldne);
    return 2;
  case MIR_LDLT:
    res_type = MIR_T_I64;
    *proto_item = _MIR_builtin_proto (ctx, curr_func_item->module, LDLT_P, 1, &res_type, 2,
                                      MIR_T_LD, "d1", MIR_T_LD, "d2");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, LDLT, mir_ldlt);
    return 2;
  case MIR_LDGE:
    res_type = MIR_T_I64;
    *proto_item = _MIR_builtin_proto (ctx, curr_func_item->module, LDGE_P, 1, &res_type, 2,
                                      MIR_T_LD, "d1", MIR_T_LD, "d2");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, LDGE, mir_ldge);
    return 2;
  case MIR_LDGT:
    res_type = MIR_T_I64;
    *proto_item = _MIR_builtin_proto (ctx, curr_func_item->module, LDGT_P, 1, &res_type, 2,
                                      MIR_T_LD, "d1", MIR_T_LD, "d2");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, LDGT, mir_ldgt);
    return 2;
  case MIR_LDLE:
    res_type = MIR_T_I64;
    *proto_item = _MIR_builtin_proto (ctx, curr_func_item->module, LDLE_P, 1, &res_type, 2,
                                      MIR_T_LD, "d1", MIR_T_LD, "d2");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, LDLE, mir_ldle);
    return 2;
  case MIR_VA_ARG:
    res_type = MIR_T_I64;
    *proto_item = _MIR_builtin_proto (ctx, curr_func_item->module, VA_ARG_P, 1, &res_type, 2,
                                      MIR_T_I64, "va", MIR_T_I64, "type");
    *func_import_item = _MIR_builtin_func (ctx, curr_func_item->module, VA_ARG, va_arg_builtin);
    return 2;
  case MIR_VA_BLOCK_ARG:
    *proto_item
      = _MIR_builtin_proto (ctx, curr_func_item->module, VA_BLOCK_ARG_P, 0, NULL, 4, MIR_T_I64,
                            "res", MIR_T_I64, "va", MIR_T_I64, "size", MIR_T_I64, "ncase");
    *func_import_item
      = _MIR_builtin_func (ctx, curr_func_item->module, VA_BLOCK_ARG, va_block_arg_builtin);
    return 4;
  default: return 0;
  }
}

DEF_VARR (int);
DEF_VARR (uint8_t);
DEF_VARR (uint64_t);

struct insn_pattern_info {
  int start, num;
};

typedef struct insn_pattern_info insn_pattern_info_t;
DEF_VARR (insn_pattern_info_t);

struct label_ref {
  int abs_addr_p, short_p;
  size_t label_val_disp;
  MIR_label_t label;
};

typedef struct label_ref label_ref_t;
DEF_VARR (label_ref_t);

struct const_ref {
  uint64_t val;
  size_t const_addr_disp;
};

typedef struct const_ref const_ref_t;
DEF_VARR (const_ref_t);

DEF_VARR (MIR_code_reloc_t);

struct target_ctx {
  unsigned char alloca_p, block_arg_func_p, leaf_p;
  uint32_t non_vararg_int_args_num;
  size_t small_aggregate_save_area;
  VARR (int) * pattern_indexes;
  VARR (insn_pattern_info_t) * insn_pattern_info;
  VARR (uint8_t) * result_code;
  VARR (label_ref_t) * label_refs;
  VARR (const_ref_t) * const_refs;
  VARR (uint64_t) * abs_address_locs;
  VARR (MIR_code_reloc_t) * relocs;
};

#define alloca_p gen_ctx->target_ctx->alloca_p
#define block_arg_func_p gen_ctx->target_ctx->block_arg_func_p
#define leaf_p gen_ctx->target_ctx->leaf_p
#define non_vararg_int_args_num gen_ctx->target_ctx->non_vararg_int_args_num
#define small_aggregate_save_area gen_ctx->target_ctx->small_aggregate_save_area
#define pattern_indexes gen_ctx->target_ctx->pattern_indexes
#define insn_pattern_info gen_ctx->target_ctx->insn_pattern_info
#define result_code gen_ctx->target_ctx->result_code
#define label_refs gen_ctx->target_ctx->label_refs
#define const_refs gen_ctx->target_ctx->const_refs
#define abs_address_locs gen_ctx->target_ctx->abs_address_locs
#define relocs gen_ctx->target_ctx->relocs

static MIR_disp_t target_get_stack_slot_offset (gen_ctx_t gen_ctx, MIR_type_t type,
                                                MIR_reg_t slot) {
  /* slot is 0, 1, ... */
  size_t offset = curr_func_item->u.func->vararg_p || block_arg_func_p ? 32 : 16;

  return ((MIR_disp_t) slot * 8 + offset);
}

static int target_valid_mem_offset_p (gen_ctx_t gen_ctx, MIR_type_t type, MIR_disp_t offset) {
  MIR_disp_t offset2 = type == MIR_T_LD ? offset + 8 : offset;
  return -(1 << 11) <= offset && offset2 < (1 << 11);
}

static void target_machinize (gen_ctx_t gen_ctx) {
  MIR_context_t ctx = gen_ctx->ctx;
  MIR_func_t func;
  MIR_type_t type, mem_type, res_type;
  MIR_insn_code_t code, ext_code, new_insn_code;
  MIR_insn_t insn, next_insn, new_insn, anchor;
  MIR_var_t var;
  MIR_reg_t ret_reg, arg_reg;
  MIR_op_t ret_reg_op, arg_reg_op, mem_op, temp_op, treg_op;
  size_t i, int_arg_num, fp_arg_num, mem_size, qwords;

  assert (curr_func_item->item_type == MIR_func_item);
  func = curr_func_item->u.func;
  block_arg_func_p = FALSE;
  anchor = DLIST_HEAD (MIR_insn_t, func->insns);
  small_aggregate_save_area = 0;
  for (i = int_arg_num = fp_arg_num = mem_size = 0; i < func->nargs; i++) {
    /* Argument extensions is already done in simplify */
    /* Prologue: generate arg_var = hard_reg|stack mem|stack addr ... */
    var = VARR_GET (MIR_var_t, func->vars, i);
    type = var.type;
    if (MIR_blk_type_p (type) && (qwords = (var.size + 7) / 8) <= 2) {
      if (type == MIR_T_BLK + 1) int_arg_num = (int_arg_num + 1) / 2 * 2; /* Make even */
      if ((type == MIR_T_BLK + 2 && fp_arg_num < 8) || (type != MIR_T_BLK + 2 && int_arg_num < 8)) {
        MIR_insn_code_t mov_code = type == MIR_T_BLK + 2 ? MIR_DMOV : MIR_MOV;
        MIR_type_t mem_type = type == MIR_T_BLK + 2 ? MIR_T_D : MIR_T_I64;
        MIR_reg_t base_arg_reg = type == MIR_T_BLK + 2 ? base_arg_reg : A0_HARD_REG;
        size_t arg_reg_num = type == MIR_T_BLK + 2 ? fp_arg_num : int_arg_num;

        small_aggregate_save_area += qwords * 8;
        gen_assert (small_aggregate_save_area < (1 << 11));
        new_insn = MIR_new_insn (ctx, MIR_SUB, MIR_new_reg_op (ctx, i + 1),
                                 _MIR_new_hard_reg_op (ctx, FP_HARD_REG),
                                 MIR_new_int_op (ctx, small_aggregate_save_area));
        gen_add_insn_before (gen_ctx, anchor, new_insn);
        if (qwords == 0) continue;
        gen_mov (gen_ctx, anchor, mov_code, MIR_new_mem_op (ctx, mem_type, 0, i + 1, 0, 1),
                 _MIR_new_hard_reg_op (ctx, base_arg_reg + arg_reg_num));
        if (qwords == 2) {
          if (arg_reg_num < 7) {
            gen_mov (gen_ctx, anchor, mov_code, MIR_new_mem_op (ctx, MIR_T_I64, 8, i + 1, 0, 1),
                     _MIR_new_hard_reg_op (ctx, base_arg_reg + arg_reg_num + 1));
          } else {
            if (!block_arg_func_p) { /* t0 = prev sp */
              block_arg_func_p = TRUE;
              gen_mov (gen_ctx, anchor, MIR_MOV, _MIR_new_hard_reg_op (ctx, T0_HARD_REG),
                       _MIR_new_hard_reg_mem_op (ctx, MIR_T_I64, 16, FP_HARD_REG, MIR_NON_HARD_REG,
                                                 1));
            }
            treg_op
              = _MIR_new_hard_reg_op (ctx, type == MIR_T_BLK + 2 ? FT1_HARD_REG : T1_HARD_REG);
            gen_mov (gen_ctx, anchor, mov_code, treg_op,
                     _MIR_new_hard_reg_mem_op (ctx, mem_type, mem_size, T0_HARD_REG,
                                               MIR_NON_HARD_REG, 1));
            gen_mov (gen_ctx, anchor, mov_code, MIR_new_mem_op (ctx, mem_type, 8, i + 1, 0, 1),
                     treg_op);
            mem_size += 8;
          }
        }
        if (type == MIR_T_BLK + 2)
          fp_arg_num += qwords;
        else
          int_arg_num += qwords;
      } else {                   /* fully on stack -- use the address: */
        if (!block_arg_func_p) { /* t0 = prev sp */
          block_arg_func_p = TRUE;
          gen_mov (gen_ctx, anchor, MIR_MOV, _MIR_new_hard_reg_op (ctx, T0_HARD_REG),
                   _MIR_new_hard_reg_mem_op (ctx, MIR_T_I64, 16, FP_HARD_REG, MIR_NON_HARD_REG, 1));
        }
        new_insn
          = MIR_new_insn (ctx, MIR_ADD, MIR_new_reg_op (ctx, i + 1),
                          _MIR_new_hard_reg_op (ctx, T0_HARD_REG), MIR_new_int_op (ctx, mem_size));
        gen_add_insn_before (gen_ctx, anchor, new_insn);
        mem_size += qwords * 8;
      }
      continue;
    }
    arg_reg = get_arg_reg (type, FALSE, &int_arg_num, &fp_arg_num, &new_insn_code);
    if (arg_reg != MIR_NON_HARD_REG) {
      arg_reg_op = _MIR_new_hard_reg_op (ctx, arg_reg);
      gen_mov (gen_ctx, anchor, new_insn_code, MIR_new_reg_op (ctx, i + 1), arg_reg_op);
    } else { /* arg is on the stack or blk address is on the stack: */
      if (!block_arg_func_p) {
        block_arg_func_p = TRUE;
        gen_mov (gen_ctx, anchor, MIR_MOV, _MIR_new_hard_reg_op (ctx, T0_HARD_REG),
                 _MIR_new_hard_reg_mem_op (ctx, MIR_T_I64, 16, FP_HARD_REG, MIR_NON_HARD_REG, 1));
      }
      mem_type = type == MIR_T_F || type == MIR_T_D || type == MIR_T_LD ? type : MIR_T_I64;
      new_insn_code = (type == MIR_T_F    ? MIR_FMOV
                       : type == MIR_T_D  ? MIR_DMOV
                       : type == MIR_T_LD ? MIR_LDMOV
                                          : MIR_MOV);
      mem_op = _MIR_new_hard_reg_mem_op (ctx, mem_type, mem_size, T0_HARD_REG, MIR_NON_HARD_REG, 1);
      gen_mov (gen_ctx, anchor, new_insn_code, MIR_new_reg_op (ctx, i + 1), mem_op);
      mem_size += type == MIR_T_LD ? 16 : 8;
    }
  }
  non_vararg_int_args_num = int_arg_num;
  alloca_p = FALSE;
  leaf_p = TRUE;
  for (insn = DLIST_HEAD (MIR_insn_t, func->insns); insn != NULL; insn = next_insn) {
    MIR_item_t proto_item, func_import_item;
    int nargs;

    next_insn = DLIST_NEXT (MIR_insn_t, insn);
    code = insn->code;
    switch (code) {
    case MIR_FBEQ: code = MIR_FEQ; break;
    case MIR_FBNE: code = MIR_FNE; break;
    case MIR_FBLT: code = MIR_FLT; break;
    case MIR_FBGE: code = MIR_FGE; break;
    case MIR_FBGT: code = MIR_FGT; break;
    case MIR_FBLE: code = MIR_FLE; break;
    case MIR_DBEQ: code = MIR_DEQ; break;
    case MIR_DBNE: code = MIR_DNE; break;
    case MIR_DBLT: code = MIR_DLT; break;
    case MIR_DBGE: code = MIR_DGE; break;
    case MIR_DBGT: code = MIR_DGT; break;
    case MIR_DBLE: code = MIR_DLE; break;
    case MIR_LDBEQ: code = MIR_LDEQ; break;
    case MIR_LDBNE: code = MIR_LDNE; break;
    case MIR_LDBLT: code = MIR_LDLT; break;
    case MIR_LDBGE: code = MIR_LDGE; break;
    case MIR_LDBGT: code = MIR_LDGT; break;
    case MIR_LDBLE: code = MIR_LDLE; break;
    case MIR_EQS:
    case MIR_NES:
    case MIR_BEQS:
    case MIR_BNES:
    case MIR_LTS:
    case MIR_LES:
    case MIR_GTS:
    case MIR_GES:
    case MIR_BLTS:
    case MIR_BLES:
    case MIR_BGTS:
    case MIR_BGES: ext_code = MIR_EXT32; goto short_cmp;
    case MIR_ULTS:
    case MIR_ULES:
    case MIR_UGTS:
    case MIR_UGES:
    case MIR_UBLTS:
    case MIR_UBLES:
    case MIR_UBGTS:
    case MIR_UBGES:
      ext_code = MIR_UEXT32;
    short_cmp:
      temp_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
      new_insn = MIR_new_insn (ctx, ext_code, temp_op, insn->ops[1]);
      gen_add_insn_before (gen_ctx, insn, new_insn);
      treg_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
      new_insn = MIR_new_insn (ctx, ext_code, treg_op, insn->ops[2]);
      gen_add_insn_before (gen_ctx, insn, new_insn);
      insn->ops[1] = temp_op;
      insn->ops[2] = treg_op;
      break;
    default: break;
    }
    if (code != insn->code) {
      temp_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
      new_insn = MIR_new_insn (ctx, code, temp_op, insn->ops[1], insn->ops[2]);
      gen_add_insn_before (gen_ctx, insn, new_insn);
      next_insn = MIR_new_insn (ctx, MIR_BT, insn->ops[0], temp_op);
      gen_add_insn_after (gen_ctx, new_insn, next_insn);
      gen_delete_insn (gen_ctx, insn);
      insn = new_insn;
    }
    if ((nargs = get_builtin (gen_ctx, code, &proto_item, &func_import_item)) > 0) {
      if (code == MIR_VA_ARG || code == MIR_VA_BLOCK_ARG) {
        /* Use a builtin func call:
           mov func_reg, func ref; [mov reg3, type;] call proto, func_reg, res_reg, va_reg,
           reg3 */
        MIR_op_t ops[6], func_reg_op, reg_op3;
        MIR_op_t res_reg_op = insn->ops[0], va_reg_op = insn->ops[1], op3 = insn->ops[2];

        assert (res_reg_op.mode == MIR_OP_REG && va_reg_op.mode == MIR_OP_REG
                && op3.mode == (code == MIR_VA_ARG ? MIR_OP_MEM : MIR_OP_REG));
        func_reg_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
        reg_op3 = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
        next_insn = new_insn
          = MIR_new_insn (ctx, MIR_MOV, func_reg_op, MIR_new_ref_op (ctx, func_import_item));
        gen_add_insn_before (gen_ctx, insn, new_insn);
        if (code == MIR_VA_ARG) {
          new_insn
            = MIR_new_insn (ctx, MIR_MOV, reg_op3, MIR_new_int_op (ctx, (int64_t) op3.u.mem.type));
          op3 = reg_op3;
          gen_add_insn_before (gen_ctx, insn, new_insn);
        }
        ops[0] = MIR_new_ref_op (ctx, proto_item);
        ops[1] = func_reg_op;
        ops[2] = res_reg_op;
        ops[3] = va_reg_op;
        ops[4] = op3;
        if (code == MIR_VA_BLOCK_ARG) ops[5] = insn->ops[3];
        new_insn = MIR_new_insn_arr (ctx, MIR_CALL, code == MIR_VA_ARG ? 5 : 6, ops);
        gen_add_insn_before (gen_ctx, insn, new_insn);
        gen_delete_insn (gen_ctx, insn);
      } else { /* Use builtin: mov freg, func ref; call proto, freg, res_reg, op_reg[, op_reg2] */
        MIR_op_t freg_op, res_reg_op = insn->ops[0], op_reg_op = insn->ops[1], ops[5];

        assert (res_reg_op.mode == MIR_OP_REG && op_reg_op.mode == MIR_OP_REG);
        freg_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
        next_insn = new_insn
          = MIR_new_insn (ctx, MIR_MOV, freg_op, MIR_new_ref_op (ctx, func_import_item));
        gen_add_insn_before (gen_ctx, insn, new_insn);
        ops[0] = MIR_new_ref_op (ctx, proto_item);
        ops[1] = freg_op;
        ops[2] = res_reg_op;
        ops[3] = op_reg_op;
        if (nargs == 2) ops[4] = insn->ops[2];
        new_insn = MIR_new_insn_arr (ctx, MIR_CALL, nargs + 3, ops);
        gen_add_insn_before (gen_ctx, insn, new_insn);
        gen_delete_insn (gen_ctx, insn);
      }
    } else if (code == MIR_VA_START) {
      MIR_op_t prev_sp_op = MIR_new_reg_op (ctx, gen_new_temp_reg (gen_ctx, MIR_T_I64, func));
      MIR_op_t va_op = insn->ops[0];
      MIR_reg_t va_reg;

      assert (func->vararg_p && va_op.mode == MIR_OP_REG);
      va_reg = va_op.u.reg;
      /* Insns can be not simplified as soon as they match a machine insn.  */
      /* __stack: prev_sp = mem64[fp + 16] */
      gen_mov (gen_ctx, insn, MIR_MOV, prev_sp_op,
               _MIR_new_hard_reg_mem_op (ctx, MIR_T_I64, 16, FP_HARD_REG, MIR_NON_HARD_REG, 1));
      if (non_vararg_int_args_num != 8)
        gen_add_insn_before (gen_ctx, insn,
                             MIR_new_insn (ctx, MIR_ADD, prev_sp_op, prev_sp_op,
                                           MIR_new_int_op (ctx,
                                                           ((uint64_t) non_vararg_int_args_num - 8)
                                                             * 8)));
      gen_mov (gen_ctx, insn, MIR_MOV, MIR_new_mem_op (ctx, MIR_T_I64, 0, va_reg, 0, 1),
               prev_sp_op);
      gen_delete_insn (gen_ctx, insn);
    } else if (code == MIR_VA_END) { /* do nothing */
      gen_delete_insn (gen_ctx, insn);
    } else if (MIR_call_code_p (code)) {
      machinize_call (gen_ctx, insn);
      leaf_p = FALSE;
    } else if (code == MIR_ALLOCA) {
      alloca_p = TRUE;
    } else if (code == MIR_RET) {
      /* In simplify we already transformed code for one return insn
         and added extension insn (if any).  */
      uint32_t n_xregs = 0, n_fpregs = 0;

      assert (func->nres == MIR_insn_nops (ctx, insn));
      for (size_t i = 0; i < func->nres; i++) {
        assert (insn->ops[i].mode == MIR_OP_REG);
        res_type = func->res_types[i];
        if ((res_type == MIR_T_F || res_type == MIR_T_D) && n_fpregs < 2) {
          new_insn_code = res_type == MIR_T_F ? MIR_FMOV : MIR_DMOV;
          ret_reg = FA0_HARD_REG + n_fpregs++;
        } else if (n_xregs < 2) {
          new_insn_code = res_type == MIR_T_LD ? MIR_LDMOV : MIR_MOV;
          ret_reg = A0_HARD_REG + n_xregs++;
          if (res_type == MIR_T_LD) n_xregs++;
        } else {
          (*MIR_get_error_func (ctx)) (MIR_ret_error,
                                       "aarch64 can not handle this combination of return values");
        }
        ret_reg_op = _MIR_new_hard_reg_op (ctx, ret_reg);
        gen_mov (gen_ctx, insn, new_insn_code, ret_reg_op, insn->ops[i]);
        insn->ops[i] = ret_reg_op;
      }
    }
  }
}

static void isave (gen_ctx_t gen_ctx, MIR_insn_t anchor, int disp, MIR_reg_t base,
                   MIR_reg_t hard_reg) {
  gen_mov (gen_ctx, anchor, MIR_MOV,
           _MIR_new_hard_reg_mem_op (gen_ctx->ctx, MIR_T_I64, disp, base, MIR_NON_HARD_REG, 1),
           _MIR_new_hard_reg_op (gen_ctx->ctx, hard_reg));
}

static void target_make_prolog_epilog (gen_ctx_t gen_ctx, bitmap_t used_hard_regs,
                                       size_t stack_slots_num) {
  MIR_context_t ctx = gen_ctx->ctx;
  MIR_func_t func;
  MIR_insn_t anchor, new_insn;
  MIR_op_t sp_reg_op, fp_reg_op, treg_op, treg_op2;
  MIR_reg_t base_reg;
  int64_t start;
  int save_prev_stack_p;
  size_t i, offset, frame_size, frame_size_after_saved_regs, saved_iregs_num, saved_fregs_num;

  assert (curr_func_item->item_type == MIR_func_item);
  func = curr_func_item->u.func;
  for (i = saved_iregs_num = saved_fregs_num = 0; i <= MAX_HARD_REG; i++)
    if (!target_call_used_hard_reg_p (i, MIR_T_UNDEF) && bitmap_bit_p (used_hard_regs, i)
        && i != FP_HARD_REG) {
      if (i < F0_HARD_REG)
        saved_iregs_num++;
      else
        saved_fregs_num++;
    }
  if (leaf_p && !alloca_p && saved_iregs_num == 0 && saved_fregs_num == 0 && !func->vararg_p
      && stack_slots_num == 0 && !block_arg_func_p && small_aggregate_save_area == 0
      && !bitmap_bit_p (used_hard_regs, RA_HARD_REG))
    return;
  sp_reg_op = _MIR_new_hard_reg_op (ctx, SP_HARD_REG);
  fp_reg_op = _MIR_new_hard_reg_op (ctx, FP_HARD_REG);
  /* Prologue: */
  anchor = DLIST_HEAD (MIR_insn_t, func->insns);
  frame_size = 0;
  if (func->vararg_p && non_vararg_int_args_num < 8) /* space for vararg int regs (a<n>..a7): */
    frame_size = (8 - non_vararg_int_args_num) * 8;
  for (i = 0; i <= MAX_HARD_REG; i++)
    if (!target_call_used_hard_reg_p (i, MIR_T_UNDEF) && bitmap_bit_p (used_hard_regs, i))
      frame_size += 8;
  if (frame_size % 16 != 0) frame_size = (frame_size + 15) / 16 * 16;
  frame_size_after_saved_regs = frame_size;
  frame_size += stack_slots_num * 8;
  if (frame_size % 16 != 0) frame_size = (frame_size + 15) / 16 * 16;
  save_prev_stack_p = func->vararg_p || block_arg_func_p;
  treg_op = _MIR_new_hard_reg_op (ctx, T1_HARD_REG);
  if (save_prev_stack_p) { /* the 1st insn: putting stack pointer into T1: */
    gen_mov (gen_ctx, anchor, MIR_MOV, treg_op, sp_reg_op);
    frame_size += 16;
  }
  frame_size += 16; /* ra/fp */
  if (frame_size < (1 << 11)) {
    new_insn = MIR_new_insn (ctx, MIR_SUB, sp_reg_op, sp_reg_op, MIR_new_int_op (ctx, frame_size));
  } else {
    treg_op2 = _MIR_new_hard_reg_op (ctx, T2_HARD_REG);
    new_insn = MIR_new_insn (ctx, MIR_MOV, treg_op2, MIR_new_int_op (ctx, frame_size));
    gen_add_insn_before (gen_ctx, anchor, new_insn); /* t = frame_size */
    new_insn = MIR_new_insn (ctx, MIR_SUB, sp_reg_op, sp_reg_op, treg_op2);
  }
  gen_add_insn_before (gen_ctx, anchor, new_insn); /* sp = sp - (frame_size|t) */
  if (save_prev_stack_p)                           /* save prev sp value which is in T1: */
    gen_mov (gen_ctx, anchor, MIR_MOV,
             _MIR_new_hard_reg_mem_op (ctx, MIR_T_I64, 16, SP_HARD_REG, MIR_NON_HARD_REG, 1),
             treg_op); /* mem[sp + 16] = t1 */
  gen_mov (gen_ctx, anchor, MIR_MOV,
           _MIR_new_hard_reg_mem_op (ctx, MIR_T_I64, 8, SP_HARD_REG, MIR_NON_HARD_REG, 1),
           _MIR_new_hard_reg_op (ctx, LINK_HARD_REG)); /* mem[sp + 8] = ra */
  gen_mov (gen_ctx, anchor, MIR_MOV,
           _MIR_new_hard_reg_mem_op (ctx, MIR_T_I64, 0, SP_HARD_REG, MIR_NON_HARD_REG, 1),
           _MIR_new_hard_reg_op (ctx, FP_HARD_REG));        /* mem[sp] = fp */
  gen_mov (gen_ctx, anchor, MIR_MOV, fp_reg_op, sp_reg_op); /* fp = sp */
  if (func->vararg_p && non_vararg_int_args_num < 8) {      /* save vararg int regs: */
    MIR_reg_t base = SP_HARD_REG;
    int reg_save_area_size = 8 * (8 - non_vararg_int_args_num);

    start = (int64_t) frame_size - reg_save_area_size;
    if (start + reg_save_area_size >= (1 << 11)) {
      new_insn = MIR_new_insn (ctx, MIR_MOV, treg_op, MIR_new_int_op (ctx, start));
      gen_add_insn_before (gen_ctx, anchor, new_insn); /* t = frame_size - reg_save_area_size */
      start = 0;
      base = T1_HARD_REG;
    }
    for (MIR_reg_t r = non_vararg_int_args_num + A0_HARD_REG; r <= A7_HARD_REG; r++, start += 8)
      isave (gen_ctx, anchor, start, base, r);
  }
  /* Saving callee saved hard registers: */
  offset = frame_size - frame_size_after_saved_regs;
  if (offset + MAX_HARD_REG * 8 < (1 << 11)) {
    base_reg = FP_HARD_REG;
  } else {
    base_reg = T2_HARD_REG;
    new_insn = gen_mov (gen_ctx, anchor, MIR_MOV, _MIR_new_hard_reg_op (ctx, base_reg),
                        MIR_new_int_op (ctx, offset));
    new_insn = gen_mov (gen_ctx, anchor, MIR_ADD, _MIR_new_hard_reg_op (ctx, base_reg),
                        _MIR_new_hard_reg_op (ctx, FP_HARD_REG));
    offset = 0;
  }
  for (i = 0; i <= MAX_HARD_REG; i++)
    if (!target_call_used_hard_reg_p (i, MIR_T_UNDEF) && bitmap_bit_p (used_hard_regs, i)
        && i != FP_HARD_REG) {
      if (i < F0_HARD_REG) {
        gen_assert (offset < (1 << 11));
        gen_mov (gen_ctx, anchor, MIR_MOV,
                 _MIR_new_hard_reg_mem_op (ctx, MIR_T_I64, offset, base_reg, MIR_NON_HARD_REG, 1),
                 _MIR_new_hard_reg_op (ctx, i));
        offset += 8;
      } else {
        // if (offset % 16 != 0) offset = (offset + 15) / 16 * 16;
        gen_assert (offset < (1 << 11));
        new_insn
          = gen_mov (gen_ctx, anchor, MIR_DMOV,
                     _MIR_new_hard_reg_mem_op (ctx, MIR_T_D, offset, base_reg, MIR_NON_HARD_REG, 1),
                     _MIR_new_hard_reg_op (ctx, i));
        offset += 8;
      }
    }
  if (small_aggregate_save_area != 0) {
    if (small_aggregate_save_area % 16 != 0)
      small_aggregate_save_area = (small_aggregate_save_area + 15) / 16 * 16;
    new_insn = MIR_new_insn (ctx, MIR_SUB, sp_reg_op, sp_reg_op,
                             MIR_new_int_op (ctx, small_aggregate_save_area));
    gen_add_insn_before (gen_ctx, anchor, new_insn); /* sp -= <small aggr save area size> */
  }
  /* Epilogue: */
  anchor = DLIST_TAIL (MIR_insn_t, func->insns);
  /* It might be infinite loop after CCP with dead code elimination: */
  if (anchor->code == MIR_JMP) return;
  assert (anchor->code == MIR_RET);
  /* Restoring hard registers: */
  offset = frame_size - frame_size_after_saved_regs;
  if (offset + MAX_HARD_REG * 8 < (1 << 11)) {
    base_reg = FP_HARD_REG;
  } else {
    base_reg = T2_HARD_REG;
    new_insn = gen_mov (gen_ctx, anchor, MIR_MOV, _MIR_new_hard_reg_op (ctx, base_reg),
                        MIR_new_int_op (ctx, offset));
    new_insn = gen_mov (gen_ctx, anchor, MIR_ADD, _MIR_new_hard_reg_op (ctx, base_reg),
                        _MIR_new_hard_reg_op (ctx, FP_HARD_REG));
    offset = 0;
  }
  for (i = 0; i <= MAX_HARD_REG; i++)
    if (!target_call_used_hard_reg_p (i, MIR_T_UNDEF) && bitmap_bit_p (used_hard_regs, i)
        && i != FP_HARD_REG) {
      if (i < F0_HARD_REG) {
        gen_assert (offset < (1 << 11));
        gen_mov (gen_ctx, anchor, MIR_MOV, _MIR_new_hard_reg_op (ctx, i),
                 _MIR_new_hard_reg_mem_op (ctx, MIR_T_I64, offset, base_reg, MIR_NON_HARD_REG, 1));
        offset += 8;
      } else {
        gen_assert (offset < (1 << 11));
        new_insn = gen_mov (gen_ctx, anchor, MIR_DMOV, _MIR_new_hard_reg_op (ctx, i),
                            _MIR_new_hard_reg_mem_op (ctx, MIR_T_D, offset, base_reg,
                                                      MIR_NON_HARD_REG, 1));
        offset += 8;
      }
    }
  /* Restore ra, sp, fp */
  gen_mov (gen_ctx, anchor, MIR_MOV, _MIR_new_hard_reg_op (ctx, LINK_HARD_REG),
           _MIR_new_hard_reg_mem_op (ctx, MIR_T_I64, 8, FP_HARD_REG, MIR_NON_HARD_REG, 1));
  if (frame_size < (1 << 11)) {
    new_insn = MIR_new_insn (ctx, MIR_ADD, sp_reg_op, fp_reg_op, MIR_new_int_op (ctx, frame_size));
  } else {
    new_insn = MIR_new_insn (ctx, MIR_MOV, treg_op, MIR_new_int_op (ctx, frame_size));
    gen_add_insn_before (gen_ctx, anchor, new_insn); /* t = frame_size */
    new_insn = MIR_new_insn (ctx, MIR_ADD, sp_reg_op, fp_reg_op, treg_op);
  }
  gen_add_insn_before (gen_ctx, anchor, new_insn); /* sp = fp + (frame_size|t) */
  gen_mov (gen_ctx, anchor, MIR_MOV, fp_reg_op,
           _MIR_new_hard_reg_mem_op (ctx, MIR_T_I64, 0, FP_HARD_REG, MIR_NON_HARD_REG, 1));
}

/* 32-bit insn formats:
|31             25|24 20|19 15|14        12|11             7|6     0|
|funct7           | rs2 | rs1 |funct3      | rd             | opcode|  :R-type
|imm[11:0]              | rs1 |funct3      | rd             | opcode|  :I-type
|imm[11:5]        | rs2 | rs1 |funct3      |imm[4:0]        | opcode|  :S-type
|imm[31:12]                                | rd             | opcode|  :U-type
|imm[12]imm[10-5] | rs2 | rs1 |funct3      |imm[4-1]imm[11] | opcode|  :B-type
|imm[20]imm[10:1]imm[11]imm[19-12]         | rd             | opcode|  :J-type

16-bits insns:
Format Meaning		      |15 14 13|12|11 10|9 8 7|6 5|4 3 2|1 0|
CR     Register	              | funct4    | rd/rs1    |    rs2  | op|
CI     Immediate	      | funct3 |im| rd/rs1    |    imm  | op|
CSS    Stack-relative Store   | funct3 |    imm       |    rs2  | op|
CIW    Wide Immediate	      | funct3 |    imm           | rd' | op|
CL     Load		      | funct3 |   imm  | rs1'|imm| rd' | op|
CS     Store		      | funct3 |   imm  | rs1'|imm| rs2'| op|
CB     Branch		      | funct3 | offset | rs1'| offset  | op|
CJ     Jump                   | funct3 |    jump target         | op|

RVC Register Number (rs1',rs2',rd')    000  001  010  011  100  101  110  111
Integer Register Number		       x8   x9   x10  x11  x12  x13  x14  x15
Integer Register ABI Name	       s0   s1   a0   a1   a2   a3   a4   a5
Floating-Point Register Number	       f8   f9   f10  f11  f12  f13  f14  f15
Floating-Point Register ABI Name       fs0  fs1  fa0  fa1  fa2  fa3  fa4  fa5

*/

struct pattern {
  MIR_insn_code_t code;
  /* Pattern elements:
     blank - ignore
     X - match everything
     $ - finish successfully matching
     r - register
     h[0-63] - hard register with given number
     c<number> - immediate integer <number>

       memory with immediate offset:
     m[0-3] - int (signed or unsigned) type memory of size 8,16,32,64-bits
     ms[0-3] - signed int type memory of size 8,16,32,64-bits
     mu[0-3] - unsigned int type memory of size 8,16,32,64-bits
       sign extended 12-bit offset

       memory with immediate offset:
     mf - memory of float
     md - memory of double
     mld - memory of long double (whose disp can be increased by 8)
       sign extended 12-bit offset

     i -- 2nd or 3rd immediate op for arithemtic insn (12-bit signed)
     j -- as i but -j should be also i (it means excluding minimal 12-bit signed) and only 3rd op
     ju -- as j but but rounded to 16 first and only 2nd op
     iu -- 32-bit signed immediate for arithemtic insn with zero 12 bits as 2nd op
     ia -- any 32-bit signed immediate as 2nd op
     I --  any 64-bit immediate
     s --  immediate shift (5 bits) as 3th op
     S --  immediate shift (6 bits) as 3th op
     l --  label as the 1st or 2nd op which can be present by signed 13-bit pc offset

     Remember we have no float or (long) double immediate at this stage. They are represented
     by a reference to data item.  */

  const char *pattern;
  /* Replacement elements:
     blank - ignore
     ; - insn separation
     Ohex - opcode [6..0]
     Fhex - funct3 (or round mode rm) [14..12]
     fhex - funct7 [31..25]
     ghex - funct7 w/o 1 bit [31..26]

     rd[0-2] - put n-th operand register into rd field [11..7]
     rs[0-2] - put n-th operand register into rs1 field [19..15]
     rS[0-2] - put n-th operand register into rs2 field [24..20]

     h(d,s,S)<one or two hex digits> - hardware register with given number in rd,rs1,rs2 field
     m = 1st or 2nd operand is (8-,16-,32-,64-bit) mem with base and signed disp

     ml = 1st or 2nd operand for load is mem with base (rs1), signed imm12 disp [31..20]
     ms = 1st or 2nd operand for store is mem with base (rs1), signed imm12 disp [31..25,11..7]

     i -- 2nd or 3rd arithmetic op 12-bit immediate [31..20]
     j -- 3rd arithmetic op 12-bit immediate [31..20] with opposite sign
     ju -- j but j round up to 16 first and used only as 2nd operand
     iu -- 2nd arithmetic op immediate [31..12]
     ih -- 20-bit upper part [31..12] of 32-bit signed 2nd op
     il -- 12-bit lower part [31..20] of 32-bit signed 2nd op
     I -- 20-bit upper part [31..12] of 32-bit signed pc-relative address of 64 bit
          constant (2nd op) in the 1st word and 12-bit lower part [31..20] in the 2nd word
     s --  immediate shift [24-20]
     S --  immediate shift [25-20]
     shex --  immediate shift value [24-20]
     Shex --  immediate shift value [25-20]
     i[-]hex -- i with given value
     iuhex -- 20-bit immediate [31..12]
     T - 12-bit immediate which is 16 + alignment of the insn addr + 8 to 8 == (0,2,4,6)

     l -- operand-label as signed 13-bit offset ([12|10:5] as [31:25] and [4:1|11] as [11:7]),
          remember address of any insn is even
     L -- operand-label as signed 21-bit offset ([20|10:1|11|19:12] as [31:12])
  */
  const char *replacement;
};

static const struct pattern patterns[] = {
  {MIR_MOV, "r r", "O13 F0 rd0 rs1 i0"}, /* addi rd,rs1,0 */
  {MIR_MOV, "r m3", "O3 F3 rd0 ml"},     /* ld rd,m */
  {MIR_MOV, "m3 r", "O23 F3 rS1 ms"},    /* sd rs2,m */

  {MIR_MOV, "r ms2", "O3 F2 rd0 ml"}, /* lw rd,m */
  {MIR_MOV, "r mu2", "O3 F6 rd0 ml"}, /* lwu rd,m */
  {MIR_MOV, "m2 r", "O23 F2 rS1 ms"}, /* sw rs2,m */

  {MIR_MOV, "r ms1", "O3 F1 rd0 ml"}, /* lh rd,m */
  {MIR_MOV, "r mu1", "O3 F5 rd0 ml"}, /* lhu rd,m */
  {MIR_MOV, "m1 r", "O23 F1 rS1 ms"}, /* sh rs2,m */

  {MIR_MOV, "r ms0", "O3 F0 rd0 ml"}, /* lb rd,m */
  {MIR_MOV, "r mu0", "O3 F4 rd0 ml"}, /* lbu rd,m */
  {MIR_MOV, "m0 r", "O23 F0 rS1 ms"}, /* sb rs2,m */

  {MIR_MOV, "r i", "O13 F0 rd0 hs0 i"}, /* addi r,zero,i */
  {MIR_MOV, "r iu", "O37 rd0 iu"},      /* lui r,i */
  //  {MIR_MOV, "r ia", "O37 rd0 ih; O13 F0 rd0 rs0 il"}, /* lui r,i; addi r,r,i */
  {MIR_MOV, "r I", "O17 rd0 I; O3 F3 rd0 rs0"}, /* auipc r,rel-caddr; ld r,rel-caddr(r) */

  {MIR_FMOV, "r r", "O53 F0 f10 rd0 rs1 rS1"}, /* fsgnj.s rd,rs1,rs2 */
  {MIR_FMOV, "r mf", "O7 F2 rd0 ml"},          /* flw rd,m */
  {MIR_FMOV, "mf r", "O27 F2 rS1 ms"},         /* fsw rd,m */

  {MIR_DMOV, "r r", "O53 F0 f11 rd0 rs1 rS1"}, /* fsgnj.d rd,rs1,rs2 */
  {MIR_DMOV, "r md", "O7 F3 rd0 ml"},          /* fld rd,m */
  {MIR_DMOV, "md r", "O27 F3 rS1 ms"},         /* fsd rd,m */

  /* LD values are always kept in memory.  We place them into int hard regs for passing
     args/returning values (see machinize).  We don't need insn replacement as we split
     load moves in target_translate: */
  {MIR_LDMOV, "r mld", ""}, /* int_reg <- mem */
  {MIR_LDMOV, "mld r", ""}, /* mem <- int_reg */
  /* mem <- mem by using temp fp regs: */
  {MIR_LDMOV, "mld mld", ""},

#define STR(c) #c
#define STR_VAL(c) STR (c)

  {MIR_UNSPEC, "c" STR_VAL (FMVXW_CODE) " r r", "O53 F0 f70 rd1 rs2"}, /* fmv.x.w r0,r1 */
  {MIR_UNSPEC, "c" STR_VAL (FMVXD_CODE) " r r", "O53 F0 f71 rd1 rs2"}, /* fmv.x.d r0,r1 */

  {MIR_EXT8, "r r",
   "O13 F1 rd0 rs1 S38; O13 F5 f20 rd0 rs0 S38"}, /* slli rd,rs1,56;srai rd,rs1,56 */
  {MIR_EXT16, "r r",
   "O13 F1 rd0 rs1 S30; O13 F5 f20 rd0 rs0 S30"}, /* slli rd,rs1,48;srai rd,rs1,48 */
  {MIR_EXT32, "r r", "O1b F0 rd0 rs1 i0"},        /* addiw rd,rs1,0 */

  {MIR_UEXT8, "r r",
   "O13 F1 rd0 rs1 S38; O13 F5 f0 rd0 rs0 S38"}, /* slli rd,rs1,56;srli rd,rs1,56 */
  {MIR_UEXT16, "r r",
   "O13 F1 rd0 rs1 S30; O13 F5 f0 rd0 rs0 S30"}, /* slli rd,rs1,48;srli rd,rs1,48 */
  {MIR_UEXT32, "r r",
   "O13 F1 rd0 rs1 S20; O13 F5 f0 rd0 rs0 S20"}, /* slli rd,rs1,32;srli rd,rs1,32 */

  {MIR_ADD, "r r r", "O33 F0 rd0 rs1 rS2"},     /* add rd,rs1,rs2 */
  {MIR_ADD, "r r i", "O13 F0 rd0 rs1 i"},       /* addi rd,rs1,i */
  {MIR_ADDS, "r r r", "O3b F0 rd0 rs1 rS2"},    /* addw rd,rs1,rs2 */
  {MIR_ADDS, "r r i", "O1b F0 rd0 rs1 i"},      /* addiw rd,rs1,i */
  {MIR_FADD, "r r r", "O53 F7 f0 rd0 rs1 rS2"}, /* fadd.s rd,rs1,rs2 */
  {MIR_DADD, "r r r", "O53 F7 f1 rd0 rs1 rS2"}, /* fadd.d rd,rs1,rs2 */
  // ldadd is implemented through builtin

  {MIR_SUB, "r r r", "O33 F0 f20 rd0 rs1 rS2"},  /* sub rd,rs1,rs2 */
  {MIR_SUB, "r r j", "O13 F0 rd0 rs1 j"},        /* addi rd,rs1,-j */
  {MIR_SUBS, "r r r", "O3b F0 f20 rd0 rs1 rS2"}, /* subw rd,rs1,rs2 */
  {MIR_SUBS, "r r j", "O1b F0 rd0 rs1 j"},       /* addiw rd,rs1,-j */
  {MIR_FSUB, "r r r", "O53 F7 f4 rd0 rs1 rS2"},  /* fsub.s rd,rs1,rs2 */
  {MIR_DSUB, "r r r", "O53 F7 f5 rd0 rs1 rS2"},  /* fsub.d rd,rs1,rs2 */
  // ldsub is implemented through builtin

  {MIR_MUL, "r r r", "O33 F0 f1 rd0 rs1 rS2"},  /* mul rd,rs1,rs2 */
  {MIR_MULS, "r r r", "O3b F0 f1 rd0 rs1 rS2"}, /* mulw rd,rs1,rs2 */
  {MIR_FMUL, "r r r", "O53 F7 f8 rd0 rs1 rS2"}, /* fmul.s rd,rs1,rs2*/
  {MIR_DMUL, "r r r", "O53 F7 f9 rd0 rs1 rS2"}, /* fmul.d rd,rs1,rs2*/
  // ldmul is implemented through builtin

  {MIR_DIV, "r r r", "O33 F4 f1 rd0 rs1 rS2"},   /* div rd,rs1,rs2 */
  {MIR_DIVS, "r r r", "O3b F4 f1 rd0 rs1 rS2"},  /* divw rd,rs1,rs2 */
  {MIR_UDIV, "r r r", "O33 F5 f1 rd0 rs1 rS2"},  /* divu rd,rs1,rs2 */
  {MIR_UDIVS, "r r r", "O3b F5 f1 rd0 rs1 rS2"}, /* divuw rd,rs1,rs2 */
  {MIR_FDIV, "r r r", "O53 F7 fc rd0 rs1 rS2"},  /* fdiv.s rd,rs1,rs2*/
  {MIR_DDIV, "r r r", "O53 F7 fd rd0 rs1 rS2"},  /* fdiv.d rd,rs1,rs2*/
  // lddiv is implemented through builtin

  {MIR_MOD, "r r r", "O33 F6 f1 rd0 rs1 rS2"},   /* rem rd,rs1,rs2 */
  {MIR_MODS, "r r r", "O3b F6 f1 rd0 rs1 rS2"},  /* remw rd,rs1,rs2 */
  {MIR_UMOD, "r r r", "O33 F7 f1 rd0 rs1 rS2"},  /* remu rd,rs1,rs2 */
  {MIR_UMODS, "r r r", "O3b F7 f1 rd0 rs1 rS2"}, /* remuw rd,rs1,rs2 */

  {MIR_EQ, "r r r",
   "O33 F0 f20 rd0 rs1 rS2; O13 F3 rd0 rs0 i1"},            /* sub rd,rs1,rs2; sltiu rd,rs1,1 */
  {MIR_EQ, "r r j", "O13 F0 rd0 rs1 j; O13 F3 rd0 rs0 i1"}, /* addi rd,rs1,-j; sltiu rd,rs1,1 */
  {MIR_EQS, "r r r",
   "O3b F0 f20 rd0 rs1 rS2; O13 F3 rd0 rs0 i1"},             /* subw rd,rs1,rs2; sltiu rd,rs1,1 */
  {MIR_EQS, "r r j", "O1b F0 rd0 rs1 j; O13 F3 rd0 rs0 i1"}, /* addiw rd,rs1,-j; sltiu rd,rs1,1 */

  {MIR_NE, "r r r",
   "O33 F0 f20 rd0 rs1 rS2; O33 F3 rd0 hs0 rS0"},            /* sub rd,rs1,rs2; sltu rd,z,rs2 */
  {MIR_NE, "r r j", "O13 F0 rd0 rs1 j; O33 F3 rd0 hs0 rS0"}, /* addi rd,rs1,-j; sltu rd,z,rs2 */
  {MIR_NES, "r r r",
   "O33 F0 f20 rd0 rs1 rS2; O33 F3 rd0 hs0 rS0"},             /* sub rd,rs1,rs2; sltu rd,z,rs2 */
  {MIR_NES, "r r j", "O13 F0 rd0 rs1 j; O33 F3 rd0 hs0 rS0"}, /* addi rd,rs1,-j; sltu rd,z,rs2 */

  {MIR_LT, "r r r", "O33 F2 f0 rd0 rs1 rS2"},   /* slt rd,rs1,rs2 */
  {MIR_LT, "r r i", "O13 F2 f0 rd0 rs1 i"},     /* slti rd,rs1,i */
  {MIR_LTS, "r r r", "O33 F2 f0 rd0 rs1 rS2"},  /* slt rd,rs1,rs2 */
  {MIR_LTS, "r r i", "O13 F2 f0 rd0 rs1 i"},    /* slti rd,rs1,i */
  {MIR_ULT, "r r r", "O33 F3 f0 rd0 rs1 rS2"},  /* sltu rd,rs1,rs2 */
  {MIR_ULT, "r r i", "O13 F3 f0 rd0 rs1 i"},    /* sltiu rd,rs1,i */
  {MIR_ULTS, "r r r", "O33 F3 f0 rd0 rs1 rS2"}, /* sltu rd,rs1,rs2 */
  {MIR_ULTS, "r r i", "O13 F3 f0 rd0 rs1 i"},   /* sltiu rd,rs1,i */

  // ??? le r,imm -> lt r,imm+1
  /* !(op2 < op1)
  /* sgt rd,rs1,rs2;xori rd,rs1,1 */
  {MIR_LE, "r r r", "O33 F2 f0 rd0 rs2 rS1; O13 F4 f0 rd0 rs0 i1"},
  /* sgti rd,rs1,i;xori rd,rs1,1 */
  {MIR_LE, "r i r", "O13 F2 f0 rd0 rs2 i; O13 F4 f0 rd0 rs0 i1"},
  /* sgt rd,rs1,rs2;xori rd,rs1,1 */
  {MIR_LES, "r r r", "O33 F2 f0 rd0 rs2 rS1; O13 F4 f0 rd0 rs0 i1"},
  /* sgti rd,rs1,i;xori rd,rs1,1 */
  {MIR_LES, "r i r", "O13 F2 f0 rd0 rs2 i; O13 F4 f0 rd0 rs0 i1"},
  /* sgtu rd,rs1,rs2;xori rd,rs1,1 */
  {MIR_ULE, "r r r", "O33 F3 f0 rd0 rs2 rS1; O13 F4 f0 rd0 rs0 i1"},
  /* sgtui rd,rs1,i;xori rd,rs1,1 */
  {MIR_ULE, "r i r", "O13 F3 f0 rd0 rs2 i; O13 F4 f0 rd0 rs0 i1"},
  /* sgtu rd,rs1,rs2;xori rd,rs1,1 */
  {MIR_ULES, "r r r", "O33 F3 f0 rd0 rs2 rS1; O13 F4 f0 rd0 rs0 i1"},
  /* sgtui rd,rs1,i;xori rd,rs1,1 */
  {MIR_ULES, "r i r", "O13 F3 f0 rd0 rs2 i; O13 F4 f0 rd0 rs0 i1"},

  {MIR_GT, "r r r", "O33 F2 f0 rd0 rs2 rS1"},   /* slt rd,rs1,rs2 */
  {MIR_GT, "r i r", "O13 F2 f0 rd0 rs2 i"},     /* slti rd,rs1,i */
  {MIR_GTS, "r r r", "O33 F2 f0 rd0 rs2 rS1"},  /* slt rd,rs1,rs2 */
  {MIR_GTS, "r i r", "O13 F2 f0 rd0 rs2 i"},    /* slti rd,rs1,i */
  {MIR_UGT, "r r r", "O33 F3 f0 rd0 rs2 rS1"},  /* sltu rd,rs1,rs2 */
  {MIR_UGT, "r i r", "O13 F3 f0 rd0 rs2 i"},    /* sltiu rd,rs1,i */
  {MIR_UGTS, "r r r", "O33 F3 f0 rd0 rs2 rS1"}, /* sltu rd,rs1,rs2 */
  {MIR_UGTS, "r i r", "O13 F3 f0 rd0 rs2 i"},   /* sltiu rd,rs1,i */

  /* slt rd,rs1,rs2;xori rd,rs1,1 */
  {MIR_GE, "r r r", "O33 F2 f0 rd0 rs1 rS2; O13 F4 f0 rd0 rs0 i1"},
  /* slti rd,rs1,i;xori rd,rs1,1 */
  {MIR_GE, "r r i", "O13 F2 f0 rd0 rs1 i; O13 F4 f0 rd0 rs0 i1"},
  /* slt rd,rs1,rs2;xori rd,rs1,1 */
  {MIR_GES, "r r r", "O33 F2 f0 rd0 rs1 rS2; O13 F4 f0 rd0 rs0 i1"},
  /* slti rd,rs1,i;xori rd,rs1,1 */
  {MIR_GES, "r r i", "O13 F2 f0 rd0 rs1 i; O13 F4 f0 rd0 rs0 i1"},
  /* sltu rd,rs1,rs2;xori rd,rs1,1 */
  {MIR_UGE, "r r r", "O33 F3 f0 rd0 rs1 rS2; O13 F4 f0 rd0 rs0 i1"},
  /* sltui rd,rs1,i;xori rd,rs1,1 */
  {MIR_UGE, "r r i", "O13 F3 f0 rd0 rs1 i; O13 F4 f0 rd0 rs0 i1"},
  /* sltu rd,rs1,rs2;xori rd,rs1,1 */
  {MIR_UGES, "r r r", "O33 F3 f0 rd0 rs1 rS2; O13 F4 f0 rd0 rs0 i1"},
  /* sltui rd,rs1,i;xori rd,rs1,1 */
  {MIR_UGES, "r r i", "O13 F3 f0 rd0 rs1 i; O13 F4 f0 rd0 rs0 i1"},

  {MIR_FEQ, "r r r", "O53 F2 f50 rd0 rs1 rS2"}, /* feq.s rd,rs1,rs2 */
  {MIR_DEQ, "r r r", "O53 F2 f51 rd0 rs1 rS2"}, /* feq.d rd,rs1,rs2 */
  {MIR_FNE, "r r r",
   "O53 F2 f50 rd0 rs1 rS2; O13 F4 rd0 rs0 i1"}, /* feq.s rd,rs1,rs2; xori rd,r1,1 */
  {MIR_DNE, "r r r",
   "O53 F2 f51 rd0 rs1 rS2; O13 F4 rd0 rs0 i1"}, /* feq.d rd,rs1,rs2;xori rd,rs1,1 */
  {MIR_FLT, "r r r", "O53 F1 f50 rd0 rs1 rS2"},  /* flt.s rd,rs1,rs2 */
  {MIR_DLT, "r r r", "O53 F1 f51 rd0 rs1 rS2"},  /* flt.d rd,rs1,rs2 */
  {MIR_FLE, "r r r", "O53 F0 f50 rd0 rs1 rS2"},  /* fle.s rd,rs1,rs2 */
  {MIR_DLE, "r r r", "O53 F0 f51 rd0 rs1 rS2"},  /* fle.d rd,rs1,rs2 */
  {MIR_FGT, "r r r", "O53 F1 f50 rd0 rs2 rS1"},  /* flt.s rd,rs1,rs2 */
  {MIR_DGT, "r r r", "O53 F1 f51 rd0 rs2 rS1"},  /* flt.d rd,rs1,rs2 */
  {MIR_FGE, "r r r", "O53 F0 f50 rd0 rs2 rS1"},  /* fle.s rd,rs1,rs2 */
  {MIR_DGE, "r r r", "O53 F0 f51 rd0 rs2 rS1"},  /* fle.d rd,rs1,rs2 */

  {MIR_JMP, "L", "O6f hd0 L"}, /* jal: 20-bit offset (w/o 1 bit) jmp */

  {MIR_BT, "l r", "O63 F1 rs1 hS0 l"},  /* bne rs1,zero,l */
  {MIR_BTS, "l r", "O63 F1 rs1 hS0 l"}, /* bne rs1,zero,l */
  {MIR_BF, "l r", "O63 F0 rs1 hS0 l"},  /* beq rs1,zero,l */
  {MIR_BFS, "l r", "O63 F0 rs1 hS0 l"}, /* beq rs1,zero,l */

  {MIR_BEQ, "l r r", "O63 F0 rs1 rS2 l"},  /* beq rs1,rs2,l */
  {MIR_BEQS, "l r r", "O63 F0 rs1 rS2 l"}, /* beq rs1,rs2,l */

  {MIR_BNE, "l r r", "O63 F1 rs1 rS2 l"},  /* bne rs1,rs2,l */
  {MIR_BNES, "l r r", "O63 F1 rs1 rS2 l"}, /* bne rs1,rs2,l */

  {MIR_BLT, "l r r", "O63 F4 rs1 rS2 l"},   /* blt rs1,rs2,l */
  {MIR_BLTS, "l r r", "O63 F4 rs1 rS2 l"},  /* blt rs1,rs2,l */
  {MIR_UBLT, "l r r", "O63 F6 rs1 rS2 l"},  /* bltu rs1,rs2,l */
  {MIR_UBLTS, "l r r", "O63 F6 rs1 rS2 l"}, /* bltu rs1,rs2,l */

  {MIR_BGE, "l r r", "O63 F5 rs1 rS2 l"},   /* bge rs1,rs2,l */
  {MIR_BGES, "l r r", "O63 F5 rs1 rS2 l"},  /* bge rs1,rs2,l */
  {MIR_UBGE, "l r r", "O63 F7 rs1 rS2 l"},  /* bgeu rs1,rs2,l */
  {MIR_UBGES, "l r r", "O63 F7 rs1 rS2 l"}, /* bgeu rs1,rs2,l */

  {MIR_BGT, "l r r", "O63 F4 rs2 rS1 l"},   /* blt rs1,rs2,l */
  {MIR_BGTS, "l r r", "O63 F4 rs2 rS1 l"},  /* blt rs1,rs2,l */
  {MIR_UBGT, "l r r", "O63 F6 rs2 rS1 l"},  /* bltu rs1,rs2,l */
  {MIR_UBGTS, "l r r", "O63 F6 rs2 rS1 l"}, /* bltu rs1,rs2,l */

  {MIR_BLE, "l r r", "O63 F5 rs2 rS1 l"},   /* bge rs1,rs2,l */
  {MIR_BLES, "l r r", "O63 F5 rs2 rS1 l"},  /* bge rs1,rs2,l */
  {MIR_UBLE, "l r r", "O63 F7 rs2 rS1 l"},  /* bgeu rs1,rs2,l */
  {MIR_UBLES, "l r r", "O63 F7 rs2 rS1 l"}, /* bgeu rs1,rs2,l */
  // there are no FBx,DBx,LDBx as they are machinized into compare and BT

  {MIR_NEG, "r r", "O33 F0 f20 rd0 hs0 rS1"},  /* sub rd,z,rs2 */
  {MIR_NEGS, "r r", "O3b F0 f20 rd0 hs0 rS1"}, /* subw rd,z,rs2 */
  {MIR_FNEG, "r r", "O53 F1 f10 rd0 rs1 rS1"}, /* fsgnjn.s rd,rs1,rs2 */
  {MIR_DNEG, "r r", "O53 F1 f11 rd0 rs1 rS1"}, /* fsgnjn.d rd,rs1,rs2 */
  // ldneg is a builtin

  {MIR_LSH, "r r r", "O33 F1 f0 rd0 rs1 rS2"},  /* sll rd,rs1,rs2 */
  {MIR_LSHS, "r r r", "O3b F1 f0 rd0 rs1 rS2"}, /* sllw rd,rs1,rs2 */
  {MIR_LSH, "r r S", "O13 F1 f0 rd0 rs1 S"},    /* slli rd,rs1,sh */
  {MIR_LSHS, "r r s", "O1b F1 f0 rd0 rs1 s"},   /* slliw rd,rs1,sh */

  {MIR_RSH, "r r r", "O33 F5 f20 rd0 rs1 rS2"},  /* sra rd,rs1,rs2 */
  {MIR_RSHS, "r r r", "O3b F5 f20 rd0 rs1 rS2"}, /* sraw rd,rs1,rs2 */
  {MIR_RSH, "r r S", "O13 F5 f20 rd0 rs1 S"},    /* srai rd,rs1,sh */
  {MIR_RSHS, "r r s", "O1b F5 f20 rd0 rs1 s"},   /* sraiw rd,rs1,sh */

  {MIR_URSH, "r r r", "O33 F5 f0 rd0 rs1 rS2"},  /* srl rd,rs1,rs2 */
  {MIR_URSHS, "r r r", "O3b F5 f0 rd0 rs1 rS2"}, /* srlw rd,rs1,rs2 */
  {MIR_URSH, "r r S", "O13 F5 f0 rd0 rs1 S"},    /* srli rd,rs1,rs2 */
  {MIR_URSHS, "r r s", "O1b F5 f0 rd0 rs1 s"},   /* srliw rd,rs1,sh */

  {MIR_AND, "r r r", "O33 F7 f0 rd0 rs1 rS2"},  /* and rd,rs1,rs2 */
  {MIR_AND, "r r i", "O13 F7 f0 rd0 rs1 i"},    /* andi rd,rs1,i */
  {MIR_ANDS, "r r r", "O33 F7 f0 rd0 rs1 rS2"}, /* and rd,rs1,rs2 */
  {MIR_ANDS, "r r i", "O13 F7 f0 rd0 rs1 i"},   /* andi rd,rs1,i */

  {MIR_OR, "r r r", "O33 F6 f0 rd0 rs1 rS2"},  /* or rd,rs1,rs2 */
  {MIR_OR, "r r i", "O13 F6 f0 rd0 rs1 i"},    /* ori rd,rs1,i */
  {MIR_ORS, "r r r", "O33 F6 f0 rd0 rs1 rS2"}, /* or rd,rs1,rs2 */
  {MIR_ORS, "r r i", "O13 F6 f0 rd0 rs1 i"},   /* ori rd,rs1,i */

  {MIR_XOR, "r r r", "O33 F4 f0 rd0 rs1 rS2"},  /* xor rd,rs1,rs2 */
  {MIR_XOR, "r r i", "O13 F4 f0 rd0 rs1 i"},    /* xori rd,rs1,i */
  {MIR_XORS, "r r r", "O33 F4 f0 rd0 rs1 rS2"}, /* xor rd,rs1,rs2 */
  {MIR_XORS, "r r i", "O13 F4 f0 rd0 rs1 i"},   /* xori rd,rs1,i */

  {MIR_I2F, "r r", "O53 F7 f68 hS2 rd0 rs1"},  /* fcvt.s.l rd,rs1 */
  {MIR_I2D, "r r", "O53 F7 f69 hS2 rd0 rs1"},  /* fcvt.d.l rd,rs1 */
  {MIR_UI2F, "r r", "O53 F7 f68 hS3 rd0 rs1"}, /* fcvt.s.lu rd,rs1 */
  {MIR_UI2D, "r r", "O53 F7 f69 hS3 rd0 rs1"}, /* fcvt.d.lu rd,rs1 */

  {MIR_F2I, "r r", "O53 F1 f60 hS2 rd0 rs1"}, /* fcvt.l.s rd,rs1,rtz */
  {MIR_D2I, "r r", "O53 F1 f61 hS2 rd0 rs1"}, /* fcvt.l.d rd,rs1,rtz */
  {MIR_F2D, "r r", "O53 F0 f21 hS0 rd0 rs1"}, /* fcvt.d.s rd,rs1 -- never round */
  {MIR_D2F, "r r", "O53 F7 f20 hS1 rd0 rs1"}, /* fcvt.s.d rd,rs1 */
  // i2ld, ui2ld, ld2i, f2ld, d2ld, ld2f, ld2d are builtins

  {MIR_CALL, "X r $", "O67 F0 hd1 rs1 i0"},   /* jalr rd,rs1 */
  {MIR_INLINE, "X r $", "O67 F0 hd1 rs1 i0"}, /* jalr rd,rs1 */
#if 0                                         // ?????
  {MIR_CALL, "X L $", ""},             /* bl address */
  {MIR_INLINE, "X L $", ""},           /* bl address */
#endif
  {MIR_RET, "$", "O67 F0 hd0 hs1 i0"}, /* jalr hr0,hr1,0  */

  /* addi r0,r1,15; andi r0,r0,-16; sub sp,sp,r0; mov r0,sp: */
  {MIR_ALLOCA, "r r",
   "O13 F0 rd0 rs1 if; O13 F7 f0 rd0 rs0 i-10;"  /* addi r0,r1,15; andi r0,r0,-16 */
   "O33 F0 f20 hd2 hs2 rS0; O13 F0 rd0 hs2 i0"}, /* sub sp,sp,r0; addi r0,sp,0 */

  /* addi sp,sp,-roundup(imm,16); addi r0,sp,0: */
  {MIR_ALLOCA, "r ju", "O13 F0 hd2 hs2 ju; O13 F0 rd0 hs2 i0"},

  {MIR_BSTART, "r", "O13 F0 rd0 hs2 i0"}, /* r = sp: addi rd,rs1,0 */
  {MIR_BEND, "r", "O13 F0 hd2 rs0 i0"},   /* sp = r: addi rd,rs1,0 */

  /* slli t5,r,3; auipc t6,16; add t6,t6,t5;ld t6,T(t6);jalr zero,t6,0;
     8-byte aligned TableContent.  Remember r can be t5 can be if switch operand is memory. */
  {MIR_SWITCH, "r $",
   "O13 F1 hd1e rs0 S3; O17 hd1f iu0; O33 F0 hd1f hs1f hS1e; O3 F3 hd1f hs1f T; O67 F0 hd0 hs1f "
   "i0"},

};

static void target_get_early_clobbered_hard_regs (MIR_insn_t insn, MIR_reg_t *hr1, MIR_reg_t *hr2) {
  *hr1 = *hr2 = MIR_NON_HARD_REG;
  if (insn->code == MIR_MOD || insn->code == MIR_MODS || insn->code == MIR_UMOD
      || insn->code == MIR_UMODS)
    *hr1 = R8_HARD_REG;
}

static int pattern_index_cmp (const void *a1, const void *a2) {
  int i1 = *(const int *) a1, i2 = *(const int *) a2;
  int c1 = (int) patterns[i1].code, c2 = (int) patterns[i2].code;

  return c1 != c2 ? c1 - c2 : (long) i1 - (long) i2;
}

static void patterns_init (gen_ctx_t gen_ctx) {
  int i, ind, n = sizeof (patterns) / sizeof (struct pattern);
  MIR_insn_code_t prev_code, code;
  insn_pattern_info_t *info_addr;
  insn_pattern_info_t pinfo = {0, 0};

  VARR_CREATE (int, pattern_indexes, 0);
  for (i = 0; i < n; i++) VARR_PUSH (int, pattern_indexes, i);
  qsort (VARR_ADDR (int, pattern_indexes), n, sizeof (int), pattern_index_cmp);
  VARR_CREATE (insn_pattern_info_t, insn_pattern_info, 0);
  for (i = 0; i < MIR_INSN_BOUND; i++) VARR_PUSH (insn_pattern_info_t, insn_pattern_info, pinfo);
  info_addr = VARR_ADDR (insn_pattern_info_t, insn_pattern_info);
  for (prev_code = MIR_INSN_BOUND, i = 0; i < n; i++) {
    ind = VARR_GET (int, pattern_indexes, i);
    if ((code = patterns[ind].code) != prev_code) {
      if (i != 0) info_addr[prev_code].num = i - info_addr[prev_code].start;
      info_addr[code].start = i;
      prev_code = code;
    }
  }
  assert (prev_code != MIR_INSN_BOUND);
  info_addr[prev_code].num = n - info_addr[prev_code].start;
}

static int dec_value (int ch) { return '0' <= ch && ch <= '9' ? ch - '0' : -1; }

static uint64_t read_dec (const char **ptr) {
  int v;
  const char *p;
  uint64_t res = 0;

  for (p = *ptr; (v = dec_value (*p)) >= 0; p++) {
    gen_assert ((res >> 60) == 0);
    res = res * 10 + v;
  }
  gen_assert (p != *ptr);
  *ptr = p - 1;
  return res;
}

static int pattern_match_p (gen_ctx_t gen_ctx, const struct pattern *pat, MIR_insn_t insn) {
  MIR_context_t ctx = gen_ctx->ctx;
  int nop;
  size_t nops = MIR_insn_nops (ctx, insn);
  const char *p;
  char ch, start_ch;
  MIR_op_t op;
  MIR_reg_t hr;

  for (nop = 0, p = pat->pattern; *p != 0; p++, nop++) {
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '$') return TRUE;
    if (MIR_call_code_p (insn->code) && nop >= nops) return FALSE;
    gen_assert (nop < nops);
    op = insn->ops[nop];
    switch (start_ch = *p) {
    case 'X': break;
    case 'r':
      if (op.mode != MIR_OP_HARD_REG) return FALSE;
      break;
#if 0
    case 'h':
      if (op.mode != MIR_OP_HARD_REG) return FALSE;
      ch = *++p;
      gen_assert ('0' <= ch && ch <= '9');
      hr = ch - '0';
      ch = *++p;
      if ('0' <= ch && ch <= '9')
        hr = hr * 10 + ch - '0';
      else
        --p;
      gen_assert (hr <= MAX_HARD_REG);
      if (op.u.hard_reg != hr) return FALSE;
      break;
#endif
    case 'c': {
      uint64_t n;
      p++;
      n = read_dec (&p);
      if ((op.mode != MIR_OP_INT && op.mode != MIR_OP_UINT) || op.u.u != n) return FALSE;
      break;
    }
    case 'm': {
      MIR_type_t type, type2, type3 = MIR_T_BOUND;
      int scale, u_p, s_p;

      if (op.mode != MIR_OP_HARD_REG_MEM) return FALSE;
      u_p = s_p = TRUE;
      ch = *++p;
      switch (ch) {
      case 'f':
        type = MIR_T_F;
        type2 = MIR_T_BOUND;
        scale = 4;
        break;
      case 'd':
        type = MIR_T_D;
        type2 = MIR_T_BOUND;
        scale = 8;
        break;
      case 'l':
        ch = *++p;
        gen_assert (ch == 'd');
        type = MIR_T_LD;
        type2 = MIR_T_BOUND;
        scale = 16;
        break;
      case 'u':
      case 's':
        u_p = ch == 'u';
        s_p = ch == 's';
        ch = *++p;
        /* Fall through: */
      default:
        gen_assert ('0' <= ch && ch <= '3');
        scale = 1 << (ch - '0');
        if (ch == '0') {
          type = u_p ? MIR_T_U8 : MIR_T_I8;
          type2 = u_p && s_p ? MIR_T_I8 : MIR_T_BOUND;
        } else if (ch == '1') {
          type = u_p ? MIR_T_U16 : MIR_T_I16;
          type2 = u_p && s_p ? MIR_T_I16 : MIR_T_BOUND;
        } else if (ch == '2') {
          type = u_p ? MIR_T_U32 : MIR_T_I32;
          type2 = u_p && s_p ? MIR_T_I32 : MIR_T_BOUND;
#if MIR_PTR32
          if (u_p) type3 = MIR_T_P;
#endif
        } else {
          type = u_p ? MIR_T_U64 : MIR_T_I64;
          type2 = u_p && s_p ? MIR_T_I64 : MIR_T_BOUND;
#if MIR_PTR64
          type3 = MIR_T_P;
#endif
        }
      }
      if (op.u.hard_reg_mem.type != type && op.u.hard_reg_mem.type != type2
          && op.u.hard_reg_mem.type != type3)
        return FALSE;
      if (op.u.hard_reg_mem.index != MIR_NON_HARD_REG || op.u.hard_reg_mem.disp < -(1 << 11)
          || op.u.hard_reg_mem.disp >= (1 << 11)
          || (type == MIR_T_LD && op.u.hard_reg_mem.disp + 8 >= (1 << 11)))
        return FALSE;
      break;
    }
    case 'i': {
      ch = *++p;
      if (op.mode != MIR_OP_INT && op.mode != MIR_OP_UINT && (ch != 'a' || op.mode != MIR_OP_REF))
        return FALSE;
      if ((ch == 'u' || ch == 'a') && (op.mode == MIR_OP_INT || op.mode == MIR_OP_UINT)) {
        assert (nop == 1);
        if (op.u.i < -(1l << 31) || op.u.i >= (1l << 31)) return FALSE;
        if (ch == 'u' && (op.u.i & 0xfff) != 0) return FALSE;
      } else if (ch == 'a' && op.mode == MIR_OP_REF) {
        int64_t v;

        if (op.u.ref->item_type == MIR_data_item && op.u.ref->u.data->name != NULL
            && _MIR_reserved_ref_name_p (ctx, op.u.ref->u.data->name)) {
          v = (int64_t) op.u.ref->u.data->u.els;
        } else {
          v = (int64_t) op.u.ref->addr;
        }
        if (v < -(1l << 31) || v >= (1l << 31)) return FALSE;
      } else {
        assert (nop == 1 || nop == 2);
        p--;
        if (op.u.i < -(1 << 11) || op.u.i >= (1 << 11)) return FALSE;
      }
      break;
    }
    case 'j':
      if (op.mode != MIR_OP_INT && op.mode != MIR_OP_UINT) return FALSE;
      int64_t i = op.u.i;
      ch = *++p;
      if (ch == 'u') {
        assert (nop == 1);
        i = (i + 15) / 16 * 16;
      } else {
        p--;
        assert (nop == 2);
      }
      if (i <= -(1 << 11) || i >= (1 << 11)) return FALSE;
      break;
    case 'I': {
      if (op.mode != MIR_OP_INT && op.mode != MIR_OP_UINT && op.mode != MIR_OP_REF) return FALSE;
      break;
    }
    case 's':
    case 'S': {
      assert (nop == 2);
      if (op.mode != MIR_OP_INT && op.mode != MIR_OP_UINT) return FALSE;
      if (op.u.i < 0 || (start_ch == 's' && op.u.i > 31) || (start_ch == 'S' && op.u.i > 63))
        return FALSE;
      break;
    }
    case 'l':
    case 'L':
      if (op.mode != MIR_OP_LABEL) return FALSE;
      break;
    default: gen_assert (FALSE);
    }
  }
  gen_assert (nop == nops);
  return TRUE;
}

static const char *find_insn_pattern_replacement (gen_ctx_t gen_ctx, MIR_insn_t insn) {
  int i;
  const struct pattern *pat;
  insn_pattern_info_t info = VARR_GET (insn_pattern_info_t, insn_pattern_info, insn->code);

  for (i = 0; i < info.num; i++) {
    pat = &patterns[VARR_GET (int, pattern_indexes, info.start + i)];
    if (pattern_match_p (gen_ctx, pat, insn)) return pat->replacement;
  }
  return NULL;
}

static void patterns_finish (gen_ctx_t gen_ctx) {
  VARR_DESTROY (int, pattern_indexes);
  VARR_DESTROY (insn_pattern_info_t, insn_pattern_info);
}

static int hex_value (int ch) {
  return ('0' <= ch && ch <= '9'   ? ch - '0'
          : 'A' <= ch && ch <= 'F' ? ch - 'A' + 10
          : 'a' <= ch && ch <= 'f' ? ch - 'a' + 10
                                   : -1);
}

static uint64_t read_hex (const char **ptr) {
  int v;
  const char *p;
  uint64_t res = 0;

  for (p = *ptr; (v = hex_value (*p)) >= 0; p++) {
    gen_assert ((res >> 60) == 0);
    res = res * 16 + v;
  }
  gen_assert (p != *ptr);
  *ptr = p - 1;
  return res;
}

static void put_byte (struct gen_ctx *gen_ctx, int byte) { VARR_PUSH (uint8_t, result_code, byte); }

static void put_uint64 (struct gen_ctx *gen_ctx, uint64_t v, int nb) { /* Little endian */
  for (; nb > 0; nb--) {
    put_byte (gen_ctx, v & 0xff);
    v >>= 8;
  }
}

static void set_int64 (uint8_t *addr, int64_t v, int nb) { /* Little endian */
  for (; nb > 0; nb--) {
    *addr++ = v & 0xff;
    v >>= 8;
  }
}

static int64_t get_int64 (uint8_t *addr, int nb) { /* Little endian */
  int64_t v = 0;
  int i, sh = (8 - nb) * 8;

  for (i = nb - 1; i >= 0; i--) v = (v << 8) | addr[i];
  if (sh > 0) v = (v << sh) >> sh; /* make it signed */
  return v;
}

static uint32_t check_and_set_mask (uint32_t opcode_mask, uint32_t mask) {
  gen_assert ((opcode_mask & mask) == 0);
  return opcode_mask | mask;
}

static void out_insn (gen_ctx_t gen_ctx, MIR_insn_t insn, const char *replacement) {
  MIR_context_t ctx = gen_ctx->ctx;
  size_t offset;
  const char *p, *insn_str;
  label_ref_t lr;
  const_ref_t cr;
  int switch_table_addr_p = FALSE;
  size_t nops = MIR_insn_nops (ctx, insn);

  if (insn->code == MIR_ALLOCA
      && (insn->ops[1].mode == MIR_OP_INT || insn->ops[1].mode == MIR_OP_UINT))
    insn->ops[1].u.u = (insn->ops[1].u.u + 15) & -16;
  for (insn_str = replacement;; insn_str = p + 1) {
    char ch, ch2, start_ch, d;
    uint32_t insn32 = 0, insn_mask = 0, el_mask;
    int opcode = -1, funct3 = -1, funct7 = -1, rd = -1, rs1 = -1, rs2 = -1;
    int shamt = -1, imm12, imm20, st_disp;
    int imm12_p = FALSE, imm20_p = FALSE, st_disp_p = FALSE;
    MIR_op_t op;
    int label_ref_num = -1;

    for (p = insn_str; (ch = *p) != '\0' && ch != ';'; p++) {
      if ((ch = *p) == 0 || ch == ';') break;
      el_mask = 0;
      switch ((start_ch = ch = *p)) {
      case ' ':
      case '\t': break;
      case 'O':
        p++;
        gen_assert (hex_value (*p) >= 0 && opcode < 0);
        opcode = read_hex (&p);
        assert (opcode < (1 << 7));
        el_mask = 0x3f;
        break;
      case 'F':
        p++;
        gen_assert (hex_value (*p) >= 0 && funct3 < 0);
        funct3 = read_hex (&p);
        assert (funct3 < (1 << 3));
        el_mask = 0xf000;
        break;
      case 'f':
        p++;
        gen_assert (hex_value (*p) >= 0 && funct7 < 0);
        funct7 = read_hex (&p);
        assert (funct7 < (1 << 7));
        el_mask = 0xfe000000;
        break;
      case 'g':
        p++;
        gen_assert (hex_value (*p) >= 0 && funct7 < 0);
        funct7 = read_hex (&p);
        assert (funct7 < (1 << 6));
        el_mask = 0xfc000000;
        break;
      case 'r':
      case 'h': {
        int reg;
        ch2 = *++p;
        gen_assert (ch2 == 'd' || ch2 == 's' || ch2 == 'S');
        ch = *++p;
        if (start_ch == 'h') {
          reg = read_hex (&p);
        } else {
          gen_assert ('0' <= ch && ch <= '2' && ch - '0' < nops);
          op = insn->ops[ch - '0'];
          gen_assert (op.mode == MIR_OP_HARD_REG);
          reg = op.u.hard_reg;
        }
        if (reg >= F0_HARD_REG) reg -= F0_HARD_REG;
        gen_assert (reg <= 31);
        if (ch2 == 'd') {
          rd = reg;
          el_mask = 0xf80;
        } else if (ch2 == 's') {
          rs1 = reg;
          el_mask = 0xf8000;
        } else {
          rs2 = reg;
          el_mask = 0x1f00000;
        }
        break;
      }
      case 'm':
        ch = *++p;
        if (ch == 's') { /* store */
          gen_assert (insn->ops[0].mode == MIR_OP_HARD_REG_MEM);
          op = insn->ops[0];
          st_disp = ((op.u.hard_reg_mem.disp << 13) & 0x01fc0000) | (op.u.hard_reg_mem.disp & 0x1f);
          el_mask = 0xfe000f80;
          st_disp_p = TRUE;
        } else { /* load */
          gen_assert (ch == 'l' && insn->ops[1].mode == MIR_OP_HARD_REG_MEM);
          op = insn->ops[1];
          imm12 = op.u.hard_reg_mem.disp;
          imm12_p = TRUE;
          el_mask = 0xfff00000;
        }
        el_mask |= 0xf8000;
        rs1 = op.u.hard_reg_mem.base;
        break;
      case 's':
      case 'S':
        el_mask = (start_ch == 's' ? 0x1f00000 : 0x3f00000);
        ch = *++p;
        if (hex_value (ch) >= 0) {
          shamt = read_hex (&p);
        } else {
          p--;
          op = insn->ops[2];
          gen_assert (op.mode == MIR_OP_INT || op.mode == MIR_OP_UINT);
          shamt = op.u.i;
          gen_assert (shamt >= 0);
        }
        break;
      case 'i':
        ch = *++p;
        if (ch == '-' || hex_value (ch) >= 0) { /* i[-]<hex> */
          int neg_p = FALSE;
          if (ch == '-') {
            ch = *++p;
            neg_p = TRUE;
          }
          gen_assert (hex_value (ch) >= 0);
          imm12 = read_hex (&p);
          if (neg_p) imm12 = -imm12;
          el_mask = 0xfff00000;
          imm12_p = TRUE;
        } else if (ch == 'h' || ch == 'l') {
          int32_t v;
          op = insn->ops[1];
          if (op.mode != MIR_OP_REF) {
            v = (int32_t) op.u.i;
          } else if (op.u.ref->item_type == MIR_data_item && op.u.ref->u.data->name != NULL
                     && _MIR_reserved_ref_name_p (ctx, op.u.ref->u.data->name)) {
            v = (int32_t) (int64_t) op.u.ref->u.data->u.els;
          } else {
            v = (int32_t) (int64_t) op.u.ref->addr;
          }
          imm12 = (v << 20) >> 20;
          el_mask = 0xfff00000;
          imm12_p = TRUE;
          if (ch == 'h') {
            imm20 = (v - imm12) >> 12;
            el_mask = 0xfffff000;
            imm12_p = FALSE;
            imm20_p = TRUE;
          }
        } else if (ch == 'u') {
          ch = *++p;
          if (hex_value (ch) >= 0) { /* iu<hex> */
            imm20 = read_hex (&p);
            el_mask = 0xfffff000;
            imm20_p = TRUE;
          } else { /* iu */
            p--;
            op = insn->ops[1];
            gen_assert (op.mode == MIR_OP_INT || op.mode == MIR_OP_UINT);
            gen_assert ((op.u.i & 0xfff) == 0);
            imm20 = op.u.i >> 12;
            el_mask = 0xfffff000;
            imm20_p = TRUE;
          }
        } else { /* i */
          p--;
          imm12 = (nops > 2 && (insn->ops[2].mode == MIR_OP_INT || insn->ops[2].mode == MIR_OP_UINT)
                     ? insn->ops[2].u.i
                     : insn->ops[1].u.i);
          imm12_p = TRUE;
        }
        break;
      case 'j':
        ch = *++p;
        if (ch == 'u') { /* ju */
          op = insn->ops[1];
          gen_assert (op.mode == MIR_OP_INT || op.mode == MIR_OP_UINT);
          imm12 = -(op.u.i + 15) / 16 * 16;
          el_mask = 0xfff00000;
        } else { /* j */
          p--;
          op = insn->ops[2];
          gen_assert (op.mode == MIR_OP_INT || op.mode == MIR_OP_UINT);
          imm12 = -op.u.i;
          el_mask = 0xfff00000;
        }
        imm12_p = TRUE;
        break;
      case 'I': {
        op = insn->ops[1];
        gen_assert (op.mode == MIR_OP_INT || op.mode == MIR_OP_UINT || op.mode == MIR_OP_REF);
        if (op.mode != MIR_OP_REF) {
          cr.val = op.u.u;
        } else if (op.u.ref->item_type == MIR_data_item && op.u.ref->u.data->name != NULL
                   && _MIR_reserved_ref_name_p (ctx, op.u.ref->u.data->name)) {
          cr.val = (uint64_t) op.u.ref->u.data->u.els;
        } else {
          cr.val = (uint64_t) op.u.ref->addr;
        }
        cr.const_addr_disp = VARR_LENGTH (uint8_t, result_code);
        VARR_PUSH (const_ref_t, const_refs, cr);
        break;
      }
      case 'T': {
        gen_assert (!switch_table_addr_p);
        imm12 = VARR_LENGTH (uint8_t, result_code) % 8;
        if (imm12 != 0) imm12 = 8 - imm12;
        imm12 += 16;
        el_mask = 0xfff00000;
        imm12_p = TRUE;
        switch_table_addr_p = TRUE;
        break;
      }
      case 'l':
      case 'L': {
        op = insn->ops[start_ch == 'l' || (insn->code != MIR_CALL && insn->code != MIR_INLINE) ? 0
                                                                                               : 1];
        gen_assert (op.mode == MIR_OP_LABEL || op.mode == MIR_OP_REF);
        lr.abs_addr_p = FALSE;
        lr.short_p = start_ch == 'l';
        lr.label_val_disp = 0;
        lr.label = op.u.label;
        label_ref_num = VARR_LENGTH (label_ref_t, label_refs);
        VARR_PUSH (label_ref_t, label_refs, lr);
        el_mask = start_ch == 'l' ? 0xfe000f80 : 0xfffff000;
        break;
      }
      default: gen_assert (FALSE);
      }
    }
    if (opcode >= 0) insn32 |= opcode;
    if (funct3 >= 0) insn32 |= (funct3 << 12);
    if (funct7 >= 0) insn32 |= (funct7 << 25);
    if (rd >= 0) {
      gen_assert (rd <= 31);
      insn32 |= rd << 7;
    }
    if (rs1 >= 0) {
      gen_assert (rs1 <= 31);
      insn32 |= rs1 << 15;
    }
    if (rs2 >= 0) {
      gen_assert (rs2 <= 31);
      insn32 |= rs2 << 20;
    }
    if (shamt >= 0) insn32 |= shamt << 20;
    if (imm12_p) insn32 |= imm12 << 20;
    if (imm20_p) insn32 |= imm20 << 12;
    if (st_disp_p) insn32 |= st_disp << 7;
    insn_mask = check_and_set_mask (insn_mask, el_mask);
    if (label_ref_num >= 0) VARR_ADDR (label_ref_t, label_refs)
    [label_ref_num].label_val_disp = VARR_LENGTH (uint8_t, result_code);

    put_uint64 (gen_ctx, insn32, 4); /* output the machine insn */

    if (*p == 0) break;
  }
  if (!switch_table_addr_p) return;
  gen_assert (insn->code == MIR_SWITCH);
  if (VARR_LENGTH (uint8_t, result_code) % 8 != 0)
    put_uint64 (gen_ctx, 0, 8 - VARR_LENGTH (uint8_t, result_code) % 8);
  for (size_t i = 1; i < insn->nops; i++) {
    gen_assert (insn->ops[i].mode == MIR_OP_LABEL);
    lr.abs_addr_p = TRUE;
    lr.label_val_disp = VARR_LENGTH (uint8_t, result_code);
    lr.label = insn->ops[i].u.label;
    VARR_PUSH (label_ref_t, label_refs, lr);
    put_uint64 (gen_ctx, 0, 8);
  }
}

static int target_insn_ok_p (gen_ctx_t gen_ctx, MIR_insn_t insn) {
  return find_insn_pattern_replacement (gen_ctx, insn) != NULL;
}

static uint32_t get_b_format_imm (int offset) {
  int d = offset >> 1; /* scale */
  gen_assert (-(1 << 11) <= d && d < (1 << 11));
  return ((((d >> 5) & 0x40) | ((d >> 4) & 0x3f)) << 25)
         | ((((d & 0xf) << 1) | ((d >> 10) & 0x1)) << 7);
}

static uint32_t get_j_format_imm (int offset) {
  int d = offset >> 1; /* scale */
  gen_assert (-(1 << 19) <= d && d < (1 << 19));
  return ((d & 0x80000) | ((d & 0x3ff) << 9) | (((d >> 10) & 0x1) << 8) | ((d >> 11) & 0xff)) << 12;
}

static uint8_t *target_translate (gen_ctx_t gen_ctx, size_t *len) {
  MIR_context_t ctx = gen_ctx->ctx;
  size_t i;
  MIR_insn_t insn, next_insn;
  const char *replacement;

  gen_assert (curr_func_item->item_type == MIR_func_item);
  VARR_TRUNC (uint8_t, result_code, 0);
  VARR_TRUNC (label_ref_t, label_refs, 0);
  VARR_TRUNC (const_ref_t, const_refs, 0);
  VARR_TRUNC (uint64_t, abs_address_locs, 0);
  for (insn = DLIST_HEAD (MIR_insn_t, curr_func_item->u.func->insns); insn != NULL;
       insn = next_insn) {
    next_insn = DLIST_NEXT (MIR_insn_t, insn);
    if (insn->code == MIR_LDMOV) { /* split ld move: */
      MIR_op_t op;

      if (insn->ops[0].mode == MIR_OP_HARD_REG) {
        gen_assert (insn->ops[0].u.hard_reg + 1 < F0_HARD_REG
                    && insn->ops[1].mode == MIR_OP_HARD_REG_MEM);
        op = insn->ops[1];
        op.u.hard_reg_mem.type = MIR_T_I64;
        next_insn = gen_mov (gen_ctx, insn, MIR_MOV, insn->ops[0], op);
        op.u.hard_reg_mem.disp += 8;
        gen_mov (gen_ctx, insn, MIR_MOV, _MIR_new_hard_reg_op (ctx, insn->ops[0].u.hard_reg + 1),
                 op);
        gen_delete_insn (gen_ctx, insn);
      } else if (insn->ops[1].mode == MIR_OP_HARD_REG) {
        gen_assert (insn->ops[1].u.hard_reg + 1 < F0_HARD_REG
                    && insn->ops[0].mode == MIR_OP_HARD_REG_MEM);
        op = insn->ops[0];
        op.u.hard_reg_mem.type = MIR_T_I64;
        next_insn = gen_mov (gen_ctx, insn, MIR_MOV, op, insn->ops[1]);
        op.u.hard_reg_mem.disp += 8;
        gen_mov (gen_ctx, insn, MIR_MOV, op,
                 _MIR_new_hard_reg_op (ctx, insn->ops[1].u.hard_reg + 1));
        gen_delete_insn (gen_ctx, insn);
      } else {
        gen_assert (insn->ops[0].mode == MIR_OP_HARD_REG_MEM
                    && insn->ops[1].mode == MIR_OP_HARD_REG_MEM);
        op = insn->ops[1];
        op.u.hard_reg_mem.type = MIR_T_D;
        next_insn = gen_mov (gen_ctx, insn, MIR_DMOV,
                             _MIR_new_hard_reg_op (ctx, TEMP_DOUBLE_HARD_REG1), op);
        op.u.hard_reg_mem.disp += 8;
        gen_mov (gen_ctx, insn, MIR_DMOV, _MIR_new_hard_reg_op (ctx, TEMP_DOUBLE_HARD_REG2), op);
        op = insn->ops[0];
        op.u.hard_reg_mem.type = MIR_T_D;
        gen_mov (gen_ctx, insn, MIR_DMOV, op, _MIR_new_hard_reg_op (ctx, TEMP_DOUBLE_HARD_REG1));
        op.u.hard_reg_mem.disp += 8;
        gen_mov (gen_ctx, insn, MIR_DMOV, op, _MIR_new_hard_reg_op (ctx, TEMP_DOUBLE_HARD_REG2));
        gen_delete_insn (gen_ctx, insn);
      }
    } else if (insn->code == MIR_LABEL) {
      set_label_disp (gen_ctx, insn, VARR_LENGTH (uint8_t, result_code));
    } else {
      replacement = find_insn_pattern_replacement (gen_ctx, insn);
      if (replacement == NULL) {
        fprintf (stderr, "fatal failure in matching insn:");
        MIR_output_insn (ctx, stderr, insn, curr_func_item->u.func, TRUE);
        exit (1);
      } else {
        gen_assert (replacement != NULL);
        out_insn (gen_ctx, insn, replacement);
      }
    }
  }
  /* Setting up labels */
  for (i = 0; i < VARR_LENGTH (label_ref_t, label_refs); i++) {
    label_ref_t lr = VARR_GET (label_ref_t, label_refs, i);

    if (!lr.abs_addr_p) {
      int64_t offset = (int64_t) get_label_disp (gen_ctx, lr.label) - (int64_t) lr.label_val_disp;
      uint32_t bin_insn;
      gen_assert ((offset & 0x1) == 0);
      if (lr.short_p && (offset < -(1 << 12) || offset > (1 << 12))) {
        /* BL:br L => BL:jmp NBL; ... NBL: br TL;jmp BL+4;TL:jmp L: */
        bin_insn = *(uint32_t *) (VARR_ADDR (uint8_t, result_code) + lr.label_val_disp);
        offset = (int64_t) VARR_LENGTH (uint8_t, result_code) - (int64_t) lr.label_val_disp;
        *(uint32_t *) (VARR_ADDR (uint8_t, result_code) + lr.label_val_disp)
          = 0x6f | get_j_format_imm (offset);
        bin_insn |= get_b_format_imm (8);
        put_uint64 (gen_ctx, bin_insn, 4);
        offset = (int64_t) lr.label_val_disp - (int64_t) VARR_LENGTH (uint8_t, result_code) + 4;
        bin_insn = 0x6f | get_j_format_imm (offset);
        put_uint64 (gen_ctx, bin_insn, 4);
        offset = (int64_t) get_label_disp (gen_ctx, lr.label)
                 - (int64_t) VARR_LENGTH (uint8_t, result_code);
        bin_insn = 0x6f | get_j_format_imm (offset);
        put_uint64 (gen_ctx, bin_insn, 4);

      } else {
        *(uint32_t *) (VARR_ADDR (uint8_t, result_code) + lr.label_val_disp)
          |= (lr.short_p ? get_b_format_imm (offset) : get_j_format_imm (offset));
      }
    } else {
      set_int64 (&VARR_ADDR (uint8_t, result_code)[lr.label_val_disp],
                 (int64_t) get_label_disp (gen_ctx, lr.label), 8);
      VARR_PUSH (uint64_t, abs_address_locs, lr.label_val_disp);
    }
  }
  while (VARR_LENGTH (uint8_t, result_code) % 8 != 0) /* Align the pool */
    VARR_PUSH (uint8_t, result_code, 0);
  /* Setting up 64-bit const addresses */
  for (i = 0; i < VARR_LENGTH (const_ref_t, const_refs); i++) {
    const_ref_t cr = VARR_GET (const_ref_t, const_refs, i);
    uint32_t disp, carry;
    gen_assert (VARR_LENGTH (uint8_t, result_code) > cr.const_addr_disp
                && VARR_LENGTH (uint8_t, result_code) - cr.const_addr_disp < (1l << 31));
    disp = (uint32_t) (VARR_LENGTH (uint8_t, result_code) - cr.const_addr_disp);
    carry = (disp & 0x800) << 1;
    *(uint32_t *) (&VARR_ADDR (uint8_t, result_code)[cr.const_addr_disp])
      |= (disp + carry) & 0xfffff000;
    *(uint32_t *) (&VARR_ADDR (uint8_t, result_code)[cr.const_addr_disp + 4]) |= disp << 20;
    put_uint64 (gen_ctx, cr.val, 8);
  }
  while (VARR_LENGTH (uint8_t, result_code) % 16 != 0) /* Align the pool */
    VARR_PUSH (uint8_t, result_code, 0);
  *len = VARR_LENGTH (uint8_t, result_code);
  return VARR_ADDR (uint8_t, result_code);
}

static void target_rebase (gen_ctx_t gen_ctx, uint8_t *base) {
  MIR_code_reloc_t reloc;

  VARR_TRUNC (MIR_code_reloc_t, relocs, 0);
  for (size_t i = 0; i < VARR_LENGTH (uint64_t, abs_address_locs); i++) {
    reloc.offset = VARR_GET (uint64_t, abs_address_locs, i);
    reloc.value = base + get_int64 (base + reloc.offset, 8);
    VARR_PUSH (MIR_code_reloc_t, relocs, reloc);
  }
  _MIR_update_code_arr (gen_ctx->ctx, base, VARR_LENGTH (MIR_code_reloc_t, relocs),
                        VARR_ADDR (MIR_code_reloc_t, relocs));
}

static void target_init (gen_ctx_t gen_ctx) {
  gen_ctx->target_ctx = gen_malloc (gen_ctx, sizeof (struct target_ctx));
  VARR_CREATE (uint8_t, result_code, 0);
  VARR_CREATE (label_ref_t, label_refs, 0);
  VARR_CREATE (const_ref_t, const_refs, 0);
  VARR_CREATE (uint64_t, abs_address_locs, 0);
  VARR_CREATE (MIR_code_reloc_t, relocs, 0);
  MIR_type_t res = MIR_T_I64;
  MIR_var_t args1[] = {{MIR_T_F, "src"}}, args2[] = {{MIR_T_D, "src"}};
  _MIR_register_unspec_insn (gen_ctx->ctx, FMVXW_CODE, "fmv.x.w", 1, &res, 1, FALSE, args1);
  _MIR_register_unspec_insn (gen_ctx->ctx, FMVXD_CODE, "fmv.x.d", 1, &res, 1, FALSE, args2);
  patterns_init (gen_ctx);
}

static void target_finish (gen_ctx_t gen_ctx) {
  patterns_finish (gen_ctx);
  VARR_DESTROY (uint8_t, result_code);
  VARR_DESTROY (label_ref_t, label_refs);
  VARR_DESTROY (const_ref_t, const_refs);
  VARR_DESTROY (uint64_t, abs_address_locs);
  VARR_DESTROY (MIR_code_reloc_t, relocs);
  free (gen_ctx->target_ctx);
  gen_ctx->target_ctx = NULL;
}