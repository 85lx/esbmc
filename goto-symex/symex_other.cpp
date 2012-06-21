/*******************************************************************\

Module: Symbolic Execution

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <irep2.h>

#include <assert.h>

#include <expr_util.h>

#include "goto_symex.h"

void goto_symext::symex_other(void)
{
  const goto_programt::instructiont &instruction=*cur_state->source.pc;

  expr2tc code2 = instruction.code;

  if (is_code_expression2t(code2))
  {
    // ignore
  }
  else if (is_code_cpp_del_array2t(code2) || is_code_cpp_delete2t(code2))
  {
    expr2tc deref_code(code2);

    replace_dynamic_allocation(deref_code);
    replace_nondet(deref_code);
    dereference(deref_code, false);

    symex_cpp_delete(deref_code);
  }
  else if (is_code_free2t(code2))
  {
    // ignore
  }
  else if (is_code_printf2t(code2))
  {
    replace_dynamic_allocation(code2);
    replace_nondet(code2);
    dereference(code2, false);
    symex_printf(expr2tc(), code2);
  }
  else if (is_code_decl2t(code2))
  {
    replace_dynamic_allocation(code2);
    replace_nondet(code2);
    dereference(code2, false);

    const code_decl2t &decl_code = to_code_decl2t(code2);

    // just do the L2 renaming to preseve locality
    const irep_idt &identifier = decl_code.value;

    std::string l1_identifier =
      cur_state->top().level1.get_ident_name(identifier);

    const irep_idt &original_id =
      cur_state->top().level1.get_original_name(l1_identifier);

    // increase the frame if we have seen this declaration before
    while(cur_state->top().declaration_history.find(l1_identifier)!=
          cur_state->top().declaration_history.end())
    {
      unsigned index=cur_state->top().level1.current_names[original_id];
      cur_state->top().level1.rename(original_id, index+1);
      l1_identifier=cur_state->top().level1.get_ident_name(original_id);
    }

    cur_state->top().declaration_history.insert(l1_identifier);
    cur_state->top().local_variables.insert(l1_identifier);

    // seen it before?
    // it should get a fresh value
    renaming::level2t::current_namest::iterator it=
      cur_state->level2.current_names.find(l1_identifier);

    if(it!=cur_state->level2.current_names.end())
    {
      cur_state->level2.rename(l1_identifier, it->second.count+1);
      it->second.constant = expr2tc();
    }
  }
  else if (is_code_asm2t(code2))
  {
    // Assembly statement -> do nothing.
    return;
  }
  else
    throw "goto_symext: unexpected statement: " + get_expr_id(code2);
}

