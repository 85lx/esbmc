/*******************************************************************\

Module: Symbolic Execution

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <irep2.h>
#include <migrate.h>
#include <assert.h>

#include <simplify_expr.h>
#include <i2string.h>
#include <cprover_prefix.h>
#include <expr_util.h>
#include <std_expr.h>

#include <ansi-c/c_types.h>

#include "goto_symex.h"
#include "dynamic_allocation.h"
#include "execution_state.h"

goto_symext::goto_symext(const namespacet &_ns, contextt &_new_context,
                         const goto_functionst &_goto_functions,
                         symex_targett *_target, const optionst &opts) :
  guard_identifier_s("goto_symex::\\guard"),
  total_claims(0),
  remaining_claims(0),
  constant_propagation(true),
  ns(_ns),
  options(opts),
  new_context(_new_context),
  goto_functions(_goto_functions),
  target(_target),
  cur_state(NULL),
  last_throw(NULL),
  inside_unexpected(false),
  unwinding_recursion_assumption(false),
  depth_limit(atol(options.get_option("depth").c_str())),
  break_insn(atol(options.get_option("break-at").c_str())),
  memory_leak_check(options.get_bool_option("memory-leak-check")),
  deadlock_check(options.get_bool_option("deadlock-check")),
  no_assertions(options.get_bool_option("no-assertions")),
  no_simplify(options.get_bool_option("no-simplify")),
  no_unwinding_assertions(options.get_bool_option("no-unwinding-assertions")),
  partial_loops(options.get_bool_option("partial-loops")),
  k_induction(options.get_bool_option("k-induction")),
  base_case(options.get_bool_option("base-case")),
  forward_condition(options.get_bool_option("forward-condition"))
{
  const std::string &set = options.get_option("unwindset");
  unsigned int length = set.length();

  for(unsigned int idx = 0; idx < length; idx++)
  {
    std::string::size_type next = set.find(",", idx);
    std::string val = set.substr(idx, next - idx);
    unsigned long id = atoi(val.substr(0, val.find(":", 0)).c_str());
    unsigned long uw = atol(val.substr(val.find(":", 0) + 1).c_str());
    unwind_set[id] = uw;
    if(next == std::string::npos) break;
    idx = next;
  }

  max_unwind=atol(options.get_option("unwind").c_str());

  art1 = NULL;

  // Work out whether or not we'll be modelling with cpp:: or c:: arrays.
  const symbolt *sp;
  if (!ns.lookup(irep_idt("c::__ESBMC_alloc"), sp)) {
    valid_ptr_arr_name = "c::__ESBMC_alloc";
    alloc_size_arr_name = "c::__ESBMC_alloc_size";
    deallocd_arr_name = "c::__ESBMC_deallocated";
    dyn_info_arr_name = "c::__ESBMC_is_dynamic";
  } else {
    valid_ptr_arr_name = "cpp::__ESBMC_alloc";
    alloc_size_arr_name = "cpp::__ESBMC_alloc_size";
    deallocd_arr_name = "cpp::__ESBMC_deallocated";
    dyn_info_arr_name = "cpp::__ESBMC_is_dynamic";
  }
}

goto_symext::goto_symext(const goto_symext &sym) :
  ns(sym.ns),
  options(sym.options),
  new_context(sym.new_context),
  goto_functions(sym.goto_functions),
  last_throw(NULL),
  inside_unexpected(false),
  unwinding_recursion_assumption(false)
{
  *this = sym;
}

goto_symext& goto_symext::operator=(const goto_symext &sym)
{
  body_warnings = sym.body_warnings;
  unwind_set = sym.unwind_set;
  max_unwind = sym.max_unwind;
  constant_propagation = sym.constant_propagation;
  total_claims = sym.total_claims;
  remaining_claims = sym.remaining_claims;
  guard_identifier_s = sym.guard_identifier_s;
  depth_limit = sym.depth_limit;
  break_insn = sym.break_insn;
  memory_leak_check = sym.memory_leak_check;
  deadlock_check = sym.deadlock_check;
  no_assertions = sym.no_assertions;
  no_simplify = sym.no_simplify;
  no_unwinding_assertions = sym.no_unwinding_assertions;
  partial_loops = sym.partial_loops;
  k_induction = sym.k_induction;
  base_case = sym.base_case;
  forward_condition = sym.forward_condition;

  valid_ptr_arr_name = sym.valid_ptr_arr_name;
  alloc_size_arr_name = sym.alloc_size_arr_name;
  deallocd_arr_name = sym.deallocd_arr_name;
  dyn_info_arr_name = sym.dyn_info_arr_name;

  dynamic_memory = sym.dynamic_memory;

  // Art ptr is shared
  art1 = sym.art1;

  // Symex target is another matter; a higher up class needs to decide
  // whether we're duplicating it or using the same one.
  target = NULL;

  return *this;
}

void goto_symext::do_simplify(exprt &expr)
{
  if(!no_simplify)
    simplify(expr);
}

void goto_symext::do_simplify(expr2tc &expr)
{
  if(!no_simplify) {
    expr2tc tmp = expr->simplify();
    if (!is_nil_expr(tmp))
      expr = tmp;
  }
}

void goto_symext::symex_assign(const expr2tc &code_assign)
{
  //replace_dynamic_allocation(state, lhs);
  //replace_dynamic_allocation(state, rhs);

  const code_assign2t &code = to_code_assign2t(code_assign);

  expr2tc lhs = code.target;
  expr2tc rhs = code.source;

  replace_nondet(lhs);
  replace_nondet(rhs);

  if (is_sideeffect2t(rhs))
  {
    const sideeffect2t &effect = to_sideeffect2t(rhs);
    switch (effect.kind) {
    case sideeffect2t::cpp_new:
    case sideeffect2t::cpp_new_arr:
      symex_cpp_new(lhs, effect);
      break;
    case sideeffect2t::malloc:
      symex_malloc(lhs, effect);
      break;
    // No nondet side effect?
    default:
      assert(0 && "unexpected side effect");
    }
  }
  else
  {
    guardt guard; // NOT the state guard!
    symex_assign_rec(lhs, rhs, guard);
  }
}

void goto_symext::symex_assign_rec(
  const expr2tc &lhs,
  expr2tc &rhs,
  guardt &guard)
{

  if (is_symbol2t(lhs)) {
    symex_assign_symbol(lhs, rhs, guard);
  } else if (is_index2t(lhs)) {
    symex_assign_array(lhs, rhs, guard);
  } else if (is_member2t(lhs)) {
    symex_assign_member(lhs, rhs, guard);
  } else if (is_if2t(lhs)) {
    symex_assign_if(lhs, rhs, guard);
  } else if (is_typecast2t(lhs)) {
    symex_assign_typecast(lhs, rhs, guard);
   } else if (is_constant_string2t(lhs) ||
           is_null_object2t(lhs) ||
           is_zero_string2t(lhs))
  {
    // ignore
  } else if (is_byte_extract2t(lhs)) {
    symex_assign_byte_extract(lhs, rhs, guard);
  } else if (is_concat2t(lhs)) {
    symex_assign_concat(lhs, rhs, guard);
  } else {
    std::cerr <<  "assignment to " << get_expr_id(lhs) << " not handled"
              << std::endl;
    abort();
  }
}

void goto_symext::symex_assign_symbol(
  const expr2tc &lhs,
  expr2tc &rhs,
  guardt &guard)
{
  // put assignment guard in rhs

  if (!guard.empty())
  {
    rhs = if2tc(rhs->type, guard.as_expr(), rhs, lhs);
  }

  expr2tc orig_name_lhs = lhs;
  cur_state->get_original_name(orig_name_lhs);
  cur_state->rename(rhs);

  do_simplify(rhs);

  expr2tc renamed_lhs = lhs;
  cur_state->assignment(renamed_lhs, rhs, constant_propagation);

  guardt tmp_guard(cur_state->guard);
  tmp_guard.append(guard);

  // do the assignment
  target->assignment(
    tmp_guard.as_expr(),
    renamed_lhs, orig_name_lhs,
    rhs,
    cur_state->source,
    cur_state->gen_stack_trace(),
    symex_targett::STATE);

}

void goto_symext::symex_assign_typecast(
  const expr2tc &lhs,
  expr2tc &rhs,
  guardt &guard)
{
  // these may come from dereferencing on the lhs

  const typecast2t &cast = to_typecast2t(lhs);
  expr2tc rhs_typecasted = rhs;
  rhs_typecasted = typecast2tc(cast.from->type, rhs);

  symex_assign_rec(cast.from, rhs_typecasted, guard);
}

void goto_symext::symex_assign_array(
  const expr2tc &lhs,
  expr2tc &rhs,
  guardt &guard)
{
  // lhs must be index operand
  // that takes two operands: the first must be an array
  // the second is the index

  const index2t &index = to_index2t(lhs);

  assert(is_array_type(index.source_value) ||
         is_string_type(index.source_value));

  // turn
  //   a[i]=e
  // into
  //   a'==a WITH [i:=e]

  with2tc new_rhs(index.source_value->type, index.source_value,
                  index.index, rhs);

  symex_assign_rec(index.source_value, new_rhs, guard);
}

void goto_symext::symex_assign_member(
  const expr2tc &lhs,
  expr2tc &rhs,
  guardt &guard)
{
  // symbolic execution of a struct member assignment

  // lhs must be member operand
  // that takes one operands, which must be a structure

  const member2t &member = to_member2t(lhs);

  assert(is_struct_type(member.source_value) ||
         is_union_type(member.source_value));

  const irep_idt &component_name = member.member;
  expr2tc real_lhs = member.source_value;

  // typecasts involved? C++ does that for inheritance.
  if (is_typecast2t(member.source_value))
  {
    const typecast2t &cast = to_typecast2t(member.source_value);
    if (is_null_object2t(cast.from))
    {
      // ignore
    }
    else
    {
      // remove the type cast, we assume that the member is there
      real_lhs = cast.from;
      assert(is_struct_type(real_lhs) || is_union_type(real_lhs));
    }
  }

  // turn
  //   a.c=e
  // into
  //   a'==a WITH [c:=e]

  type2tc str_type =
    type2tc(new string_type2t(component_name.as_string().size()));
  with2tc new_rhs(real_lhs->type, real_lhs,
                       constant_string2tc(str_type, component_name),
                       rhs);

  symex_assign_rec(member.source_value, new_rhs, guard);
}

void goto_symext::symex_assign_if(
  const expr2tc &lhs,
  expr2tc &rhs,
  guardt &guard)
{
  // we have (c?a:b)=e;

  unsigned old_guard_size=guard.size();

  // need to copy rhs -- it gets destroyed
  expr2tc rhs_copy = rhs;
  const if2t &ifval = to_if2t(lhs);

  expr2tc cond = ifval.cond;

  guard.add(cond);
  symex_assign_rec(ifval.true_value, rhs, guard);
  guard.resize(old_guard_size);

  not2tc not_cond(cond);

  guard.add(not_cond);
  symex_assign_rec(ifval.false_value, rhs_copy, guard);
  guard.resize(old_guard_size);
}

void goto_symext::symex_assign_byte_extract(
  const expr2tc &lhs,
  expr2tc &rhs,
  guardt &guard)
{
  // we have byte_extract_X(l, b)=r
  // turn into l=byte_update_X(l, b, r)

  // Grief: multi dimensional arrays.
  const byte_extract2t &extract = to_byte_extract2t(lhs);

  if (is_multi_dimensional_array(extract.source_value)) {
    const array_type2t &arr_type = to_array_type(extract.source_value->type);
    assert(!is_multi_dimensional_array(arr_type.subtype) &&
           "Can't currently byte extract through more than two dimensions of "
           "array right now, sorry");
    constant_int2tc subtype_sz(index_type2(),
                               type_byte_size(*arr_type.subtype));
    expr2tc div = div2tc(index_type2(), extract.source_offset, subtype_sz);
    expr2tc mod = modulus2tc(index_type2(), extract.source_offset, subtype_sz);
    do_simplify(div);
    do_simplify(mod);

    index2tc idx(arr_type.subtype, extract.source_value, div);
    byte_update2tc be2(arr_type.subtype, idx, mod, rhs, extract.big_endian);
    with2tc store(extract.source_value->type, extract.source_value, div, be2);
    symex_assign_rec(extract.source_value, store, guard);
  } else {
    byte_update2tc new_rhs(extract.source_value->type, extract.source_value,
                           extract.source_offset, rhs,
                           extract.big_endian);

    symex_assign_rec(extract.source_value, new_rhs, guard);
  }
}

void goto_symext::symex_assign_concat(
  const expr2tc &lhs,
  expr2tc &rhs,
  guardt &guard)
{
  // Right: generate a series of symex assigns.
  const concat2t &cat = to_concat2t(lhs);
  assert(cat.type->get_width() > 8);
  assert(is_scalar_type(rhs));

  // Ensure we're dealing with a bitvector.
  if (!is_bv_type(rhs))
    rhs = typecast2tc(get_uint_type(rhs->type->get_width()), rhs);

  if (cat.side_1->type->get_width() == 8) {
    unsigned int shift_distance = cat.type->get_width() - 8;
    expr2tc shift_dist = gen_uint(shift_distance);
    shift_dist = typecast2tc(rhs->type, shift_dist);
    ashr2tc shr(rhs->type, rhs, shift_dist);
    typecast2tc shr_cast(get_uint8_type(), shr);

    // Assign byte from rhs to first lhs operand.
    symex_assign_rec(cat.side_1, shr_cast, guard);

    // Assign the remainder of the rhs to the lhs's second operand.
    // XXX -- am I assuming little endian here?
    typecast2tc cast(get_uint_type(shift_distance), rhs);
    symex_assign_rec(cat.side_2, cast, guard);
  } else {
    assert(is_scalar_type(rhs) && "Can't assign a nonscalar to a concat expr");
    // Extract lower bytes
    typecast2tc cast(cat.side_2->type, rhs);
    symex_assign_rec(cat.side_2, cast, guard);

    // Shift those bytes off the end, and assign the remainder to the other
    // operand.
    unsigned int shift_distance = cat.side_2->type->get_width();
    expr2tc shift_dist = gen_uint(shift_distance);
    shift_dist = typecast2tc(rhs->type, shift_dist);
    ashr2tc shr(rhs->type, rhs, shift_dist);

    unsigned int cast_size = cat.type->get_width() - shift_distance;
    typecast2tc cast2(get_uint_type(cast_size), rhs);
    symex_assign_rec(cat.side_1, cast2, guard);
  }
}

void goto_symext::replace_nondet(expr2tc &expr)
{
  if (is_sideeffect2t(expr) &&
      to_sideeffect2t(expr).kind == sideeffect2t::nondet)
  {
    unsigned int &nondet_count = get_dynamic_counter();
    expr = symbol2tc(expr->type,
                              "nondet$symex::nondet"+i2string(nondet_count++));
  }
  else
  {
    Forall_operands2(it, idx, expr) {
      if (!is_nil_expr(*it))
        replace_nondet(*it);
    }
  }
}
