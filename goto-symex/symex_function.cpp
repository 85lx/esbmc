/*******************************************************************\

   Module: Symbolic Execution of ANSI-C

   Author: Daniel Kroening, kroening@kroening.com Lucas Cordeiro,
     lcc08r@ecs.soton.ac.uk

\*******************************************************************/

#include <assert.h>

#include <expr_util.h>
#include <i2string.h>
#include <cprover_prefix.h>
#include <prefix.h>
#include <arith_tools.h>
#include <base_type.h>
#include <std_expr.h>

#include <ansi-c/c_types.h>

#include "goto_symex.h"
#include "execution_state.h"

bool
goto_symext::get_unwind_recursion(
  const irep_idt &identifier, unsigned unwind)
{
  unsigned long this_loop_max_unwind = max_unwind;

  #if 1
  if (unwind != 0) {
    const symbolt &symbol = ns.lookup(identifier);

    std::string msg =
      "Unwinding recursion " +
      id2string(symbol.display_name()) +
      " iteration " + i2string(unwind);

    if (this_loop_max_unwind != 0)
      msg += " (" + i2string(this_loop_max_unwind) + " max)";

    std::cout << msg << std::endl;
  }
  #endif

  return this_loop_max_unwind != 0 &&
         unwind >= this_loop_max_unwind;
}

void
goto_symext::argument_assignments(
  const code_type2t &function_type,
  const std::vector<expr2tc> &arguments)
{
  // iterates over the operands
  std::vector<expr2tc>::const_iterator it1 = arguments.begin();

  // these are the types of the arguments
  const std::vector<type2tc> &argument_types = function_type.arguments;

  // iterates over the types of the arguments
  unsigned int name_idx = 0;
  for (std::vector<type2tc>::const_iterator it2 = argument_types.begin();
       it2 != argument_types.end(); it2++, name_idx++)
  {
    // if you run out of actual arguments there was a mismatch
    if (it1 == arguments.end()) {
      std::cerr << "function call: not enough arguments" << std::endl;
      abort();
    }

    const type2tc &arg_type = *it2;
    const irep_idt &identifier = function_type.argument_names[name_idx];

    if (identifier == "") {
      std::cerr << "no identifier for function argument" << std::endl;
      abort();
    }

    const symbolt &symbol = ns.lookup(identifier);
    exprt tmp_lhs = symbol_expr(symbol);
    expr2tc lhs;
    migrate_expr(tmp_lhs, lhs);

    if (is_nil_expr(*it1)) {
      ; // XXX jmorse, is this valid?
    } else {
      expr2tc rhs = *it1;

      // it should be the same exact type
      if (!base_type_eq(arg_type, rhs->type, ns)) {
	const type2tc &f_arg_type = arg_type;
	const type2tc &f_rhs_type = rhs->type;

	// we are willing to do some limited conversion
	if ((is_number_type(f_arg_type) ||
             is_bool_type(f_arg_type) ||
             is_pointer_type(f_arg_type)) &&
	    (is_number_type(f_rhs_type) ||
             is_bool_type(f_rhs_type) ||
             is_pointer_type(f_rhs_type))) {
          rhs = expr2tc(new typecast2t(arg_type, rhs));
	} else   {
	  std::string error = "function call: argument \"" +
	                      id2string(identifier) +
	                      "\" type mismatch: got " +
	                      get_type_id((*it1)->type)+ ", expected " +
	                      get_type_id(arg_type);
          std::cerr << error;
          abort();
	}
      }

      guardt guard;
      symex_assign_symbol(lhs, rhs, guard);
    }

    it1++;
  }

  if (function_type.ellipsis) {
    for (; it1 != arguments.end(); it1++)
    {
    }
  } else if (it1 != arguments.end())      {
    // we got too many arguments, but we will just ignore them
  }
}

void
goto_symext::symex_function_call(const expr2tc &code)
{
  const code_function_call2t &call = to_code_function_call2t(code);

  if (is_symbol2t(call.function))
    symex_function_call_code(code);
  else
    symex_function_call_deref(code);
}

void
goto_symext::symex_function_call_code(const expr2tc &expr)
{
  const code_function_call2t &call = to_code_function_call2t(expr);
  const irep_idt &identifier = to_symbol2t(call.function).get_symbol_name();

  // find code in function map

  goto_functionst::function_mapt::const_iterator it =
    goto_functions.function_map.find(identifier);

  if (it == goto_functions.function_map.end()) {
    if (has_prefix(identifier.as_string(), "symex::invalid_object")) {
      std::cout << "WARNING: function ptr call with no target, ";
      cur_state->source.pc++;
      return;
    }

    std::cerr << "failed to find `" + id2string(identifier) +
                 "' in function_map";
    abort();
  }

  const goto_functionst::goto_functiont &goto_function = it->second;

  unsigned &unwinding_counter = cur_state->function_unwind[identifier];

  // see if it's too much
  if (get_unwind_recursion(identifier, unwinding_counter)) {
    if (!options.get_bool_option("no-unwinding-assertions"))
      claim(false_expr,
            "recursion unwinding assertion");

    cur_state->source.pc++;
    return;
  }

  if (!goto_function.body_available) {
    if (body_warnings.insert(identifier).second) {
      std::string msg = "**** WARNING: no body for function " + id2string(
        identifier);
      std::cerr << msg << std::endl;
    }

    if (!is_nil_expr(call.ret)) {
      unsigned int &nondet_count = get_nondet_counter();
      expr2tc rhs = expr2tc(new symbol2t(call.ret->type,
                                  "nondet$symex::" + i2string(nondet_count++)));

      guardt guard;
      symex_assign_rec(call.ret, rhs, guard);
    }

    cur_state->source.pc++;
    return;
  }

  // read the arguments -- before the locality renaming
  std::vector<expr2tc> arguments = call.operands;
  for (unsigned i = 0; i < arguments.size(); i++)
  {
    cur_state->rename(arguments[i]);
  }

  // increase unwinding counter
  unwinding_counter++;

  // produce a new frame
  assert(!cur_state->call_stack.empty());
  goto_symex_statet::framet &frame = cur_state->new_frame(cur_state->source.thread_nr);

  // copy L1 renaming from previous frame
  frame.level1 = cur_state->previous_frame().level1;
  frame.level1._thread_id = cur_state->source.thread_nr;

  unsigned &frame_nr = cur_state->function_frame[identifier];
  frame_nr++;

  frame.calling_location = cur_state->source;

  // preserve locality of local variables
  locality(frame_nr, goto_function);

  // assign arguments
  type2tc tmp_type;
  migrate_type(goto_function.type, tmp_type);

  if (to_code_type(tmp_type).arguments.size() != arguments.size() &&
      !to_code_type(tmp_type).ellipsis) {
    std::cerr << "Function call to \"" << identifier << "\": number of "
              << "arguments doesn't match type definition; some inconsistent "
              << "rewriting occured" << std::endl;
    abort();
  }

  argument_assignments(to_code_type(tmp_type), arguments);

  frame.end_of_function = --goto_function.body.instructions.end();
  frame.return_value = call.ret;
  frame.function_identifier = identifier;

  cur_state->source.is_set = true;
  cur_state->source.pc = goto_function.body.instructions.begin();
  cur_state->source.prog = &goto_function.body;
}

static std::list<std::pair<guardt, symbol2tc> >
get_function_list(const expr2tc &expr)
{
  std::list<std::pair<guardt, symbol2tc> > l;

  if (is_if2t(expr)) {
    std::list<std::pair<guardt, symbol2tc> > l1, l2;
    const if2t &ifexpr = to_if2t(expr);
    expr2tc guardexpr = ifexpr.cond;
    expr2tc notguardexpr = expr2tc(new not2t(guardexpr));

    // Get sub items, them iterate over adding the relevant guard
    l1 = get_function_list(ifexpr.true_value);
    for (std::list<std::pair<guardt, symbol2tc> >::iterator it = l1.begin();
         it != l1.end(); it++)
      it->first.add(guardexpr);

    l2 = get_function_list(ifexpr.false_value);
    for (std::list<std::pair<guardt, symbol2tc> >::iterator it = l2.begin();
         it != l2.end(); it++)
      it->first.add(notguardexpr);

    l1.splice(l1.begin(), l2);
    return l1;
  } else if (is_symbol2t(expr)) {
    guardt guard;
    guard.make_true();
    std::pair<guardt, symbol2tc> p(guard, symbol2tc(expr));
    l.push_back(p);
    return l;
  } else if (is_typecast2t(expr)) {
    return get_function_list(to_typecast2t(expr).from);
  } else {
    std::cerr << "Unexpected irep id " << get_expr_id(expr) <<
    " in function ptr dereference" << std::endl;
    abort();
  }
}

void
goto_symext::symex_function_call_deref(const expr2tc &expr)
{
  const code_function_call2t &call = to_code_function_call2t(expr);
  assert(cur_state->top().cur_function_ptr_targets.size() == 0);

  // Indirect function call. The value is dereferenced, so we'll get either an
  // address_of a symbol, or a set of if ireps. For symbols we'll invoke
  // symex_function_call_symbol, when dealing with if's we need to fork and
  // merge.
  if (is_nil_expr(call.function)) {
    std::cerr << "Function pointer call with no targets; irep: ";
    std::cerr << call.pretty(0) << std::endl;
    abort();
  }

  // Generate a list of functions to call. We'll then proceed to call them,
  // and will later on merge them.
  expr2tc func_ptr = call.function;
  dereference(func_ptr, false);

  // Match the two varieties of failed symbol we can encounter,
  if (is_symbol2t(func_ptr) && (
   has_prefix(to_symbol2t(func_ptr).get_symbol_name(),"symex::invalid_object")||
   to_symbol2t(func_ptr).get_symbol_name().find("$object") !=std::string::npos))
  {

    // Emit warning; perform no function call behaviour. Increment PC
    // XXX jmorse - no location information any more.
    std::cout << "No target candidate for function call " <<
    from_expr(ns, "", call.function) << std::endl;
    cur_state->source.pc++;
    return;
  }

  std::list<std::pair<guardt, symbol2tc> > l = get_function_list(func_ptr);

  // Store.
  for (std::list<std::pair<guardt, symbol2tc> >::iterator it = l.begin();
       it != l.end(); it++) {

    goto_functionst::function_mapt::const_iterator fit =
      goto_functions.function_map.find(it->second->get_symbol_name());
    if (fit == goto_functions.function_map.end() ||
        !fit->second.body_available) {
      std::cerr << "Couldn't find symbol " << it->second->get_symbol_name();
      std::cerr << " or body not available, during function ptr dereference";
      std::cerr << std::endl;
      abort();
    }

    // Set up a merge of the current state into the target function.
    statet::goto_state_listt &goto_state_list =
      cur_state->top().goto_state_map[fit->second.body.instructions.begin()];

    cur_state->top().cur_function_ptr_targets.push_back(
      std::pair<goto_programt::const_targett, symbol2tc>(
        fit->second.body.instructions.begin(), it->second)
      );

    goto_state_list.push_back(statet::goto_statet(*cur_state));
    statet::goto_statet &new_state = goto_state_list.back();
    expr2tc guardexpr = it->first.as_expr();
    cur_state->rename(guardexpr);
    new_state.guard.add(guardexpr);
  }

  cur_state->top().function_ptr_call_loc = cur_state->source.pc;
  cur_state->top().function_ptr_combine_target = cur_state->source.pc;
  cur_state->top().function_ptr_combine_target++;
  cur_state->top().orig_func_ptr_call = expr;

  run_next_function_ptr_target(true);
}

bool
goto_symext::run_next_function_ptr_target(bool first)
{

  if (cur_state->call_stack.empty())
    return false;

  if (cur_state->top().cur_function_ptr_targets.size() == 0)
    return false;

  // Record a merge - when all function ptr target runs are completed, they'll
  // be merged into the state when the instruction after the func call is run.
  // But, don't do it the first time, or we'll have a merge that's effectively
  // unconditional.
  if (!first) {
    statet::goto_state_listt &goto_state_list =
      cur_state->top().goto_state_map[cur_state->top().function_ptr_combine_target];
    goto_state_list.push_back(statet::goto_statet(*cur_state));
  }

  // Take one function ptr target out of the list and jump to it. A previously
  // recorded merge will ensure it gets the right state.
  std::pair<goto_programt::const_targett, expr2tc> p =
    cur_state->top().cur_function_ptr_targets.front();
  cur_state->top().cur_function_ptr_targets.pop_front();

  goto_programt::const_targett target = p.first;
  expr2tc target_symbol = p.second;

  cur_state->guard.make_false();
  cur_state->source.pc = target;

  // Merge pre-function-ptr-call state in immediately.
  merge_gotos();

  // Now switch back to the original call location so that the call appears
  // to originate from there...
  cur_state->source.pc = cur_state->top().function_ptr_call_loc;

  // And setup the function call.
  code_function_call2tc call = cur_state->top().orig_func_ptr_call;
  call.get()->function = target_symbol;
  goto_symex_statet::framet &cur_frame = cur_state->top();

  if (cur_state->top().cur_function_ptr_targets.size() == 0)
    cur_frame.orig_func_ptr_call = expr2tc();

  symex_function_call_code(call);

  return true;
}

void
goto_symext::pop_frame(void)
{
  assert(!cur_state->call_stack.empty());

  statet::framet &frame = cur_state->top();

  // restore state
  cur_state->source.pc = frame.calling_location.pc;
  cur_state->source.prog = frame.calling_location.prog;

  // clear locals from L2 renaming
  for(statet::framet::local_variablest::const_iterator
      it=frame.local_variables.begin();
      it!=frame.local_variables.end();
      it++) {
    cur_state->level2.remove(*it);
    irep_idt orig_name = cur_state->level2.get_original_name(*it);
    cur_state->value_set.erase(orig_name);
  }

  // decrease recursion unwinding counter
  if (frame.function_identifier != "")
    cur_state->function_unwind[frame.function_identifier]--;

  cur_state->pop_frame();
}

void
goto_symext::symex_end_of_function()
{
  pop_frame();
}

void
goto_symext::locality(unsigned frame_nr,
  const goto_functionst::goto_functiont &goto_function)
{
  goto_programt::local_variablest local_identifiers;

  for (goto_programt::instructionst::const_iterator
       it = goto_function.body.instructions.begin();
       it != goto_function.body.instructions.end();
       it++)
    local_identifiers.insert(
      it->local_variables.begin(),
      it->local_variables.end());

  statet::framet &frame = cur_state->top();

  for (goto_programt::local_variablest::const_iterator
       it = local_identifiers.begin();
       it != local_identifiers.end();
       it++)
  {
    frame.level1.rename(*it, frame_nr);
    // Temporary, for symbol migration,
    expr2tc tmp_sym = expr2tc(new symbol2t(type_pool.get_empty(), *it));
    frame.level1.get_ident_name(tmp_sym);
    frame.local_variables.insert(to_symbol2t(tmp_sym).get_symbol_name());
  }
}

bool
goto_symext::make_return_assignment(expr2tc &assign, const expr2tc &code)
{
  statet::framet &frame = cur_state->top();
  const code_return2t &ret = to_code_return2t(code);

  if (!is_nil_expr(ret.operand)) {
    expr2tc value = ret.operand;

    dereference(value, false);

    if (!is_nil_expr(frame.return_value)) {
      assign = expr2tc(new code_assign2t(frame.return_value, value));

      if (frame.return_value->type != value->type) {
        expr2tc cast = expr2tc(new typecast2t(frame.return_value->type, value));
        assign = expr2tc(new code_assign2t(frame.return_value, cast));
      }

      return true;
    }
  } else {
    if (!is_nil_expr(frame.return_value)) {
      std::cerr << "return with unexpected value" << std::endl;
      abort();
    }
  }

  return false;
}

void
goto_symext::symex_return(void)
{

  // we treat this like an unconditional
  // goto to the end of the function

  // put into state-queue
  statet::goto_state_listt &goto_state_list =
    cur_state->top().goto_state_map[cur_state->top().end_of_function];

  goto_state_list.push_back(statet::goto_statet(*cur_state));

  // kill this one
  cur_state->guard.make_false();
}
