/*******************************************************************\

Module: Symbolic Execution of ANSI-C

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <irep2.h>
#include <migrate.h>

#include <pointer-analysis/dereference.h>
#include <langapi/language_util.h>

#include "goto_symex.h"

class symex_dereference_statet:
  public dereference_callbackt
{
public:
  symex_dereference_statet(
    goto_symext &_goto_symex,
    goto_symext::statet &_state):
    goto_symex(_goto_symex),
    state(_state)
  {
  }

protected:
  goto_symext &goto_symex;
  goto_symext::statet &state;

  // overloads from dereference_callbackt
  // XXXjmorse - no it doesn't. This should be virtual pure!
  virtual bool is_valid_object(const irep_idt &identifier __attribute__((unused)))
  {
    return true;
  }
#if 1
  virtual void dereference_failure(
    const std::string &property,
    const std::string &msg,
    const guardt &guard);
#endif
  virtual void get_value_set(
    const expr2tc &expr,
    value_setst::valuest &value_set);

  virtual bool has_failed_symbol(
    const expr2tc &expr,
    const symbolt *&symbol);
};

void symex_dereference_statet::dereference_failure(
  const std::string &property __attribute__((unused)),
  const std::string &msg __attribute__((unused)),
  const guardt &guard __attribute__((unused)))
{
  // XXXjmorse - this is clearly wrong, but we can't do anything about it until
  // we fix the memory model.
}

bool symex_dereference_statet::has_failed_symbol(
  const expr2tc &expr,
  const symbolt *&symbol)
{

  if (is_symbol2t(expr))
  {
    // Null and invalid name lookups will fail.
    if (to_symbol2t(expr).thename == "NULL" ||
        to_symbol2t(expr).thename == "INVALID")
      return false;

    const symbolt &ptr_symbol = goto_symex.ns.lookup(to_symbol2t(expr).thename);

    const irep_idt &failed_symbol=
      ptr_symbol.type.failed_symbol();

    if(failed_symbol=="") return false;

    return !goto_symex.ns.lookup(failed_symbol, symbol);
  }

  return false;
}

void symex_dereference_statet::get_value_set(
  const expr2tc &expr,
  value_setst::valuest &value_set)
{

  state.value_set.get_value_set(expr, value_set, goto_symex.ns);
}

void goto_symext::dereference_rec(
  expr2tc &expr,
  guardt &guard,
  dereferencet &dereference,
  const bool write)
{

  if (is_dereference2t(expr))
  {
    dereference2t &deref = to_dereference2t(expr);

    // first make sure there are no dereferences in there
    dereference_rec(deref.value, guard, dereference, false);

    if (is_array_type(to_pointer_type(deref.value->type).subtype)) {
      // If our dereference yields an array, we're not actually performing
      // a dereference, we're performing pointer arithmetic and getting another
      // pointer out of this. Simply drop the dereference.
      expr2tc tmp = deref.value;
      const array_type2t &arr =
        to_array_type(to_pointer_type(deref.value->type).subtype);

      tmp.get()->type = type2tc(new pointer_type2t(arr.subtype));
      expr = tmp;
      return;
    }

    dereference.dereference(deref.value, guard,
                            write ? dereferencet::WRITE : dereferencet::READ);
    expr = deref.value;
  }
  else if (is_index2t(expr) &&
           is_pointer_type(to_index2t(expr).source_value))
  {
    index2t &index = to_index2t(expr);
    add2tc tmp(index.source_value->type, index.source_value, index.index);

    // first make sure there are no dereferences in there
    dereference_rec(tmp, guard, dereference, false);

    dereference.dereference(tmp, guard,
                            write ? dereferencet::WRITE : dereferencet::READ);
    expr = tmp;
  }
  else
  {
    Forall_operands2(it, idx, expr) {
      if (is_nil_expr(*it))
        continue;

      dereference_rec(*it, guard, dereference, write);
    }

    // Workaround: we may have just rewritten an index operand. Redereference
    // if that's the case.
    if (is_index2t(expr) &&
        is_pointer_type(to_index2t(expr).source_value))
      dereference_rec(expr, guard, dereference, write);
  }
}

void goto_symext::dereference(expr2tc &expr, const bool write, bool free, bool internal)
{
  symex_dereference_statet symex_dereference_state(*this, *cur_state);

if (free || internal)
  return;

  dereferencet dereference(
    ns,
    new_context,
    options,
    symex_dereference_state);

  // needs to be renamed to level 1
  assert(!cur_state->call_stack.empty());
  cur_state->top().level1.rename(expr);

  guardt guard = cur_state->guard;
  dereference_rec(expr, guard, dereference, write);
}
