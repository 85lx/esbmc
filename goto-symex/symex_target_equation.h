/*******************************************************************\

Module: Generate Equation using Symbolic Execution

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_BASIC_SYMEX_EQUATION_H
#define CPROVER_BASIC_SYMEX_EQUATION_H

extern "C" {
#include <stdio.h>
}

#include <list>
#include <map>
#include <vector>

#include <namespace.h>

#include <config.h>
#include <goto-programs/goto_program.h>
#include <solvers/prop/prop_conv.h>

#include "symex_target.h"
#include "goto_trace.h"

extern "C" {
#include <stdint.h>
#include <string.h>
}

class symex_target_equationt:public symex_targett
{
public:
  symex_target_equationt(const namespacet &_ns):ns(_ns)
  {
    debug_print = config.options.get_bool_option("symex-ssa-trace");
  }

  // assignment to a variable - must be symbol
  // the value is destroyed
  virtual void assignment(
    const guardt &guard,
    const exprt &lhs,
    const exprt &original_lhs,
    exprt &rhs,
    const sourcet &source,
    std::vector<dstring> stack_trace,
    assignment_typet assignment_type);
    
  // output
  virtual void output(
    const guardt &guard,
    const sourcet &source,
    const std::string &fmt,
    const std::list<exprt> &args);
  
  // record an assumption
  // cond is destroyed
  virtual void assumption(
    const guardt &guard,
    exprt &cond,
    const sourcet &source);

  // record an assertion
  // cond is destroyed
  virtual void assertion(
    const guardt &guard,
    exprt &cond,
    const std::string &msg,
    std::vector<dstring> stack_trace,
    const sourcet &source);

  void convert(prop_convt &prop_conv);
  void convert_assignments(decision_proceduret &decision_procedure) const;
  void convert_assumptions(prop_convt &prop_conv);
  void convert_assertions(prop_convt &prop_conv);
  void convert_guards(prop_convt &prop_conv);
  void convert_output(decision_proceduret &decision_procedure);

  class SSA_stept
  {
  public:
    sourcet source;
    goto_trace_stept::typet type;

    // Vector of strings recording the stack state when this step was taken.
    // This can potentially be optimised to the point where there's only one
    // stack trace recorded per function activation record. Valid for assignment
    // and assert steps only. In reverse order (most recent in idx 0).
    std::vector<dstring> stack_trace;
    
    bool is_assert() const     { return type==goto_trace_stept::ASSERT; }
    bool is_assume() const     { return type==goto_trace_stept::ASSUME; }
    bool is_assignment() const { return type==goto_trace_stept::ASSIGNMENT; }
    bool is_output() const     { return type==goto_trace_stept::OUTPUT; }
    
    exprt guard;

    // for ASSIGNMENT  
    exprt lhs, rhs, original_lhs;
    assignment_typet assignment_type;
    
    // for ASSUME/ASSERT
    exprt cond; 
    std::string comment;

    // for OUTPUT
    std::string format_string;
    std::list<exprt> output_args;

    // for conversion
    literalt guard_literal, cond_literal;
    std::list<exprt> converted_output_args;
    
    // for slicing
    bool ignore;
    
    SSA_stept():
      guard(static_cast<const exprt &>(get_nil_irep())),
      cond(static_cast<const exprt &>(get_nil_irep())),
      ignore(false)
    {
    }

    void output(const namespacet &ns, std::ostream &out) const;
    void short_output(const namespacet &ns, std::ostream &out) const;
  };
  
  unsigned count_ignored_SSA_steps() const
  {
    unsigned i=0;
    for(SSA_stepst::const_iterator
        it=SSA_steps.begin();
        it!=SSA_steps.end(); it++)
      if(it->ignore) i++;
    return i;
  }

  typedef std::list<SSA_stept> SSA_stepst;
  SSA_stepst SSA_steps;

  SSA_stepst::iterator get_SSA_step(unsigned s)
  {
    SSA_stepst::iterator it=SSA_steps.begin();
    for(; s!=0; s--)
    {
      assert(it!=SSA_steps.end());
      it++;
    }
    return it;
  }

  void output(std::ostream &out) const;
  
  void clear()
  {
    SSA_steps.clear();
  }
  
  virtual symex_targett *clone(void) const
  {
    // No pointers or anything that requires ownership modification, can just
    // duplicate self.
    return new symex_target_equationt(*this);
  }


protected:
  const namespacet &ns;
  bool debug_print;
};

extern inline bool operator<(
  const symex_target_equationt::SSA_stepst::const_iterator a,
  const symex_target_equationt::SSA_stepst::const_iterator b)
{
  return &(*a)<&(*b);
}

std::ostream &operator<<(std::ostream &out, const symex_target_equationt::SSA_stept &step);
std::ostream &operator<<(std::ostream &out, const symex_target_equationt &equation);

#endif
