/*******************************************************************\

   Module: Symbolic Execution

   Author: Daniel Kroening, kroening@kroening.com Lucas Cordeiro,
     lcc08r@ecs.soton.ac.uk

\*******************************************************************/

#include <assert.h>
#include <iostream>
#include <vector>

#include <prefix.h>
#include <std_expr.h>
#include <expr_util.h>

#include "goto_symex.h"
#include "goto_symex_state.h"
#include "execution_state.h"
#include "symex_target_equation.h"
#include "reachability_tree.h"

#include <std_expr.h>
#include "../ansi-c/c_types.h"
#include <simplify_expr.h>
#include "config.h"

void
goto_symext::claim(const exprt &claim_expr, const std::string &msg) {

  total_claims++;

  exprt expr = claim_expr;
  cur_state->rename(expr);

  // first try simplifier on it
  if (!expr.is_false())
    do_simplify(expr);

  if (expr.is_true() &&
      !options.get_bool_option("all-assertions"))
    return;

  cur_state->guard.guard_expr(expr);

  remaining_claims++;
  target->assertion(cur_state->guard, expr, msg, cur_state->gen_stack_trace(),
                    cur_state->source);
}

void
goto_symext::assume(const exprt &assumption)
{

  // Irritatingly, assumption destroys its expr argument
  exprt assumpt_dup = assumption;
  target->assumption(cur_state->guard, assumpt_dup, cur_state->source);
  return;
}

goto_symext::symex_resultt *
goto_symext::get_symex_result(void)
{

  return new goto_symext::symex_resultt(target, total_claims, remaining_claims);
}

void
goto_symext::symex_step(reachability_treet & art)
{

  assert(!cur_state->call_stack.empty());

  const goto_programt::instructiont &instruction = *cur_state->source.pc;

  merge_gotos();

  // depth exceeded?
  {
    unsigned max_depth = atoi(options.get_option("depth").c_str());
    if (max_depth != 0 && cur_state->depth > max_depth)
      cur_state->guard.add(false_exprt());
    cur_state->depth++;
  }

  // actually do instruction
  switch (instruction.type) {
  case SKIP:
  case LOCATION:
    // really ignore
    cur_state->source.pc++;
    break;

  case END_FUNCTION:

    // We must check if we can access right frame
    if(cur_state->call_stack.size()>2)
    {
      // Get the correct frame
      goto_symex_statet::call_stackt::reverse_iterator
        s_it=cur_state->call_stack.rbegin();
      ++s_it;

      // Clear the allowed exceptions, we're not on the function anymore
      (*s_it).throw_list_set.clear();

      // We don't have throw_decl anymore too
      (*s_it).has_throw_decl = false;
    }

    symex_end_of_function();

    // Potentially skip to run another function ptr target; if not,
    // continue
    if (!run_next_function_ptr_target(false))
      cur_state->source.pc++;
    break;

  case GOTO:
  {
    if(cur_state->call_stack.size())
    {
      goto_symex_statet::call_stackt::reverse_iterator
        s_it=cur_state->call_stack.rbegin();

      if((*s_it).has_throw_target)
      {
        cur_state->source.pc++;
        break;
      }
    }

    exprt tmp(instruction.guard);
    replace_dynamic_allocation(tmp);
    replace_nondet(tmp);
    dereference(tmp, false);

    symex_goto(tmp);
  }
  break;

  case ASSUME:
    if (!cur_state->guard.is_false()) {
      exprt tmp(instruction.guard);
      replace_dynamic_allocation(tmp);
      replace_nondet(tmp);
      dereference(tmp, false);

      exprt tmp1 = tmp;
      cur_state->rename(tmp);

      do_simplify(tmp);
      if (!tmp.is_true()) {
        exprt tmp2 = tmp;
        cur_state->guard.guard_expr(tmp2);

        assume(tmp2);

        // we also add it to the state guard
        cur_state->guard.add(tmp);
      }
    }
    cur_state->source.pc++;
    break;

  case ASSERT:
    if (!cur_state->guard.is_false()) {
      if (!options.get_bool_option("no-assertions") ||
          !cur_state->source.pc->location.user_provided()
          || options.get_bool_option("deadlock-check")) {

        std::string msg = cur_state->source.pc->location.comment().as_string();
        if (msg == "") msg = "assertion";
        exprt tmp(instruction.guard);

        replace_dynamic_allocation(tmp);
        replace_nondet(tmp);
        dereference(tmp, false);

        claim(tmp, msg);
      }
    }
    cur_state->source.pc++;
    break;

  case RETURN:
    if (!cur_state->guard.is_false()) {
      const code_returnt &code =
          to_code_return(instruction.code);
      code_assignt assign;
      if (make_return_assignment(assign, code))
        goto_symext::symex_assign(assign);
      symex_return();
    }

    cur_state->source.pc++;
    break;

  case ASSIGN:
    if (!cur_state->guard.is_false()) {
      codet deref_code = instruction.code;
      replace_dynamic_allocation(deref_code);
      replace_nondet(deref_code);
      assert(deref_code.operands().size() == 2);

      dereference(deref_code.op0(), true);
      dereference(deref_code.op1(), false);

      symex_assign(deref_code);
    }
    cur_state->source.pc++;
    break;

  case FUNCTION_CALL:
    if (!cur_state->guard.is_false()) {
      code_function_callt deref_code =
          to_code_function_call(instruction.code);

      replace_dynamic_allocation(deref_code);
      replace_nondet(deref_code);

      if (deref_code.lhs().is_not_nil()) {
        dereference(deref_code.lhs(), true);
      }

      dereference(deref_code.function(), false);

      Forall_expr(it, deref_code.arguments()) {
        dereference(*it, false);
      }

      if (has_prefix(deref_code.function().identifier().as_string(),
          "c::__ESBMC")) {
        cur_state->source.pc++;
        run_intrinsic(deref_code, art,
            deref_code.function().identifier().as_string());
        return;
      }

      symex_function_call(deref_code);
    } else   {
      cur_state->source.pc++;
    }
    break;

  case OTHER:
    if (!cur_state->guard.is_false()) {
      symex_other();
    }
    cur_state->source.pc++;
    break;

  case CATCH:
    symex_catch();
    break;

  case THROW:
    symex_throw();
    cur_state->source.pc++;
    break;

  case THROW_DECL:
    symex_throw_decl();
    cur_state->source.pc++;
    break;

  default:
    std::cerr << "GOTO instruction type " << instruction.type;
    std::cerr << " not handled in goto_symext::symex_step" << std::endl;
    abort();
  }
}

void
goto_symext::run_intrinsic(code_function_callt &call, reachability_treet &art,
  const std::string symname)
{

  if (symname == "c::__ESBMC_yield") {
    intrinsic_yield(art);
  } else if (symname == "c::__ESBMC_switch_to") {
    intrinsic_switch_to(call, art);
  } else if (symname == "c::__ESBMC_switch_away_from") {
    intrinsic_switch_from(art);
  } else if (symname == "c::__ESBMC_get_thread_id") {
    intrinsic_get_thread_id(call, art);
  } else if (symname == "c::__ESBMC_set_thread_internal_data") {
    intrinsic_set_thread_data(call, art);
  } else if (symname == "c::__ESBMC_get_thread_internal_data") {
    intrinsic_get_thread_data(call, art);
  } else if (symname == "c::__ESBMC_spawn_thread") {
    intrinsic_spawn_thread(call, art);
  } else if (symname == "c::__ESBMC_terminate_thread") {
    intrinsic_terminate_thread(art);
  } else if (symname == "c::__ESBMC_get_thread_state") {
    intrinsic_get_thread_state(call, art);
  } else {
    std::cerr << "Function call to non-intrinsic prefixed with __ESBMC (fatal)";
    std::cerr << std::endl << "The name in question: " << symname << std::endl;
    std::cerr <<
    "(NB: the C spec reserves the __ prefix for the compiler and environment)"
              << std::endl;
    abort();
  }

  return;
}

void
goto_symext::finish_formula(void)
{

  if (!options.get_bool_option("memory-leak-check"))
    return;

  std::list<allocated_obj>::const_iterator it;
  for (it = dynamic_memory.begin(); it != dynamic_memory.end(); it++) {
    // Assert that the allocated object was freed.
    exprt deallocd("deallocated_object", bool_typet());
    deallocd.copy_to_operands(it->obj);
    equality_exprt eq(deallocd, true_exprt());
    replace_dynamic_allocation(eq);
    it->alloc_guard.guard_expr(eq);
    cur_state->rename(eq);
    target->assertion(it->alloc_guard, eq,
                      "dereference failure: forgotten memory",
                      std::vector<dstring>(), cur_state->source);
    total_claims++;
    remaining_claims++;
  }
}
