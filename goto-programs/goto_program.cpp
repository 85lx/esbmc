/*******************************************************************\

Module: Program Transformation

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <langapi/language_util.h>

#include "goto_program.h"

/*******************************************************************\

Function: goto_programt::output_instruction

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::ostream& goto_programt::output_instruction(
  const class namespacet &ns,
  const irep_idt &identifier,
  std::ostream& out,
  instructionst::const_iterator it,
  bool show_location,
  bool show_variables) const
{
  if (show_location) {
  out << "        // " << it->location_number << " ";

  if(!it->location.is_nil())
    out << it->location.as_string();
  else
    out << "no location";

  out << "\n";
  }

  if(show_variables && !it->local_variables.empty())
  {
    out << "        // Variables:";
    for(local_variablest::const_iterator
        l_it=it->local_variables.begin();
        l_it!=it->local_variables.end();
        l_it++)
      out << " " << *l_it;

    out << std::endl;
  }

  if(!it->labels.empty())
  {
    out << "        // Labels:";
    for(instructiont::labelst::const_iterator
        l_it=it->labels.begin();
        l_it!=it->labels.end();
        l_it++)
    {
      out << " " << *l_it;
    }

    out << std::endl;
  }

  if(it->is_target())
    out << std::setw(6) << it->target_number << ": ";
  else
    out << "        ";

  switch(it->type)
  {
  case NO_INSTRUCTION_TYPE:
    out << "NO INSTRUCTION TYPE SET" << std::endl;
    break;

  case GOTO:
    if(!it->guard.is_true())
    {
      out << "IF "
          << from_expr(ns, identifier, it->guard)
          << " THEN ";
    }

    out << "GOTO ";

    for(instructiont::targetst::const_iterator
        gt_it=it->targets.begin();
        gt_it!=it->targets.end();
        gt_it++)
    {
      if(gt_it!=it->targets.begin()) out << ", ";
      out << (*gt_it)->target_number;
    }

    out << std::endl;
    break;

  case RETURN:
  case OTHER:
  case FUNCTION_CALL:
  case ASSIGN:
    out << from_expr(ns, identifier, it->code) << std::endl;
    break;

  case ASSUME:
  case ASSERT:
    if(it->is_assume())
      out << "ASSUME ";
    else
      out << "ASSERT ";

    {
      out << from_expr(ns, identifier, it->guard);

      const irep_idt &comment=it->location.comment();
      if(comment!="") out << " // " << comment;
    }

    out << std::endl;
    break;

  case SKIP:
    out << "SKIP" << std::endl;
    break;

  case END_FUNCTION:
    out << "END_FUNCTION" << std::endl;
    break;

  case LOCATION:
    out << "LOCATION" << std::endl;
    break;

  case THROW:
    out << "THROW";

    {
      const irept::subt &exception_list=
        it->code.find("exception_list").get_sub();

      for(irept::subt::const_iterator
          it=exception_list.begin();
          it!=exception_list.end();
          it++)
        out << " " << it->id();
    }

    if(it->code.operands().size()==1)
      out << ": " << from_expr(ns, identifier, it->code.op0());

    out << std::endl;
    break;

  case CATCH:
    out << "CATCH ";

    {
      unsigned i=0;
      const irept::subt &exception_list=
        it->code.find("exception_list").get_sub();
      assert(it->targets.size()==exception_list.size());

      for(instructiont::targetst::const_iterator
          gt_it=it->targets.begin();
          gt_it!=it->targets.end();
          gt_it++,
          i++)
      {
        if(gt_it!=it->targets.begin()) out << ", ";
        out << exception_list[i].id() << "->"
            << (*gt_it)->target_number;
      }
    }

    out << std::endl;
    break;

  case ATOMIC_BEGIN:
    out << "ATOMIC_BEGIN" << std::endl;
    break;

  case ATOMIC_END:
    out << "ATOMIC_END" << std::endl;
    break;

  case THROW_DECL:
    out << "THROW_DECL (";

    {
      const irept::subt &throw_list=
        it->code.find("throw_list").get_sub();

      for(unsigned int i=0; i<throw_list.size(); ++i)
      {
        if(i) out << ", ";
        out << throw_list[i].id();
      }
      out << ")";
    }

    out << std::endl;
    break;

  default:
    throw "unknown statement";
  }

  return out;
}

/*******************************************************************\

Function: operator<

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool operator<(const goto_programt::const_targett i1,
               const goto_programt::const_targett i2)
{
  const goto_programt::instructiont &_i1=*i1;
  const goto_programt::instructiont &_i2=*i2;
  return &_i1<&_i2;
}
