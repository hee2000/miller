#include <stdlib.h>
#include "lib/mlr_globals.h"
#include "lib/mlrutil.h"
#include "mlr_dsl_cst.h"
#include "context_flags.h"

// xxx:
//
// assign LHS full-srec from RHS localvar    ref & write elementwise
// assign LHS full-srec from RHS oosvar      ref & write elementwise
// assign LHS full-srec from RHS full-oosvar ref & write elementwise
// assign LHS full-srec from RHS full-srec   no-op
// assign LHS full-srec from RHS maplit      alloc ephem & write elementwise
// assign LHS full-srec from RHS func-retval alloc ephem & move

// ================================================================
typedef struct _full_srec_assignment_state_t {
	rxval_evaluator_t* prhs_xevaluator;
} full_srec_assignment_state_t;

static mlr_dsl_cst_statement_handler_t handle_full_srec_assignment_nop;
static mlr_dsl_cst_statement_handler_t handle_full_srec_assignment;
static mlr_dsl_cst_statement_freer_t free_full_srec_assignment;

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_full_srec_assignment(mlr_dsl_cst_t* pcst, mlr_dsl_ast_node_t* pnode,
	int type_inferencing, int context_flags)
{
	full_srec_assignment_state_t* pstate = mlr_malloc_or_die(sizeof(
		full_srec_assignment_state_t));

	mlr_dsl_ast_node_t* plhs_node = pnode->pchildren->phead->pvvalue;
	mlr_dsl_ast_node_t* prhs_node = pnode->pchildren->phead->pnext->pvvalue;

	MLR_INTERNAL_CODING_ERROR_IF(plhs_node->type != MD_AST_NODE_TYPE_FULL_SREC);

	mlr_dsl_cst_statement_handler_t* phandler = handle_full_srec_assignment;
	if (prhs_node->type == MD_AST_NODE_TYPE_FULL_SREC) {
		// '$* = $*' is a syntactically acceptable no-op
		pstate->prhs_xevaluator = NULL;
		phandler = handle_full_srec_assignment_nop;
	} else {
		pstate->prhs_xevaluator = rxval_evaluator_alloc_from_ast(
			prhs_node, pcst->pfmgr, type_inferencing, context_flags);
		phandler = handle_full_srec_assignment;
	}

	return mlr_dsl_cst_statement_valloc(
		pnode,
		phandler,
		free_full_srec_assignment,
		pstate);
}

// ----------------------------------------------------------------
static void free_full_srec_assignment(mlr_dsl_cst_statement_t* pstatement) {
	full_srec_assignment_state_t* pstate = pstatement->pvstate;

	if (pstate->prhs_xevaluator != NULL) {
		pstate->prhs_xevaluator->pfree_func(pstate->prhs_xevaluator);
	}

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_full_srec_assignment_nop(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
}

// ----------------------------------------------------------------
static void handle_full_srec_assignment(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	full_srec_assignment_state_t* pstate = pstatement->pvstate;

	lrec_clear(pvars->pinrec);
	lhmsmv_clear(pvars->ptyped_overlay);

	rxval_evaluator_t* prhs_xevaluator = pstate->prhs_xevaluator;
	boxed_xval_t boxed_xval = prhs_xevaluator->pprocess_func(prhs_xevaluator->pvstate, pvars);

	if (!boxed_xval.xval.is_terminal) {
		for (mlhmmv_level_entry_t* pe = boxed_xval.xval.pnext_level->phead; pe != NULL; pe = pe->pnext) {
			mv_t* pkey = &pe->level_key;
			mlhmmv_value_t* pval = &pe->level_value;

			if (pval->is_terminal) { // xxx else collapse-down using json separator?
				char* skey = mv_alloc_format_val(pkey);
				mv_t val = boxed_xval.map_is_ephemeral ? pval->mlrval : mv_copy(&pval->mlrval);
				// Write typed mlrval output to the typed overlay rather than into the lrec
				// (which holds only string values).
				//
				// The rval_evaluator reads the overlay in preference to the lrec. E.g. if the
				// input had "x"=>"abc","y"=>"def" but a previous statement had set "y"=>7.4 and
				// "z"=>"ghi", then an expression right-hand side referring to $y would get the
				// floating-point value 7.4. So we don't need to lrec_put the value here, and
				// moreover should not for two reasons: (1) there is a performance hit of doing
				// throwaway number-to-string formatting -- it's better to do it once at the
				// end; (2) having the string values doubly owned by the typed overlay and the
				// lrec would result in double frees, or awkward bookkeeping. However, the NR
				// variable evaluator reads prec->field_count, so we need to put something here.
				// And putting something statically allocated minimizes copying/freeing.
				lhmsmv_put(pvars->ptyped_overlay, mlr_strdup_or_die(skey), &val,
					FREE_ENTRY_KEY | FREE_ENTRY_VALUE);
				lrec_put(pvars->pinrec, skey, "bug", FREE_ENTRY_KEY);
			}
		}
		if (boxed_xval.map_is_ephemeral) {
			mlhmmv_free_submap(boxed_xval.xval);
		}
	} else {
		mlhmmv_free_submap(boxed_xval.xval);
	}
}

// ================================================================
typedef struct _local_variable_definition_state_t {
	char*              lhs_variable_name;
	int                lhs_frame_relative_index;
	int                lhs_type_mask;
	rxval_evaluator_t* prhs_xevaluator;
} local_variable_definition_state_t;

static mlr_dsl_cst_statement_handler_t handle_local_variable_definition_from_xval;
static mlr_dsl_cst_statement_freer_t free_local_variable_definition;

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_local_variable_definition(
	mlr_dsl_cst_t*      pcst,
	mlr_dsl_ast_node_t* pnode,
	int                 type_inferencing,
	int                 context_flags,
	int                 type_mask)
{
	local_variable_definition_state_t* pstate = mlr_malloc_or_die(
		sizeof(local_variable_definition_state_t));

	pstate->lhs_variable_name        = NULL;
	pstate->lhs_frame_relative_index = MD_UNUSED_INDEX;
	pstate->lhs_type_mask            = 0;
	pstate->prhs_xevaluator          = NULL;

	mlr_dsl_ast_node_t* pname_node = pnode->pchildren->phead->pvvalue;
	pstate->lhs_variable_name = pname_node->text;
	MLR_INTERNAL_CODING_ERROR_IF(pname_node->vardef_frame_relative_index == MD_UNUSED_INDEX);
	pstate->lhs_frame_relative_index = pname_node->vardef_frame_relative_index;
	pstate->lhs_type_mask = type_mask;

	mlr_dsl_cst_statement_handler_t* pstatement_handler = NULL;
	mlr_dsl_ast_node_t* prhs_node = pnode->pchildren->phead->pnext->pvvalue;
	pstate->prhs_xevaluator = rxval_evaluator_alloc_from_ast(
		prhs_node, pcst->pfmgr, type_inferencing, context_flags);
	pstatement_handler = handle_local_variable_definition_from_xval;

	return mlr_dsl_cst_statement_valloc(
		pnode,
		pstatement_handler,
		free_local_variable_definition,
		pstate);
}

// ----------------------------------------------------------------
static void free_local_variable_definition(mlr_dsl_cst_statement_t* pstatement) {
	local_variable_definition_state_t* pstate = pstatement->pvstate;

	pstate->prhs_xevaluator->pfree_func(pstate->prhs_xevaluator);

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_local_variable_definition_from_xval(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	local_variable_definition_state_t* pstate = pstatement->pvstate;
	rxval_evaluator_t* prhs_xevaluator = pstate->prhs_xevaluator;
	boxed_xval_t boxed_xval = prhs_xevaluator->pprocess_func(prhs_xevaluator->pvstate, pvars);

	local_stack_frame_t* pframe = local_stack_get_top_frame(pvars->plocal_stack);
	if (!boxed_xval.map_is_ephemeral && !boxed_xval.xval.is_terminal) {
		local_stack_frame_define_extended(pframe,
			pstate->lhs_variable_name, pstate->lhs_frame_relative_index, pstate->lhs_type_mask,
			mlhmmv_copy_aux(&boxed_xval.xval));
	} else {
		local_stack_frame_define_extended(pframe,
			pstate->lhs_variable_name, pstate->lhs_frame_relative_index, pstate->lhs_type_mask,
			boxed_xval.xval);
	}
}

// ================================================================
typedef struct _nonindexed_local_variable_assignment_state_t {
	char*              lhs_variable_name; // For error messages only: stack-index is computed by stack-allocator:
	int                lhs_frame_relative_index;
	rxval_evaluator_t* prhs_xevaluator;
} nonindexed_local_variable_assignment_state_t;

static mlr_dsl_cst_statement_handler_t handle_nonindexed_local_variable_assignment_from_xval;
static mlr_dsl_cst_statement_freer_t free_nonindexed_local_variable_assignment;

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_nonindexed_local_variable_assignment(mlr_dsl_cst_t* pcst, mlr_dsl_ast_node_t* pnode,
	int type_inferencing, int context_flags)
{
	nonindexed_local_variable_assignment_state_t* pstate = mlr_malloc_or_die(sizeof(
		nonindexed_local_variable_assignment_state_t));

	pstate->lhs_variable_name        = NULL;
	pstate->lhs_frame_relative_index = MD_UNUSED_INDEX;
	pstate->prhs_xevaluator          = NULL;

	MLR_INTERNAL_CODING_ERROR_IF((pnode->pchildren == NULL) || (pnode->pchildren->length != 2));

	mlr_dsl_ast_node_t* plhs_node = pnode->pchildren->phead->pvvalue;
	mlr_dsl_ast_node_t* prhs_node = pnode->pchildren->phead->pnext->pvvalue;

	MLR_INTERNAL_CODING_ERROR_IF(plhs_node->type != MD_AST_NODE_TYPE_NONINDEXED_LOCAL_VARIABLE);
	MLR_INTERNAL_CODING_ERROR_IF(plhs_node->pchildren != NULL);

	pstate->lhs_variable_name = plhs_node->text;
	MLR_INTERNAL_CODING_ERROR_IF(plhs_node->vardef_frame_relative_index == MD_UNUSED_INDEX);
	pstate->lhs_frame_relative_index = plhs_node->vardef_frame_relative_index;

	mlr_dsl_cst_statement_handler_t* pstatement_handler = NULL;

	pstate->prhs_xevaluator = rxval_evaluator_alloc_from_ast(
		prhs_node, pcst->pfmgr, type_inferencing, context_flags);
	pstatement_handler = handle_nonindexed_local_variable_assignment_from_xval;

	return mlr_dsl_cst_statement_valloc(
		pnode,
		pstatement_handler,
		free_nonindexed_local_variable_assignment,
		pstate);
}

// ----------------------------------------------------------------
static void free_nonindexed_local_variable_assignment(mlr_dsl_cst_statement_t* pstatement) {
	nonindexed_local_variable_assignment_state_t* pstate = pstatement->pvstate;

	if (pstate->prhs_xevaluator != NULL) {
		pstate->prhs_xevaluator->pfree_func(pstate->prhs_xevaluator);
	}

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_nonindexed_local_variable_assignment_from_xval(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	nonindexed_local_variable_assignment_state_t* pstate = pstatement->pvstate;

	rxval_evaluator_t* prhs_xevaluator = pstate->prhs_xevaluator;
	boxed_xval_t boxed_xval = prhs_xevaluator->pprocess_func(prhs_xevaluator->pvstate, pvars);

	if (boxed_xval.xval.is_terminal) {
		if (mv_is_present(&boxed_xval.xval.mlrval)) {
			local_stack_frame_t* pframe = local_stack_get_top_frame(pvars->plocal_stack);
			local_stack_frame_assign_extended_nonindexed(pframe, pstate->lhs_frame_relative_index,
				boxed_xval.xval);
		}
	} else {
		local_stack_frame_t* pframe = local_stack_get_top_frame(pvars->plocal_stack);
		if (boxed_xval.map_is_ephemeral) {
			local_stack_frame_assign_extended_nonindexed(pframe, pstate->lhs_frame_relative_index,
				boxed_xval.xval);
		} else {
			local_stack_frame_assign_extended_nonindexed(pframe, pstate->lhs_frame_relative_index,
				mlhmmv_copy_aux(&boxed_xval.xval));
		}
	}
}

// ================================================================
typedef struct _indexed_local_variable_assignment_state_t {
	char*              lhs_variable_name; // For error messages only: stack-index is computed by stack-allocator:
	int                lhs_frame_relative_index;
	sllv_t*            plhs_keylist_evaluators;
	rxval_evaluator_t* prhs_xevaluator;
} indexed_local_variable_assignment_state_t;

static mlr_dsl_cst_statement_handler_t handle_indexed_local_variable_assignment_from_xval;
static mlr_dsl_cst_statement_freer_t free_indexed_local_variable_assignment;

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_indexed_local_variable_assignment(mlr_dsl_cst_t* pcst, mlr_dsl_ast_node_t* pnode,
	int type_inferencing, int context_flags)
{
	indexed_local_variable_assignment_state_t* pstate = mlr_malloc_or_die(sizeof(
		indexed_local_variable_assignment_state_t));

	pstate->lhs_variable_name        = NULL;
	pstate->lhs_frame_relative_index = MD_UNUSED_INDEX;
	pstate->prhs_xevaluator          = NULL;

	mlr_dsl_ast_node_t* plhs_node = pnode->pchildren->phead->pvvalue;
	mlr_dsl_ast_node_t* prhs_node = pnode->pchildren->phead->pnext->pvvalue;

	MLR_INTERNAL_CODING_ERROR_IF(plhs_node->type != MD_AST_NODE_TYPE_INDEXED_LOCAL_VARIABLE);
	MLR_INTERNAL_CODING_ERROR_IF(plhs_node->pchildren == NULL);

	pstate->lhs_variable_name = plhs_node->text;
	MLR_INTERNAL_CODING_ERROR_IF(plhs_node->vardef_frame_relative_index == MD_UNUSED_INDEX);
	pstate->lhs_frame_relative_index = plhs_node->vardef_frame_relative_index;

	pstate->plhs_keylist_evaluators = allocate_keylist_evaluators_from_ast_node(
		plhs_node, pcst->pfmgr, type_inferencing, context_flags);

	mlr_dsl_cst_statement_handler_t* pstatement_handler = NULL;

	pstate->prhs_xevaluator = rxval_evaluator_alloc_from_ast(
		prhs_node, pcst->pfmgr, type_inferencing, context_flags);
	pstatement_handler = handle_indexed_local_variable_assignment_from_xval;

	return mlr_dsl_cst_statement_valloc(
		pnode,
		pstatement_handler,
		free_indexed_local_variable_assignment,
		pstate);
}

// ----------------------------------------------------------------
static void free_indexed_local_variable_assignment(mlr_dsl_cst_statement_t* pstatement) {
	indexed_local_variable_assignment_state_t* pstate = pstatement->pvstate;

	for (sllve_t* pe = pstate->plhs_keylist_evaluators->phead; pe != NULL; pe = pe->pnext) {
		rval_evaluator_t* pev = pe->pvvalue;
		pev->pfree_func(pev);
	}
	sllv_free(pstate->plhs_keylist_evaluators);

	pstate->prhs_xevaluator->pfree_func(pstate->prhs_xevaluator);

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_indexed_local_variable_assignment_from_xval(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	indexed_local_variable_assignment_state_t* pstate = pstatement->pvstate;

	int lhs_keys_all_non_null_or_error;
	sllmv_t* pmvkeys = evaluate_list(pstate->plhs_keylist_evaluators, pvars, &lhs_keys_all_non_null_or_error);
	if (lhs_keys_all_non_null_or_error) {

		rxval_evaluator_t* prhs_xevaluator = pstate->prhs_xevaluator;
		boxed_xval_t boxed_xval = prhs_xevaluator->pprocess_func(prhs_xevaluator->pvstate, pvars);

		if (boxed_xval.xval.is_terminal) {
			if (mv_is_present(&boxed_xval.xval.mlrval)) {
				local_stack_frame_t* pframe = local_stack_get_top_frame(pvars->plocal_stack);
				local_stack_frame_assign_extended_indexed(pframe, pstate->lhs_frame_relative_index,
					pmvkeys, boxed_xval.xval);
			}
		} else {
			local_stack_frame_t* pframe = local_stack_get_top_frame(pvars->plocal_stack);
			if (boxed_xval.map_is_ephemeral) {
				local_stack_frame_assign_extended_indexed(pframe, pstate->lhs_frame_relative_index,
					pmvkeys, boxed_xval.xval);
			} else {
				local_stack_frame_assign_extended_nonindexed(pframe, pstate->lhs_frame_relative_index,
					mlhmmv_copy_aux(&boxed_xval.xval));
			}
		}
	}
	sllmv_free(pmvkeys);

}

// ================================================================
// All assignments produce a mlrval on the RHS and store it on the left -- except if both LHS and RHS
// are oosvars in which case there are recursive copies, or in case of $* on the LHS or RHS.

typedef struct _oosvar_assignment_state_t {
	sllv_t*            plhs_keylist_evaluators;
	rxval_evaluator_xxx_deprecated_t* prhs_xevaluator;
} oosvar_assignment_state_t;

static mlr_dsl_cst_statement_handler_t handle_oosvar_assignment_from_xval;
static mlr_dsl_cst_statement_freer_t free_oosvar_assignment;

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_oosvar_assignment(mlr_dsl_cst_t* pcst, mlr_dsl_ast_node_t* pnode,
	int type_inferencing, int context_flags)
{
	oosvar_assignment_state_t* pstate = mlr_malloc_or_die(sizeof(oosvar_assignment_state_t));

	pstate->plhs_keylist_evaluators = NULL;
	pstate->prhs_xevaluator         = NULL;

	mlr_dsl_ast_node_t* plhs_node = pnode->pchildren->phead->pvvalue;
	mlr_dsl_ast_node_t* prhs_node = pnode->pchildren->phead->pnext->pvvalue;

	MLR_INTERNAL_CODING_ERROR_IF(plhs_node->type != MD_AST_NODE_TYPE_OOSVAR_KEYLIST);

	pstate->plhs_keylist_evaluators = allocate_keylist_evaluators_from_ast_node(
		plhs_node, pcst->pfmgr, type_inferencing, context_flags);

	mlr_dsl_cst_statement_handler_t* pstatement_handler = NULL;

	pstate->prhs_xevaluator = rxval_evaluator_alloc_from_ast_xxx_deprecated(
		prhs_node, pcst->pfmgr, type_inferencing, context_flags);
	pstatement_handler = handle_oosvar_assignment_from_xval;

	return mlr_dsl_cst_statement_valloc(
		pnode,
		pstatement_handler,
		free_oosvar_assignment,
		pstate);
}

// ----------------------------------------------------------------
static void free_oosvar_assignment(mlr_dsl_cst_statement_t* pstatement) {
	oosvar_assignment_state_t* pstate = pstatement->pvstate;

	for (sllve_t* pe = pstate->plhs_keylist_evaluators->phead; pe != NULL; pe = pe->pnext) {
		rval_evaluator_t* pev = pe->pvvalue;
		pev->pfree_func(pev);
	}
	sllv_free(pstate->plhs_keylist_evaluators);
	if (pstate->prhs_xevaluator != NULL) {
		pstate->prhs_xevaluator->pfree_func(pstate->prhs_xevaluator);
	}

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_oosvar_assignment_from_xval(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	oosvar_assignment_state_t* pstate = pstatement->pvstate;

	int lhs_all_non_null_or_error = TRUE;
	sllmv_t* plhskeys = evaluate_list(pstate->plhs_keylist_evaluators, pvars,
		&lhs_all_non_null_or_error);

	if (lhs_all_non_null_or_error) {
		rxval_evaluator_xxx_deprecated_t* prhs_xevaluator = pstate->prhs_xevaluator;
		mlhmmv_value_t xval = prhs_xevaluator->pprocess_func(prhs_xevaluator->pvstate, pvars);
		if (!xval.is_terminal || mv_is_present(&xval.mlrval)) { // xxx funcify
			mlhmmv_put_value_at_level_aux(pvars->poosvars->proot_level, plhskeys->phead, &xval); // xxx rename
		} else {
			mlhmmv_free_submap(xval); // xxx rename
		}
	}

	sllmv_free(plhskeys);
}

// ================================================================
// All assignments produce a mlrval on the RHS and store it on the left -- except if both LHS and RHS
// are oosvars in which case there are recursive copies, or in case of $* on the LHS or RHS.

typedef struct _oosvar_from_full_srec_assignment_state_t {
	sllv_t* plhs_keylist_evaluators;
} oosvar_from_full_srec_assignment_state_t;

static mlr_dsl_cst_statement_handler_t handle_oosvar_from_full_srec_assignment;
static mlr_dsl_cst_statement_freer_t free_oosvar_from_full_srec_assignment;

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_oosvar_from_full_srec_assignment(mlr_dsl_cst_t* pcst, mlr_dsl_ast_node_t* pnode,
	int type_inferencing, int context_flags)
{
	oosvar_from_full_srec_assignment_state_t* pstate = mlr_malloc_or_die(sizeof(
		oosvar_from_full_srec_assignment_state_t));

	mlr_dsl_ast_node_t* plhs_node = pnode->pchildren->phead->pvvalue;
	mlr_dsl_ast_node_t* prhs_node = pnode->pchildren->phead->pnext->pvvalue;

	MLR_INTERNAL_CODING_ERROR_IF(plhs_node->type != MD_AST_NODE_TYPE_OOSVAR_KEYLIST);
	MLR_INTERNAL_CODING_ERROR_IF(prhs_node->type != MD_AST_NODE_TYPE_FULL_SREC);

	pstate->plhs_keylist_evaluators = allocate_keylist_evaluators_from_ast_node(
		plhs_node, pcst->pfmgr, type_inferencing, context_flags);

	return mlr_dsl_cst_statement_valloc(
		pnode,
		handle_oosvar_from_full_srec_assignment,
		free_oosvar_from_full_srec_assignment,
		pstate);
}

// ----------------------------------------------------------------
static void free_oosvar_from_full_srec_assignment(mlr_dsl_cst_statement_t* pstatement) {
	oosvar_from_full_srec_assignment_state_t* pstate = pstatement->pvstate;

	for (sllve_t* pe = pstate->plhs_keylist_evaluators->phead; pe != NULL; pe = pe->pnext) {
		rval_evaluator_t* pev = pe->pvvalue;
		pev->pfree_func(pev);
	}
	sllv_free(pstate->plhs_keylist_evaluators);

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_oosvar_from_full_srec_assignment(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	oosvar_from_full_srec_assignment_state_t* pstate = pstatement->pvstate;

	int all_non_null_or_error = TRUE;
	sllmv_t* plhskeys = evaluate_list(pstate->plhs_keylist_evaluators, pvars, &all_non_null_or_error);
	if (all_non_null_or_error) {

		mlhmmv_level_t* plevel = mlhmmv_get_or_create_level(pvars->poosvars, plhskeys);
		if (plevel != NULL) {

			mlhmmv_clear_level(plevel);

			for (lrece_t* pe = pvars->pinrec->phead; pe != NULL; pe = pe->pnext) {
				mv_t k = mv_from_string(pe->key, NO_FREE); // mlhmmv_put_terminal_from_level will copy
				sllmve_t e = { .value = k, .free_flags = 0, .pnext = NULL };
				mv_t* pomv = lhmsmv_get(pvars->ptyped_overlay, pe->key);
				if (pomv != NULL) {
					mlhmmv_put_terminal_from_level(plevel, &e, pomv);
				} else {
					mv_t v = mv_from_string(pe->value, NO_FREE); // mlhmmv_put_terminal_from_level will copy
					mlhmmv_put_terminal_from_level(plevel, &e, &v);
				}
			}

		}
	}
	sllmv_free(plhskeys);
}
