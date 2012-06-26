/*******************************************************************\

Module: Symbolic Execution of ANSI-C

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <irep2.h>
#include <migrate.h>

#include <assert.h>

#include <expr_util.h>
#include <i2string.h>
#include <arith_tools.h>
#include <cprover_prefix.h>
#include <std_types.h>
#include <base_type.h>

#include <ansi-c/c_types.h>

#include "goto_symex.h"
#include "execution_state.h"
#include "reachability_tree.h"

void goto_symext::symex_malloc(
  const expr2tc &lhs,
  const sideeffect2t &code)
{
    
  if (is_nil_expr(lhs))
    return; // ignore

  // size
  type2tc type = code.alloctype;
  expr2tc size = code.size;
  bool size_is_one = false;

  if (is_nil_expr(size))
    size_is_one=true;
  else
  {
    cur_state->rename(size);
    mp_integer i;
    if (is_constant_int2t(size) && to_constant_int2t(size).as_ulong() == 1)
      size_is_one = true;
  }
  
  if (is_nil_type(type))
    type = char_type2();

  unsigned int &dynamic_counter = get_dynamic_counter();
  dynamic_counter++;

  // value
  symbolt symbol;

  symbol.base_name = "dynamic_" + i2string(dynamic_counter) +
                     (size_is_one ? "_value" : "_array");

  symbol.name = "symex_dynamic::" + id2string(symbol.base_name);
  symbol.lvalue = true;
  
  typet renamedtype = ns.follow(migrate_type_back(type));
  if(size_is_one)
    symbol.type=renamedtype;
  else
  {
    symbol.type=typet(typet::t_array);
    symbol.type.subtype()=renamedtype;
    symbol.type.size(migrate_expr_back(size));
  }

  symbol.type.dynamic(true);

  symbol.mode="C";

  new_context.add(symbol);

  type2tc new_type;
  migrate_type(symbol.type, new_type);
  
  expr2tc rhs = expr2tc(new address_of2t(type_pool.get_empty(), expr2tc()));
  address_of2t &rhs_ref = to_address_of2t(rhs);

  if(size_is_one)
  {
    rhs_ref.type = type_pool.get_pointer(pointer_typet(symbol.type));
    rhs_ref.ptr_obj = expr2tc(new symbol2t(new_type, symbol.name));
  }
  else
  {
    type2tc subtype;
    migrate_type(symbol.type.subtype(), subtype);
    expr2tc sym = expr2tc(new symbol2t(new_type, symbol.name));
    expr2tc idx_val = expr2tc(new constant_int2t(int_type2(), BigInt(0)));
    expr2tc idx = expr2tc(new index2t(subtype, sym, idx_val));
    rhs_ref.type = type_pool.get_pointer(pointer_typet(symbol.type.subtype()));
    rhs_ref.ptr_obj = idx;
  }
  
  if (rhs_ref.type != lhs->type)
    rhs = expr2tc(new typecast2t(lhs->type, rhs));

  // Pas this point, rhs_ref may be an invalid reference.

  cur_state->rename(rhs);
  
  guardt guard;
  symex_assign_rec(lhs, rhs, guard);

  // Mark that object as being dynamic, in the __ESBMC_is_dynamic array
  type2tc sym_type = type2tc(new array_type2t(type_pool.get_bool(),
                                              expr2tc(), true));
  expr2tc sym = expr2tc(new symbol2t(sym_type, "c::__ESBMC_is_dynamic"));

  expr2tc ptr_obj = expr2tc(new pointer_object2t(int_type2(), lhs));

  expr2tc idx = expr2tc(new index2t(type_pool.get_bool(), sym, ptr_obj));

  expr2tc truth = true_expr;

  symex_assign_rec(idx, truth, guard);
}

void goto_symext::symex_printf(
  const expr2tc &lhs __attribute__((unused)),
  const expr2tc &rhs)
{

  assert(is_code_printf2t(rhs));
  expr2tc new_rhs = rhs;
  cur_state->rename(new_rhs);

  expr2t::expr_operands operands;
  new_rhs->list_operands(operands);

  const expr2tc &format = **operands.begin();
  
  if (is_address_of2t(format)) {
    const address_of2t &addrof = to_address_of2t(format);
    if (is_index2t(addrof.ptr_obj)) {
      const index2t &idx = to_index2t(addrof.ptr_obj);
      if (is_constant_string2t(idx.source_value) &&
          is_constant_int2t(idx.index) &&
          to_constant_int2t(idx.index).as_ulong() == 0) {
        const std::string &fmt =
          to_constant_string2t(idx.source_value).value.as_string();

        std::list<expr2tc> args; 
        forall_operands2(it, op_list, new_rhs)
          args.push_back(**it);

        target->output(cur_state->guard.as_expr(), cur_state->source, fmt,args);
      }
    }
  }
}

void goto_symext::symex_cpp_new(
  const expr2tc &lhs,
  const sideeffect2t &code)
{
  bool do_array;

  do_array = (code.kind == sideeffect2t::cpp_new_arr);
      
  unsigned int &dynamic_counter = get_dynamic_counter();
  dynamic_counter++;

  const std::string count_string(i2string(dynamic_counter));

  // value
  symbolt symbol;
  symbol.base_name=
    do_array?"dynamic_"+count_string+"_array":
             "dynamic_"+count_string+"_value";
  symbol.name="symex_dynamic::"+id2string(symbol.base_name);
  symbol.lvalue=true;
  symbol.mode="C++";
  
  const pointer_type2t &ptr_ref = to_pointer_type(code.type);
  typet renamedtype = ns.follow(migrate_type_back(ptr_ref.subtype));
  type2tc newtype, renamedtype2;
  migrate_type(renamedtype, renamedtype2);

  if(do_array)
  {
    newtype = type2tc(new array_type2t(renamedtype2, code.size, false));
  }
  else
    newtype = renamedtype2;

  symbol.type = migrate_type_back(newtype);

  symbol.type.dynamic(true);
  
  new_context.add(symbol);

  // make symbol expression

  expr2tc rhs = expr2tc(new address_of2t(type2tc(renamedtype2), expr2tc()));
  address_of2t &addrof = to_address_of2t(rhs);

  if(do_array)
  {
    expr2tc sym = expr2tc(new symbol2t(newtype, symbol.name));
    expr2tc zero = expr2tc(new constant_int2t(int_type2(), BigInt(0)));
    expr2tc idx = expr2tc(new index2t(renamedtype2, sym, zero));
    addrof.ptr_obj = idx;
  }
  else
    addrof.ptr_obj = expr2tc(new symbol2t(newtype, symbol.name));
  
  cur_state->rename(rhs);

  guardt guard;
  symex_assign_rec(lhs, rhs, guard);
}

// XXX - implement as a call to free?
void goto_symext::symex_cpp_delete(const expr2tc &code __attribute__((unused)))
{
  //bool do_array=code.statement()=="delete[]";
}

void
goto_symext::intrinsic_yield(reachability_treet &art)
{

  art.force_cswitch_point();
  return;
}


void
goto_symext::intrinsic_switch_to(const code_function_call2t &call,
                                 reachability_treet &art)
{

  // Switch to other thread.
  const expr2tc &num = call.operands[0];
  if (!is_constant_int2t(num)) {
    std::cerr << "Can't switch to non-constant thread id no";
    abort();
  }

  const constant_int2t &thread_num = to_constant_int2t(num);

  unsigned int tid = thread_num.constant_value.to_long();
  if (tid != art.get_cur_state().get_active_state_number())
    art.get_cur_state().switch_to_thread(tid);

  return;
}

void
goto_symext::intrinsic_switch_from(reachability_treet &art)
{

  // Mark switching back to this thread as already having been explored
  art.get_cur_state().DFS_traversed[art.get_cur_state().get_active_state_number()] = true;

  // And force a context switch.
  art.force_cswitch_point();
  return;
}


void
goto_symext::intrinsic_get_thread_id(const code_function_call2t &call,
                                     reachability_treet &art)
{
  statet &state = art.get_cur_state().get_active_state();
  unsigned int thread_id;

  thread_id = art.get_cur_state().get_active_state_number();
  expr2tc tid = expr2tc(new constant_int2t(uint_type2(), BigInt(thread_id)));

  state.value_set.assign(call.ret, tid, ns);

  expr2tc assign = expr2tc(new code_assign2t(call.ret, tid));
  assert(call.ret->type == tid->type);
  symex_assign(assign);
  return;
}

void
goto_symext::intrinsic_set_thread_data(const code_function_call2t &call,
                                       reachability_treet &art)
{
  statet &state = art.get_cur_state().get_active_state();
  expr2tc threadid = call.operands[0];
  expr2tc startdata = call.operands[1];

  state.rename(threadid);
  state.rename(startdata);

  if (!is_constant_int2t(threadid)) {
    std::cerr << "__ESBMC_set_start_data received nonconstant thread id";
    std::cerr << std::endl;
    abort();
  }

  unsigned int tid = to_constant_int2t(threadid).constant_value.to_ulong();
  art.get_cur_state().set_thread_start_data(tid, startdata);
}

void
goto_symext::intrinsic_get_thread_data(const code_function_call2t &call,
                                       reachability_treet &art)
{
  statet &state = art.get_cur_state().get_active_state();
  expr2tc threadid = call.operands[0];

  state.level2.rename(threadid);

  if (!is_constant_int2t(threadid)) {
    std::cerr << "__ESBMC_set_start_data received nonconstant thread id";
    std::cerr << std::endl;
    abort();
  }

  unsigned int tid = to_constant_int2t(threadid).constant_value.to_ulong();
  const expr2tc &startdata = art.get_cur_state().get_thread_start_data(tid);

  expr2tc assign = expr2tc(new code_assign2t(call.ret, startdata));
  assert(base_type_eq(call.ret->type, startdata->type, ns));

  state.value_set.assign(call.ret, startdata, ns);
  symex_assign(assign);
  return;
}

void
goto_symext::intrinsic_spawn_thread(const code_function_call2t &call,
                                    reachability_treet &art)
{

  // As an argument, we expect the address of a symbol.
  const expr2tc &addr = call.operands[0];
  assert(is_address_of2t(addr));
  const address_of2t &addrof = to_address_of2t(addr);
  assert(is_symbol2t(addrof.ptr_obj));
  const irep_idt &symname = to_symbol2t(addrof.ptr_obj).thename;

  goto_functionst::function_mapt::const_iterator it =
    art.goto_functions.function_map.find(symname);
  if (it == art.goto_functions.function_map.end()) {
    std::cerr << "Spawning thread \"" << symname << "\": symbol not found";
    std::cerr << std::endl;
    abort();
  }

  if (!it->second.body_available) {
    std::cerr << "Spawning thread \"" << symname << "\": no body" << std::endl;
    abort();
  }

  const goto_programt &prog = it->second.body;
  // Invalidates current state reference!
  unsigned int thread_id = art.get_cur_state().add_thread(&prog);

  statet &state = art.get_cur_state().get_active_state();

  expr2tc thread_id_exp = expr2tc(new constant_int2t(int_type2(),
                                                     BigInt(thread_id)));

  expr2tc assign = expr2tc(new code_assign2t(call.ret, thread_id_exp));
  state.value_set.assign(call.ret, thread_id_exp, ns);

  symex_assign(assign);

  // Force a context switch point. If the caller is in an atomic block, it'll be
  // blocked, but a context switch will be forced when we exit the atomic block.
  // Otherwise, this will cause the required context switch.
  art.force_cswitch_point();

  return;
}

void
goto_symext::intrinsic_terminate_thread(reachability_treet &art)
{

  art.get_cur_state().end_thread();
  // No need to force a context switch; an ended thread will cause the run to
  // end and the switcher to be invoked.
  return;
}
