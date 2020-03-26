// Copyright (c) 2019 Christoffer Lerno. All rights reserved.
// Use of this source code is governed by the GNU LGPLv3.0 license
// a copy of which can be found in the LICENSE file.

#include "llvm_codegen_internal.h"
#include "compiler_internal.h"

static inline LLVMValueRef gencontext_emit_add_int(GenContext *context, Type *type, bool use_mod, LLVMValueRef left, LLVMValueRef right)
{
	if (use_mod)
	{
		return LLVMBuildAdd(context->builder, left, right, "add_mod");
	}

	// TODO insert trap
	return type_is_unsigned_integer(type)
		? LLVMBuildNUWAdd(context->builder, left, right, "uadd")
		: LLVMBuildNSWAdd(context->builder, left, right, "add");
}

static inline LLVMValueRef gencontext_emit_sub_int(GenContext *context, Type *type, bool use_mod, LLVMValueRef left, LLVMValueRef right)
{
	if (use_mod)
	{
		return LLVMBuildSub(context->builder, left, right, "sub_mod");
	}

	// TODO insert trap
	return type_is_unsigned_integer(type)
	       ? LLVMBuildNUWSub(context->builder, left, right, "usub")
	       : LLVMBuildNSWSub(context->builder, left, right, "sub");
}

static inline LLVMValueRef gencontext_emit_subscript_addr(GenContext *context, Expr *expr)
{
	LLVMValueRef index = gencontext_emit_expr(context, expr->subscript_expr.index);
	Type *type = expr->subscript_expr.expr->type->canonical;
	switch (type->type_kind)
	{
		case TYPE_ARRAY:
			TODO
		case TYPE_POINTER:
			return LLVMBuildGEP2(context->builder,
			                     BACKEND_TYPE(type->pointer),
			                     gencontext_emit_expr(context, expr->subscript_expr.expr),
			                     &index, 1, "[]");
		case TYPE_VARARRAY:
		case TYPE_SUBARRAY:
		case TYPE_STRING:
			TODO
		default:
			UNREACHABLE
	}
}

static inline LLVMValueRef gencontext_emit_access_addr(GenContext *context, Expr *expr)
{
	LLVMValueRef value = gencontext_emit_address(context, expr->access_expr.parent);
	return LLVMBuildStructGEP2(context->builder, BACKEND_TYPE(expr->access_expr.parent->type), value, (unsigned)expr->access_expr.index, "");
}

LLVMValueRef gencontext_emit_scoped_expr(GenContext *context, Expr *expr)
{
	LLVMValueRef value = gencontext_emit_expr(context, expr->expr_scope.expr);
	gencontext_emit_defer(context, expr->expr_scope.defers.start, expr->expr_scope.defers.end);
	return value;
}

LLVMValueRef gencontext_emit_scoped_expr_address(GenContext *context, Expr *expr)
{
	LLVMValueRef value = gencontext_emit_address(context, expr->expr_scope.expr);
	gencontext_emit_defer(context, expr->expr_scope.defers.start, expr->expr_scope.defers.end);
	return value;
}

LLVMValueRef gencontext_emit_address(GenContext *context, Expr *expr)
{
	switch (expr->expr_kind)
	{
		case EXPR_IDENTIFIER:
			return expr->identifier_expr.decl->var.backend_ref;
		case EXPR_UNARY:
			assert(expr->unary_expr.operator == UNARYOP_DEREF);
			return gencontext_emit_expr(context, expr->unary_expr.expr);
		case EXPR_ACCESS:
			return gencontext_emit_access_addr(context, expr);
		case EXPR_SUBSCRIPT:
			return gencontext_emit_subscript_addr(context, expr);
		case EXPR_SCOPED_EXPR:
			return gencontext_emit_scoped_expr_address(context, expr);
		case EXPR_CONST:
		case EXPR_TYPE:
		case EXPR_POISONED:
		case EXPR_TRY:
		case EXPR_SIZEOF:
		case EXPR_BINARY:
		case EXPR_TERNARY:
		case EXPR_POST_UNARY:
		case EXPR_TYPE_ACCESS:
		case EXPR_CALL:
		case EXPR_STRUCT_VALUE:
		case EXPR_STRUCT_INIT_VALUES:
		case EXPR_INITIALIZER_LIST:
		case EXPR_EXPRESSION_LIST:
		case EXPR_CAST:
			UNREACHABLE
	}
	UNREACHABLE
}

LLVMValueRef gencontext_emit_cast(GenContext *context, CastKind cast_kind, LLVMValueRef value, Type *type, Type *target_type)
{
	switch (cast_kind)
	{
		case CAST_ERROR:
			UNREACHABLE
		case CAST_PTRPTR:
			return LLVMBuildPointerCast(context->builder, value, BACKEND_TYPE(type), "ptrptr");
		case CAST_PTRXI:
			return LLVMBuildPtrToInt(context->builder, value, BACKEND_TYPE(type), "ptrxi");
		case CAST_VARRPTR:
			TODO
		case CAST_ARRPTR:
			TODO
		case CAST_STRPTR:
			TODO
		case CAST_PTRBOOL:
			return LLVMBuildICmp(context->builder, LLVMIntNE, value, LLVMConstPointerNull(BACKEND_TYPE(type->canonical->pointer)), "ptrbool");
		case CAST_BOOLINT:
			return LLVMBuildTrunc(context->builder, value, BACKEND_TYPE(type), "boolsi");
		case CAST_FPBOOL:
			return LLVMBuildFCmp(context->builder, LLVMRealUNE, value, LLVMConstNull(LLVMTypeOf(value)), "fpbool");
		case CAST_BOOLFP:
			return LLVMBuildSIToFP(context->builder, value, BACKEND_TYPE(type), "boolfp");
		case CAST_INTBOOL:
			return LLVMBuildICmp(context->builder, LLVMIntNE, value, LLVMConstNull(LLVMTypeOf(value)), "intbool");
		case CAST_FPFP:
			return type_convert_will_trunc(type, target_type)
			       ? LLVMBuildFPTrunc(context->builder, value, BACKEND_TYPE(type), "fpfptrunc")
			       : LLVMBuildFPExt(context->builder, value, BACKEND_TYPE(type), "fpfpext");
		case CAST_FPSI:
			return LLVMBuildFPToSI(context->builder, value, BACKEND_TYPE(type), "fpsi");
		case CAST_FPUI:
			return LLVMBuildFPToUI(context->builder, value, BACKEND_TYPE(type), "fpui");
		case CAST_SISI:
			return type_convert_will_trunc(type, target_type)
			       ? LLVMBuildTrunc(context->builder, value, BACKEND_TYPE(type), "sisitrunc")
			       : LLVMBuildSExt(context->builder, value, BACKEND_TYPE(type), "sisiext");
		case CAST_SIUI:
			return type_convert_will_trunc(type, target_type)
			       ? LLVMBuildTrunc(context->builder, value, BACKEND_TYPE(type), "siuitrunc")
			       : LLVMBuildZExt(context->builder, value, BACKEND_TYPE(type), "siuiext");
		case CAST_SIFP:
			return LLVMBuildSIToFP(context->builder, value, BACKEND_TYPE(type), "sifp");
		case CAST_XIPTR:
			return LLVMBuildIntToPtr(context->builder, value, BACKEND_TYPE(type), "xiptr");
		case CAST_UISI:
			return type_convert_will_trunc(type, target_type)
			       ? LLVMBuildTrunc(context->builder, value, BACKEND_TYPE(type), "uisitrunc")
			       : LLVMBuildZExt(context->builder, value, BACKEND_TYPE(type), "uisiext");
		case CAST_UIUI:
			return type_convert_will_trunc(type, target_type)
			       ? LLVMBuildTrunc(context->builder, value, BACKEND_TYPE(type), "uiuitrunc")
			       : LLVMBuildZExt(context->builder, value, BACKEND_TYPE(type), "uiuiext");
		case CAST_UIFP:
			return LLVMBuildUIToFP(context->builder, value, BACKEND_TYPE(type), "uifp");
		case CAST_ENUMSI:
			TODO
	}
}
static inline LLVMValueRef gencontext_emit_cast_expr(GenContext *context, Expr *expr)
{
	LLVMValueRef rhs = gencontext_emit_expr(context, expr->cast_expr.expr);
	return gencontext_emit_cast(context, expr->cast_expr.kind, rhs, expr->type->canonical, expr->cast_expr.expr->type->canonical);
}

static inline LLVMValueRef gencontext_emit_inc_dec_change(GenContext *context, bool use_mod, LLVMValueRef current_value, Expr *expr, int diff)
{
	Type *type = expr->type->canonical;
	LLVMTypeRef llvm_type = BACKEND_TYPE(type);

	if (type->type_kind == TYPE_POINTER)
	{
		LLVMValueRef add = LLVMConstInt(diff < 0 ? BACKEND_TYPE(type_isize) : BACKEND_TYPE(type_usize), diff, diff < 0);
		return LLVMBuildGEP2(context->builder, llvm_type, current_value, &add, 1, "ptrincdec");
	}

	if (type_is_float(type))
	{
		LLVMValueRef add = LLVMConstReal(llvm_type, (double)diff);
		return LLVMBuildFAdd(context->builder, current_value, add, "fincdec");
	}

	LLVMValueRef diff_value = LLVMConstInt(llvm_type, 1, false);
	return diff > 0
		? gencontext_emit_add_int(context, type, use_mod, current_value, diff_value)
		: gencontext_emit_sub_int(context, type, use_mod, current_value, diff_value);
}

static inline LLVMValueRef gencontext_emit_pre_inc_dec(GenContext *context, Expr *expr, int diff, bool use_mod)
{
	LLVMValueRef addr = gencontext_emit_address(context, expr);
	LLVMValueRef value = LLVMBuildLoad2(context->builder, BACKEND_TYPE(expr->type), addr, "");
	LLVMValueRef result = gencontext_emit_inc_dec_change(context, use_mod, value, expr, diff);
	LLVMBuildStore(context->builder, result, addr);
	return result;
}

static inline LLVMValueRef gencontext_emit_post_inc_dec(GenContext *context, Expr *expr, int diff, bool use_mod)
{
	LLVMValueRef addr = gencontext_emit_address(context, expr);
	LLVMValueRef value = LLVMBuildLoad2(context->builder, BACKEND_TYPE(expr->type), addr, "");
	LLVMValueRef result = gencontext_emit_inc_dec_change(context, use_mod, value, expr, diff);
	LLVMBuildStore(context->builder, result, addr);
	return value;
}

LLVMValueRef gencontext_emit_unary_expr(GenContext *context, Expr *expr)
{
	switch (expr->unary_expr.operator)
	{
		case UNARYOP_ERROR:
			FATAL_ERROR("Illegal unary op %s", expr->unary_expr.operator);
		case UNARYOP_NOT:
			return LLVMBuildXor(context->builder, gencontext_emit_expr(context, expr->unary_expr.expr), LLVMConstInt(type_bool->backend_type, 1, 0), "not");
		case UNARYOP_BITNEG:
			return LLVMBuildNot(context->builder, gencontext_emit_expr(context, expr->unary_expr.expr), "bnot");
		case UNARYOP_NEG:
			if (type_is_float(expr->unary_expr.expr->type->canonical))
			{
				return LLVMBuildFNeg(context->builder, gencontext_emit_expr(context, expr->unary_expr.expr), "fneg");
			}
			return LLVMBuildNeg(context->builder, gencontext_emit_expr(context, expr->unary_expr.expr), "neg");
		case UNARYOP_ADDR:
			return gencontext_emit_address(context, expr->unary_expr.expr);
		case UNARYOP_DEREF:
			return LLVMBuildLoad2(context->builder, BACKEND_TYPE(expr->unary_expr.expr->type), gencontext_emit_expr(context, expr->unary_expr.expr), "deref");
		case UNARYOP_INC:
			return gencontext_emit_pre_inc_dec(context, expr->unary_expr.expr, 1, false);
		case UNARYOP_DEC:
			return gencontext_emit_pre_inc_dec(context, expr->unary_expr.expr, -1, false);
	}
	UNREACHABLE
}



static LLVMValueRef gencontext_emit_logical_and_or(GenContext *context, Expr *expr, BinaryOp op)
{
	// Value *ScalarExprEmitter::VisitBinLAnd(const BinaryOperator *E)
	// For vector implementation.

	// Set up basic blocks, following Cone
	LLVMBasicBlockRef start_block = LLVMGetInsertBlock(context->builder);
	LLVMBasicBlockRef phi_block = LLVMCreateBasicBlockInContext(context->context, op == BINARYOP_AND ? "and.phi" : "or.phi");
	LLVMBasicBlockRef rhs_block = LLVMCreateBasicBlockInContext(context->context, op == BINARYOP_AND ? "and.rhs" : "or.rhs");

	// Generate left-hand condition and conditional branch
	LLVMValueRef lhs = gencontext_emit_expr(context, expr->binary_expr.left);

	if (op == BINARYOP_AND)
	{
		gencontext_emit_cond_br(context, lhs, rhs_block, phi_block);
	}
	else
	{
		gencontext_emit_cond_br(context, lhs, phi_block, rhs_block);
	}

	gencontext_emit_block(context, rhs_block);
	LLVMValueRef rhs = gencontext_emit_expr(context, expr->binary_expr.right);
	gencontext_emit_br(context, phi_block);

			// Generate phi
	gencontext_emit_block(context, phi_block);
	LLVMValueRef phi = LLVMBuildPhi(context->builder, type_bool->backend_type, "val");

	// Simplify for LLVM by entering the constants we already know of.
	LLVMValueRef result_on_skip = LLVMConstInt(LLVMInt1TypeInContext(context->context), op == BINARYOP_AND ? 0 : 1, false);
	LLVMValueRef logicValues[2] = { result_on_skip, rhs };
	LLVMBasicBlockRef blocks[2] = { start_block, rhs_block };
	LLVMAddIncoming(phi, logicValues, blocks, 2);

	return phi;
}

static inline LLVMValueRef gencontext_emit_initialization_from_expr(GenContext *context, LLVMValueRef strukt, Expr *expr)
{
	assert(expr->expr_kind == EXPR_INITIALIZER_LIST);
	// TODO
	return strukt;
}

static inline LLVMValueRef gencontext_emit_struct_value_expr(GenContext *context, Expr *expr)
{
	LLVMValueRef temp_alloc = gencontext_emit_alloca(context, BACKEND_TYPE(expr->type), "temp");
	return gencontext_emit_initialization_from_expr(context, temp_alloc, expr->struct_value_expr.init_expr);
}



static LLVMValueRef gencontext_emit_binary(GenContext *context, Expr *expr, LLVMValueRef lhs_addr, BinaryOp binary_op)
{

	if (binary_op == BINARYOP_AND || binary_op == BINARYOP_OR)
	{
		return gencontext_emit_logical_and_or(context, expr, binary_op);
	}
	Type *type = expr->type->canonical;
	Expr *lhs = expr->binary_expr.left;
	Expr *rhs = expr->binary_expr.right;

	LLVMValueRef lhs_value;
	LLVMValueRef rhs_value;
	if (lhs_addr)
	{
		lhs_value = LLVMBuildLoad2(context->builder, BACKEND_TYPE(lhs->type), lhs_addr, "");
	}
	else
	{
		lhs_value = gencontext_emit_expr(context, lhs);
	}

	rhs_value = gencontext_emit_expr(context, rhs);

	bool is_float = type_is_float(type);

	switch (binary_op)
	{
		case BINARYOP_ERROR:
			UNREACHABLE
		case BINARYOP_MULT:
			if (is_float) return LLVMBuildFMul(context->builder, lhs_value, rhs_value, "fmul");
			// TODO insert trap
			if (type_is_unsigned_integer(lhs->type->canonical))
			{
				return LLVMBuildNUWMul(context->builder, lhs_value, rhs_value, "umul");
			}
			else
			{
				return LLVMBuildNSWMul(context->builder, lhs_value, rhs_value, "mul");
			}
		case BINARYOP_MULT_MOD:
			return LLVMBuildMul(context->builder, lhs_value, rhs_value, "mul");
		case BINARYOP_SUB:
		case BINARYOP_SUB_MOD:
			if (lhs->type->canonical->type_kind == TYPE_POINTER)
			{
				if (lhs->type->canonical == rhs->type->canonical) return LLVMBuildPtrDiff(context->builder, lhs_value, rhs_value, "ptrdiff");
				rhs_value = LLVMBuildNeg(context->builder, rhs_value, "");
				return LLVMBuildGEP2(context->builder, BACKEND_TYPE(lhs->type), lhs_value, &rhs_value, 1, "ptrsub");
			}
			if (is_float) return LLVMBuildFSub(context->builder, lhs_value, rhs_value, "fsub");
			return gencontext_emit_sub_int(context, lhs->type->canonical, binary_op == BINARYOP_SUB_MOD, lhs_value, rhs_value);
		case BINARYOP_ADD:
		case BINARYOP_ADD_MOD:
			if (lhs->type->canonical->type_kind == TYPE_POINTER)
			{
				assert(type_is_integer(rhs->type->canonical));
				return LLVMBuildGEP2(context->builder, BACKEND_TYPE(lhs->type), lhs_value, &rhs_value, 1, "ptradd");
			}
			if (is_float) return LLVMBuildFAdd(context->builder, lhs_value, rhs_value, "fadd");
			return gencontext_emit_add_int(context, lhs->type->canonical, binary_op == BINARYOP_ADD_MOD, lhs_value, rhs_value);
		case BINARYOP_DIV:
			if (is_float) return LLVMBuildFDiv(context->builder, lhs_value, rhs_value, "fdiv");
			return type_is_unsigned(type)
				? LLVMBuildUDiv(context->builder, lhs_value, rhs_value, "udiv")
				: LLVMBuildSDiv(context->builder, lhs_value, rhs_value, "sdiv");
		case BINARYOP_MOD:
			return type_is_unsigned(type)
				? LLVMBuildURem(context->builder, lhs_value, rhs_value, "umod")
				: LLVMBuildSRem(context->builder, lhs_value, rhs_value, "smod");
		case BINARYOP_SHR:
			return type_is_unsigned(type)
				? LLVMBuildLShr(context->builder, lhs_value, rhs_value, "lshr")
				: LLVMBuildAShr(context->builder, lhs_value, rhs_value, "ashr");
		case BINARYOP_SHL:
			return LLVMBuildShl(context->builder, lhs_value, rhs_value, "shl");
		case BINARYOP_BIT_AND:
			return LLVMBuildAnd(context->builder, lhs_value, rhs_value, "and");
		case BINARYOP_BIT_OR:
			return LLVMBuildOr(context->builder, lhs_value, rhs_value, "or");
		case BINARYOP_BIT_XOR:
			return LLVMBuildXor(context->builder, lhs_value, rhs_value, "xor");
		case BINARYOP_EQ:
			// Unordered?
			if (type_is_float(type)) LLVMBuildFCmp(context->builder, LLVMRealUEQ, lhs_value, rhs_value, "eq");
			return LLVMBuildICmp(context->builder, LLVMIntEQ, lhs_value, rhs_value, "eq");
		case BINARYOP_NE:
			// Unordered?
			if (type_is_float(type)) LLVMBuildFCmp(context->builder, LLVMRealUNE, lhs_value, rhs_value, "neq");
			return LLVMBuildICmp(context->builder, LLVMIntNE, lhs_value, rhs_value, "neq");
		case BINARYOP_GE:
			if (type_is_float(type)) LLVMBuildFCmp(context->builder, LLVMRealUGE, lhs_value, rhs_value, "ge");
			return type_is_unsigned(type)
				? LLVMBuildICmp(context->builder, LLVMIntUGE, lhs_value, rhs_value, "ge")
				: LLVMBuildICmp(context->builder, LLVMIntSGE, lhs_value, rhs_value, "ge");
		case BINARYOP_GT:
			if (type_is_float(type)) LLVMBuildFCmp(context->builder, LLVMRealUGT, lhs_value, rhs_value, "gt");
			return type_is_unsigned(type)
			       ? LLVMBuildICmp(context->builder, LLVMIntUGT, lhs_value, rhs_value, "gt")
			       : LLVMBuildICmp(context->builder, LLVMIntSGT, lhs_value, rhs_value, "gt");
		case BINARYOP_LE:
			if (type_is_float(type)) LLVMBuildFCmp(context->builder, LLVMRealULE, lhs_value, rhs_value, "le");
			return type_is_unsigned(type)
			       ? LLVMBuildICmp(context->builder, LLVMIntULE, lhs_value, rhs_value, "le")
			       : LLVMBuildICmp(context->builder, LLVMIntSLE, lhs_value, rhs_value, "le");
		case BINARYOP_LT:
			if (type_is_float(type)) LLVMBuildFCmp(context->builder, LLVMRealULE, lhs_value, rhs_value, "lt");
			return type_is_unsigned(type)
			       ? LLVMBuildICmp(context->builder, LLVMIntULT, lhs_value, rhs_value, "lt")
			       : LLVMBuildICmp(context->builder, LLVMIntSLT, lhs_value, rhs_value, "lt");
		case BINARYOP_AND:
		case BINARYOP_OR:
			UNREACHABLE
		case BINARYOP_ASSIGN:
		case BINARYOP_MULT_ASSIGN:
		case BINARYOP_MULT_MOD_ASSIGN:
		case BINARYOP_ADD_ASSIGN:
		case BINARYOP_ADD_MOD_ASSIGN:
		case BINARYOP_SUB_ASSIGN:
		case BINARYOP_SUB_MOD_ASSIGN:
		case BINARYOP_DIV_ASSIGN:
		case BINARYOP_MOD_ASSIGN:
		case BINARYOP_AND_ASSIGN:
		case BINARYOP_OR_ASSIGN:
		case BINARYOP_BIT_AND_ASSIGN:
		case BINARYOP_BIT_OR_ASSIGN:
		case BINARYOP_BIT_XOR_ASSIGN:
		case BINARYOP_SHR_ASSIGN:
		case BINARYOP_SHL_ASSIGN:
			UNREACHABLE
	}
	UNREACHABLE
}

LLVMValueRef gencontext_emit_post_unary_expr(GenContext *context, Expr *expr)
{
	return gencontext_emit_post_inc_dec(context, expr->post_expr.expr, expr->post_expr.operator == POSTUNARYOP_INC ? 1 : -1, false);
}

static LLVMValueRef gencontext_emit_binary_expr(GenContext *context, Expr *expr)
{
	BinaryOp binary_op = expr->binary_expr.operator;
	if (binary_op > BINARYOP_ASSIGN)
	{
		BinaryOp base_op = binaryop_assign_base_op(binary_op);
		assert(base_op != BINARYOP_ERROR);
		LLVMValueRef addr = gencontext_emit_address(context, expr->binary_expr.left);
		LLVMValueRef value = gencontext_emit_binary(context, expr, addr, base_op);
		LLVMBuildStore(context->builder, value, addr);
		return value;
	}
	if (binary_op == BINARYOP_ASSIGN)
	{
		LLVMValueRef addr = gencontext_emit_address(context, expr->binary_expr.left);
		if (expr->binary_expr.right->expr_kind == EXPR_INITIALIZER_LIST)
		{
			return gencontext_emit_initialization_from_expr(context, addr, expr->binary_expr.right);
		}
		LLVMValueRef value = gencontext_emit_expr(context, expr->binary_expr.right);
		LLVMBuildStore(context->builder, value, addr);
		return value;
	}
	return gencontext_emit_binary(context, expr, NULL, binary_op);
}

LLVMValueRef gencontext_emit_elvis_expr(GenContext *context, Expr *expr)
{
	LLVMBasicBlockRef current_block = context->current_block;
	LLVMBasicBlockRef phi_block = LLVMCreateBasicBlockInContext(context->context, "cond.phi");
	LLVMBasicBlockRef rhs_block = LLVMCreateBasicBlockInContext(context->context, "cond.rhs");

	// Generate condition and conditional branch
	LLVMValueRef lhs = gencontext_emit_expr(context, expr->ternary_expr.cond);
	LLVMValueRef cond = lhs;
	Type *cond_type = expr->ternary_expr.cond->type->canonical;
	if (cond_type != type_bool)
	{
		CastKind cast = cast_to_bool_kind(cond_type);
		cond = gencontext_emit_cast(context, cast, cond, cond_type, type_bool);
	}

	gencontext_emit_cond_br(context, cond, phi_block, rhs_block);

	gencontext_emit_block(context, rhs_block);
	LLVMValueRef rhs = gencontext_emit_expr(context, expr->ternary_expr.else_expr);
	gencontext_emit_br(context, phi_block);

	// Generate phi
	gencontext_emit_block(context, phi_block);
	LLVMValueRef phi = LLVMBuildPhi(context->builder, expr->type->backend_type, "val");

	LLVMValueRef logicValues[2] = { lhs, rhs };
	LLVMBasicBlockRef blocks[2] = { current_block, rhs_block };
	LLVMAddIncoming(phi, logicValues, blocks, 2);

	return phi;
}

LLVMValueRef gencontext_emit_ternary_expr(GenContext *context, Expr *expr)
{
	if (expr->ternary_expr.then_expr == NULL) return gencontext_emit_elvis_expr(context, expr);

	// Set up basic blocks, following Cone
	LLVMBasicBlockRef phi_block = LLVMCreateBasicBlockInContext(context->context, "cond.phi");
	LLVMBasicBlockRef lhs_block = LLVMCreateBasicBlockInContext(context->context, "cond.lhs");
	LLVMBasicBlockRef rhs_block = LLVMCreateBasicBlockInContext(context->context, "cond.rhs");

	// Generate condition and conditional branch
	LLVMValueRef cond = gencontext_emit_expr(context, expr->ternary_expr.cond);

	gencontext_emit_cond_br(context, cond, lhs_block, rhs_block);

	gencontext_emit_block(context, lhs_block);
	LLVMValueRef lhs = gencontext_emit_expr(context, expr->ternary_expr.then_expr);
	gencontext_emit_br(context, phi_block);

	gencontext_emit_block(context, rhs_block);
	LLVMValueRef rhs = gencontext_emit_expr(context, expr->ternary_expr.else_expr);
	gencontext_emit_br(context, phi_block);

	// Generate phi
	gencontext_emit_block(context, phi_block);
	LLVMValueRef phi = LLVMBuildPhi(context->builder, expr->type->backend_type, "val");

	LLVMValueRef logicValues[2] = { lhs, rhs };
	LLVMBasicBlockRef blocks[2] = { lhs_block, rhs_block };
	LLVMAddIncoming(phi, logicValues, blocks, 2);

	return phi;
}

static LLVMValueRef gencontext_emit_identifier_expr(GenContext *context, Expr *expr)
{
	return LLVMBuildLoad2(context->builder, expr->identifier_expr.decl->type->canonical->backend_type,
			expr->identifier_expr.decl->var.backend_ref, expr->identifier_expr.decl->name);
}

LLVMValueRef gencontext_emit_const_expr(GenContext *context, Expr *expr)
{
	LLVMTypeRef type = BACKEND_TYPE(expr->type);
	switch (expr->const_expr.type)
	{
		case CONST_INT:
			return LLVMConstInt(type, expr->const_expr.i, type_is_unsigned(expr->type->canonical) ? false : true);
		case CONST_FLOAT:
			return LLVMConstReal(type, (double) expr->const_expr.f);
		case CONST_NIL:
			return LLVMConstNull(type);
		case CONST_BOOL:
			return LLVMConstInt(type, expr->const_expr.b ? 1 : 0, false);
		case CONST_STRING:
		{
			LLVMValueRef global_name = LLVMAddGlobal(context->module, type, "string");
			LLVMSetLinkage(global_name, LLVMInternalLinkage);
			LLVMSetGlobalConstant(global_name, 1);
			LLVMSetInitializer(global_name, LLVMConstStringInContext(context->context,
			                                                         expr->const_expr.string.chars,
			                                                         expr->const_expr.string.len,
			                                                         0));
			return global_name;
		}
	}
	UNREACHABLE
}

LLVMValueRef gencontext_emit_call_expr(GenContext *context, Expr *expr)
{
	size_t args = vec_size(expr->call_expr.arguments);
	LLVMValueRef *values = args ? malloc_arena(args * sizeof(LLVMValueRef)) : NULL;
	VECEACH(expr->call_expr.arguments, i)
	{
		values[i] = gencontext_emit_expr(context, expr->call_expr.arguments[i]);
	}


	Decl *function = expr->call_expr.function->identifier_expr.decl;

	LLVMValueRef func = function->func.backend_value;
	LLVMTypeRef func_type = BACKEND_TYPE(function->type);
	LLVMValueRef call = LLVMBuildCall2(context->builder, func_type, func, values, args, "call");
	/*
	if (function->func.function_signature.convention)
	{
		LLVMSetFunctionCallConv(call, LLVMX86StdcallCallConv);
	}*/
	return call;
}




static inline LLVMValueRef gencontext_emit_access_expr(GenContext *context, Expr *expr)
{
	// Improve, add string description to the access?
	LLVMValueRef value = gencontext_emit_address(context, expr->access_expr.parent);
	LLVMValueRef val =  LLVMBuildStructGEP2(context->builder, BACKEND_TYPE(expr->access_expr.parent->type), value, (unsigned)expr->access_expr.index, "");
	return LLVMBuildLoad2(context->builder, gencontext_get_llvm_type(context, expr->type), val, "");
}

static inline LLVMValueRef gencontext_emit_expression_list_expr(GenContext *context, Expr *expr)
{
	LLVMValueRef value = NULL;
	VECEACH(expr->expression_list, i)
	{
		value = gencontext_emit_expr(context, expr->expression_list[i]);
	}
	return value;
}

static inline LLVMValueRef gencontext_emit_initializer_list_expr(GenContext *context, Expr *expr)
{
	LLVMValueRef value = LLVMGetUndef(LLVMTYPE(expr->type));

	/*
	for (expr->initializer_expr)
	expr->type.
	else if (littype->tag == StructTag) {
		LLVMValueRef strval = LLVMGetUndef(genlType(gen, littype));
		unsigned int pos = 0;
		for (nodesFor(lit->args, cnt, nodesp))
			strval = LLVMBuildInsertValue(gen->builder, strval, genlExpr(gen, *nodesp), pos++, "literal");
		return strval;
	}
	TODO*/
	return value;
}

static inline LLVMValueRef gencontext_emit_struct_init_values_expr(GenContext *context, Expr *expr)
{
	TODO
}

LLVMValueRef gencontext_emit_expr(GenContext *context, Expr *expr)
{
	switch (expr->expr_kind)
	{
		case EXPR_POISONED:
			UNREACHABLE
		case EXPR_SCOPED_EXPR:
			return gencontext_emit_scoped_expr(context, expr);
		case EXPR_UNARY:
			return gencontext_emit_unary_expr(context, expr);
		case EXPR_CONST:
			return gencontext_emit_const_expr(context, expr);
		case EXPR_BINARY:
			return gencontext_emit_binary_expr(context, expr);
		case EXPR_TERNARY:
			return gencontext_emit_ternary_expr(context, expr);
		case EXPR_POST_UNARY:
			return gencontext_emit_post_unary_expr(context, expr);
		case EXPR_TYPE:
		case EXPR_SIZEOF:
		case EXPR_TYPE_ACCESS:
		case EXPR_TRY:
			// These are folded in the semantic analysis step.
			UNREACHABLE
		case EXPR_IDENTIFIER:
		case EXPR_SUBSCRIPT:
			return gencontext_load_expr(context, gencontext_emit_address(context, expr));
		case EXPR_CALL:
			return gencontext_emit_call_expr(context, expr);
		case EXPR_ACCESS:
			return gencontext_emit_access_expr(context, expr);
		case EXPR_STRUCT_VALUE:
			return gencontext_emit_struct_value_expr(context, expr);
		case EXPR_STRUCT_INIT_VALUES:
			return gencontext_emit_struct_init_values_expr(context, expr);
		case EXPR_INITIALIZER_LIST:
			return gencontext_emit_initializer_list_expr(context, expr);
		case EXPR_EXPRESSION_LIST:
			return gencontext_emit_expression_list_expr(context, expr);
		case EXPR_CAST:
			return gencontext_emit_cast_expr(context, expr);
	}
	UNREACHABLE
}