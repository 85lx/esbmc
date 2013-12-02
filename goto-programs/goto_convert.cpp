/*******************************************************************\

Module: Program Transformation

Author: Daniel Kroening, kroening@kroening.com
		Lucas Cordeiro, lcc08r@ecs.soton.ac.uk

\*******************************************************************/

#include <assert.h>

#include <i2string.h>
#include <cprover_prefix.h>
//#include <expr_util.h>
#include <prefix.h>
#include <std_expr.h>

#include <ansi-c/c_types.h>

#include "goto_convert_class.h"
#include "remove_skip.h"
#include "destructor.h"

#include <arith_tools.h>

//#define DEBUG

#ifdef DEBUG
#define DEBUGLOC std::cout << std::endl << __FUNCTION__ << \
                        "[" << __LINE__ << "]" << std::endl;
#else
#define DEBUGLOC
#endif

static void
link_up_type_names(irept &irep, const namespacet &ns)
{

  if (irep.find(exprt::i_type) != get_nil_irep()) {
    if (irep.find("type").id() == "symbol") {
      typet newtype = ns.follow((typet&)irep.find("type"));
      irep.add("type") = newtype;
    }
  }

  Forall_irep(it, irep.get_sub())
    link_up_type_names(*it, ns);
  Forall_named_irep(it, irep.get_named_sub())
    link_up_type_names(it->second, ns);

  return;
}

/*******************************************************************\

Function: goto_convertt::finish_gotos

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::finish_gotos()
{
  for(gotost::const_iterator it=targets.gotos.begin();
      it!=targets.gotos.end();
      it++)
  {
    goto_programt::instructiont &i=**it;

    if(i.code.statement()=="non-deterministic-goto")
    {
      assert(0 && "can't handle non-deterministic gotos");
      // jmorse - looks like this portion of code is related to the non-existant
      // nondeterministic goto. Nothing else in {es,c}bmc fiddles with
      // "destinations", and I'm busy fixing the type situation, so gets
      // disabled as it serves no purpose and is only getting in the way.
#if 0
      const irept &destinations=i.code.find("destinations");

      i.make_goto();

      forall_irep(it, destinations.get_sub())
      {
        labelst::const_iterator l_it=
          targets.labels.find(it->id_string());

        if(l_it==targets.labels.end())
        {
          err_location(i.code);
          str << "goto label " << it->id_string() << " not found";
          throw 0;
        }

        i.targets.push_back(l_it->second);
      }
#endif
    }
    else if(i.code.statement()=="goto")
    {
      const irep_idt &goto_label=i.code.destination();

      labelst::const_iterator l_it=targets.labels.find(goto_label);

      if(l_it==targets.labels.end())
      {
        err_location(i.code);
        str << "goto label " << goto_label << " not found";
        throw 0;
      }

      i.targets.clear();
      i.targets.push_back(l_it->second);
    }
    else
    {
      err_location(i.code);
      throw "finish_gotos: unexpected goto";
    }
  }

  targets.gotos.clear();
}

/*******************************************************************\

Function: goto_convertt::goto_convert

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::goto_convert(const codet &code, goto_programt &dest)
{
  goto_convert_rec(code, dest);
}

/*******************************************************************\

Function: goto_convertt::goto_convert_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::goto_convert_rec(
  const codet &code,
  goto_programt &dest)
{
  convert(code, dest);

  finish_gotos();
}

/*******************************************************************\

Function: goto_convertt::copy

  Inputs:

 Outputs:

 Purpose: Ben: copy code and make a new instruction of goto-functions

\*******************************************************************/

void goto_convertt::copy(
  const codet &code,
  goto_program_instruction_typet type,
  goto_programt &dest)
{
  goto_programt::targett t=dest.add_instruction(type);
  t->code=code;
  t->location=code.location();
}

/*******************************************************************\

Function: goto_convert::convert_label

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_label(
  const code_labelt &code,
  goto_programt &dest)
{
  if(code.operands().size()!=1)
  {
    err_location(code);
    throw "label statement expected to have one operand";
  }

  // grab the label
  const irep_idt &label=code.get_label();

  goto_programt tmp;

  convert(to_code(code.op0()), tmp);

  // magic ERROR label?

  const std::string &error_label=options.get_option("error-label");

  goto_programt::targett target;

  if(error_label!="" && label==error_label)
  {
    goto_programt::targett t=dest.add_instruction(ASSERT);
    t->guard.make_false();
    t->location=code.location();
    t->location.property("error label");
    t->location.comment("error label");
    t->location.user_provided(false);

    target=t;
    dest.destructive_append(tmp);
  }
  else
  {
    target=tmp.instructions.begin();
    dest.destructive_append(tmp);
  }

  if(!label.empty())
  {
    targets.labels.insert(std::pair<irep_idt, goto_programt::targett>
                          (label, target));
    target->labels.push_back(label);
  }

  // cases?

  const exprt::operandst &case_op=code.case_op();

  if(!case_op.empty())
  {
    exprt::operandst &case_op_dest=targets.cases[target];

    case_op_dest.reserve(case_op_dest.size()+case_op.size());

    forall_expr(it, case_op)
      case_op_dest.push_back(*it);
  }

  // default?

  if(code.is_default())
    targets.set_default(target);
}

/*******************************************************************\

Function: goto_convertt::convert

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert(
  const codet &code,
  goto_programt &dest)
{
  const irep_idt &statement=code.get_statement();

  link_up_type_names((codet&)code, ns);

  if(statement=="block")
    convert_block(code, dest);
  else if(statement=="decl")
    convert_decl(code, dest);
  else if(statement=="expression")
    convert_expression(code, dest);
  else if(statement=="assign")
    convert_assign(to_code_assign(code), dest);
  else if(statement=="init")
    convert_init(code, dest);
  else if(statement=="assert")
    convert_assert(code, dest);
  else if(statement=="assume")
    convert_assume(code, dest);
  else if(statement=="function_call")
    convert_function_call(to_code_function_call(code), dest);
  else if(statement=="label")
    convert_label(to_code_label(code), dest);
  else if(statement=="for")
    convert_for(code, dest);
  else if(statement=="while")
    convert_while(code, dest);
  else if(statement=="dowhile")
    convert_dowhile(code, dest);
  else if(statement=="switch")
    convert_switch(code, dest);
  else if(statement=="break")
    convert_break(to_code_break(code), dest);
  else if(statement=="return")
    convert_return(to_code_return(code), dest);
  else if(statement=="continue")
    convert_continue(to_code_continue(code), dest);
  else if(statement=="goto")
    convert_goto(code, dest);
  else if(statement=="skip")
    convert_skip(code, dest);
  else if(statement=="non-deterministic-goto")
    convert_non_deterministic_goto(code, dest);
  else if(statement=="ifthenelse")
    convert_ifthenelse(code, dest);
  else if(statement=="atomic_begin")
    convert_atomic_begin(code, dest);
  else if(statement=="atomic_end")
    convert_atomic_end(code, dest);
  else if(statement=="bp_enforce")
    convert_bp_enforce(code, dest);
  else if(statement=="bp_abortif")
    convert_bp_abortif(code, dest);
  else if(statement=="cpp_delete" ||
          statement=="cpp_delete[]")
    convert_cpp_delete(code, dest);
  else if(statement=="cpp-catch")
    convert_catch(code, dest);
  else if(statement=="throw_decl")
    convert_throw_decl(code, dest);
  else if(statement=="throw_decl_end")
    convert_throw_decl_end(code,dest);
  else
  {
    copy(code, OTHER, dest);
  }

  // if there is no instruction in the program, add skip to it
  if(dest.instructions.empty())
  {
    dest.add_instruction(SKIP);
    dest.instructions.back().code.make_nil();
  }
}

/*******************************************************************\

Function: goto_convertt::convert_throw_decl_end

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_throw_decl_end(const exprt &expr, goto_programt &dest)
{
  // add the THROW_DECL_END instruction to 'dest'
  goto_programt::targett throw_decl_end_instruction=dest.add_instruction();
  throw_decl_end_instruction->make_throw_decl_end();
  throw_decl_end_instruction->code.set_statement("throw_decl_end");
  throw_decl_end_instruction->location=expr.location();
}

/*******************************************************************\

Function: goto_convertt::convert_throw_decl

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_throw_decl(const exprt &expr, goto_programt &dest)
{
  // add the THROW_DECL instruction to 'dest'
  goto_programt::targett throw_decl_instruction=dest.add_instruction();
  throw_decl_instruction->make_throw_decl();
  throw_decl_instruction->code.set_statement("throw_decl");
  throw_decl_instruction->location=expr.location();

  // the THROW_DECL instruction is annotated with a list of IDs,
  // one per target
  irept::subt &throw_list=
    throw_decl_instruction->code.add("throw_list").get_sub();

  for(unsigned i=0; i<expr.operands().size(); i++)
  {
    const exprt &block=expr.operands()[i];
    irept type = irept(block.get("throw_decl_id"));

    // grab the ID and add to THROW_DECL instruction
    throw_list.push_back(irept(type));
  }
}

/*******************************************************************\

Function: goto_convertt::convert_catch

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_catch(
  const codet &code,
  goto_programt &dest)
{
  assert(code.operands().size()>=2);

  // add the CATCH-push instruction to 'dest'
  goto_programt::targett catch_push_instruction=dest.add_instruction();
  catch_push_instruction->make_catch();
  catch_push_instruction->code.set_statement("cpp-catch");
  catch_push_instruction->location=code.location();

  // the CATCH-push instruction is annotated with a list of IDs,
  // one per target
  irept::subt &exception_list=
    catch_push_instruction->code.add("exception_list").get_sub();

  // add a SKIP target for the end of everything
  goto_programt end;
  goto_programt::targett end_target=end.add_instruction();
  end_target->make_skip();

  // the first operand is the 'try' block
  goto_programt tmp;
  convert(to_code(code.op0()), tmp);
  dest.destructive_append(tmp);

  // add the CATCH-pop to the end of the 'try' block
  goto_programt::targett catch_pop_instruction=dest.add_instruction();
  catch_pop_instruction->make_catch();
  catch_pop_instruction->code.set_statement("cpp-catch");

  // add a goto to the end of the 'try' block
  dest.add_instruction()->make_goto(end_target);

  for(unsigned i=1; i<code.operands().size(); i++)
  {
    const codet &block=to_code(code.operands()[i]);

    // grab the ID and add to CATCH instruction
    exception_list.push_back(irept(block.get("exception_id")));

    // Hack for object value passing
    const_cast<exprt&>(block.op0()).operands().push_back(gen_zero(block.op0().op0().type()));

    convert(block, tmp);
    catch_push_instruction->targets.push_back(tmp.instructions.begin());
    dest.destructive_append(tmp);

    // add a goto to the end of the 'catch' block
    dest.add_instruction()->make_goto(end_target);
  }

  // add end-target
  dest.destructive_append(end);
}

/*******************************************************************\

Function: goto_convertt::convert_block

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_block(
  const codet &code,
  goto_programt &dest)
{
  bool last_for=is_for_block();
  bool last_while=is_while_block();

  if(inductive_step && code.add("inside_loop") != irept(""))
    set_for_block(true);

  std::list<irep_idt> locals;
  //extract all the local variables from the block

  forall_operands(it, code)
  {
    const codet &code=to_code(*it);

    if(code.get_statement()=="decl")
    {
      const exprt &op0=code.op0();
      assert(op0.id()=="symbol");
      const irep_idt &identifier=op0.identifier();
      const symbolt &symbol=lookup(identifier);

      if(!symbol.static_lifetime &&
         !symbol.type.is_code())
        locals.push_back(identifier);
    }

    goto_programt tmp;
    convert(code, tmp);

    // all the temp symbols are also local variables and they are gotten
    // via the convert process
    for(tmp_symbolst::const_iterator
        it=tmp_symbols.begin();
        it!=tmp_symbols.end();
        it++)
      locals.push_back(*it);

    tmp_symbols.clear();

    //add locals to instructions
    if(!locals.empty())
      Forall_goto_program_instructions(i_it, tmp)
        i_it->add_local_variables(locals);

    dest.destructive_append(tmp);
  }

  // see if we need to call any destructors

  while(!locals.empty())
  {
    const symbolt &symbol=ns.lookup(locals.back());

    code_function_callt destructor=get_destructor(ns, symbol.type);

    if(destructor.is_not_nil())
    {
      // add "this"
      exprt this_expr("address_of", pointer_typet());
      this_expr.type().subtype()=symbol.type;
      this_expr.copy_to_operands(symbol_expr(symbol));
      destructor.arguments().push_back(this_expr);

      goto_programt tmp;
      convert(destructor, tmp);

      Forall_goto_program_instructions(i_it, tmp)
        i_it->add_local_variables(locals);

      dest.destructive_append(tmp);
    }

    locals.pop_back();
  }

  if(inductive_step)
  {
    code.remove("inside_loop");
    set_for_block(last_for);
    set_while_block(last_while);
  }
}


/*******************************************************************\

Function: goto_convertt::convert_sideeffect

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_sideeffect(
  exprt &expr,
  goto_programt &dest)
{
  const irep_idt &statement=expr.statement();

  if(statement=="postincrement" ||
     statement=="postdecrement" ||
     statement=="preincrement" ||
     statement=="predecrement")
  {
    if(expr.operands().size()!=1)
    {
      err_location(expr);
      str << statement << " takes one argument";
      throw 0;
    }

    exprt rhs;

    if(statement=="postincrement" ||
       statement=="preincrement")
      rhs.id("+");
    else
      rhs.id("-");

    const typet &op_type=ns.follow(expr.op0().type());

    if(op_type.is_bool())
    {
      rhs.copy_to_operands(expr.op0(), gen_one(int_type()));
      rhs.op0().make_typecast(int_type());
      rhs.type()=int_type();
      rhs.make_typecast(typet("bool"));
    }
    else if(op_type.id()=="c_enum" ||
            op_type.id()=="incomplete_c_enum")
    {
      rhs.copy_to_operands(expr.op0(), gen_one(int_type()));
      rhs.op0().make_typecast(int_type());
      rhs.type()=int_type();
      rhs.make_typecast(op_type);
    }
    else
    {
      typet constant_type;

      if(op_type.id()=="pointer")
        constant_type=index_type();
      else if(is_number(op_type))
        constant_type=op_type;
      else
      {
        err_location(expr);
        throw "no constant one of type "+op_type.to_string();
      }

      exprt constant=gen_one(constant_type);

      rhs.copy_to_operands(expr.op0());
      rhs.move_to_operands(constant);
      rhs.type()=expr.op0().type();
    }

    codet assignment("assign");
    assignment.copy_to_operands(expr.op0());
    assignment.move_to_operands(rhs);

    assignment.location()=expr.find_location();

    convert(assignment, dest);
  }
  else if(statement=="assign")
  {
    exprt tmp;
    tmp.swap(expr);
    tmp.id("code");
    convert(to_code(tmp), dest);
  }
  else if(statement=="assign+" ||
          statement=="assign-" ||
          statement=="assign*" ||
          statement=="assign_div" ||
          statement=="assign_mod" ||
          statement=="assign_shl" ||
          statement=="assign_ashr" ||
          statement=="assign_lshr" ||
          statement=="assign_bitand" ||
          statement=="assign_bitxor" ||
          statement=="assign_bitor")
  {
    if(expr.operands().size()!=2)
    {
      err_location(expr);
      str << statement << " takes two arguments";
      throw 0;
    }

    exprt rhs;

    if(statement=="assign+")
      rhs.id("+");
    else if(statement=="assign-")
      rhs.id("-");
    else if(statement=="assign*")
      rhs.id("*");
    else if(statement=="assign_div")
      rhs.id("/");
    else if(statement=="assign_mod")
      rhs.id("mod");
    else if(statement=="assign_shl")
      rhs.id("shl");
    else if(statement=="assign_ashr")
      rhs.id("ashr");
    else if(statement=="assign_lshr")
      rhs.id("lshr");
    else if(statement=="assign_bitand")
      rhs.id("bitand");
    else if(statement=="assign_bitxor")
      rhs.id("bitxor");
    else if(statement=="assign_bitor")
      rhs.id("bitor");
    else
    {
      err_location(expr);
      str << statement << " not yet supproted";
      throw 0;
    }

    rhs.copy_to_operands(expr.op0(), expr.op1());
    rhs.type()=expr.op0().type();

    if(rhs.op0().type().is_bool())
    {
      rhs.op0().make_typecast(int_type());
      rhs.op1().make_typecast(int_type());
      rhs.type()=int_type();
      rhs.make_typecast(typet("bool"));
    }

    exprt lhs(expr.op0());

    code_assignt assignment(lhs, rhs);
    assignment.location()=expr.location();

    convert(assignment, dest);
  }
  else if(statement=="cpp_delete" ||
          statement=="cpp_delete[]")
  {
    exprt tmp;
    tmp.swap(expr);
    tmp.id("code");
    convert(to_code(tmp), dest);
  }
  else if(statement=="function_call")
  {
    if(expr.operands().size()!=2)
    {
      err_location(expr);
      str << "function_call sideeffect takes two arguments, but got "
          << expr.operands().size();
      throw 0;
    }

    code_function_callt function_call;
    function_call.location()=expr.location();
    function_call.function()=expr.op0();
    function_call.arguments()=expr.op1().operands();
    convert_function_call(function_call, dest);
  }
  else if(statement=="statement_expression")
  {
    if(expr.operands().size()!=1)
    {
      err_location(expr);
      str << "statement_expression sideeffect takes one argument";
      throw 0;
    }

    convert(to_code(expr.op0()), dest);
  }
  else if(statement=="gcc_conditional_expression")
  {
    remove_sideeffects(expr, dest, false);
  }
  else if(statement=="temporary_object")
  {
    remove_sideeffects(expr, dest, false);
  }
  else if(statement=="cpp-throw")
  {
    goto_programt::targett t=dest.add_instruction(THROW);
    t->code=codet("cpp-throw");
    t->code.operands().swap(expr.operands());
    t->code.location()=expr.location();
    t->location=expr.location();
    t->code.set("exception_list", expr.find("exception_list"));

    // the result can't be used, these are void
    expr.make_nil();
  }
  else
  {
    err_location(expr);
    str << "sideeffect " << statement << " not supported";
    throw 0;
  }
}

/*******************************************************************\

Function: goto_convertt::convert_expression

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_expression(
  const codet &code,
  goto_programt &dest)
{
  if(code.operands().size()!=1)
  {
    err_location(code);
    throw "expression statement takes one operand";
  }

  exprt expr=code.op0();

  if(expr.id()=="sideeffect")
  {
    Forall_operands(it, expr)
      remove_sideeffects(*it, dest);

    goto_programt tmp;
    convert_sideeffect(expr, tmp);
    dest.destructive_append(tmp);
  }
  else
  {
    remove_sideeffects(expr, dest, false); // result not used

    if(expr.is_not_nil())
    {
      codet tmp(code);
      tmp.op0()=expr;
      copy(tmp, OTHER, dest);
    }
  }
}

/*******************************************************************\

Function: goto_convertt::is_expr_in_state

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool goto_convertt::is_expr_in_state(
  const exprt &expr,
  const struct_typet &str)
{
  const struct_typet &struct_type = to_struct_type(str);
  const struct_typet::componentst &components = struct_type.components();

  for (struct_typet::componentst::const_iterator
     it = components.begin();
     it != components.end();
     it++)
  {
    if (it->get("name").compare(expr.get_string("identifier")) == 0)
   	  return true;
  }

  return false;
}

/*******************************************************************\

Function: goto_convertt::get_struct_components

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::get_struct_components(const exprt &exp)
{
  DEBUGLOC;
  if (exp.is_symbol() && exp.type().id()!="code")
  {
    std::size_t found = exp.identifier().as_string().find("__ESBMC_");
    if(found != std::string::npos)
      return;

    if(exp.identifier().as_string() == "c::__func__"
       || exp.identifier().as_string() == "c::__PRETTY_FUNCTION__"
       || exp.identifier().as_string() == "c::pthread_lib::num_total_threads"
       || exp.identifier().as_string() == "c::pthread_lib::num_threads_running")
      return;

    if(exp.location().file().as_string() == "<built-in>"
       || exp.cmt_location().file().as_string() == "<built-in>"
       || exp.type().location().file().as_string() == "<built-in>"
       || exp.type().cmt_location().file().as_string() == "<built-in>")
      return;

    if (is_for_block() || is_while_block())
      loop_vars.insert(std::pair<exprt,struct_typet>(exp,state));

    if (!is_expr_in_state(exp, state))
    {
      unsigned int size = state.components().size();
      state.components().resize(size+1);
      state.components()[size] = (struct_typet::componentt &) exp;
      state.components()[size].set_name(exp.get_string("identifier"));
      state.components()[size].pretty_name(exp.get_string("identifier"));
    }
  }
  else if (exp.operands().size()==1)
  {
    DEBUGLOC;
    if (exp.op0().is_symbol()) {
      get_struct_components(exp.op0());
    } else if (exp.op0().operands().size()==1)
      get_struct_components(exp.op0().op0());
  }
  else if (exp.operands().size()==2)
  {
    DEBUGLOC;
    if (exp.op0().is_symbol()) {
      get_struct_components(exp.op0());
    } else if (exp.op0().operands().size())
      get_struct_components(exp.op0().op0());
  }
  else
  {
    forall_operands(it, exp)
    {
      DEBUGLOC;
      get_struct_components(*it);
    }
  }
  DEBUGLOC;
}

/*******************************************************************\

Function: goto_convertt::convert_decl

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_decl(
  const codet &code,
  goto_programt &dest)
{
  if(code.operands().size()!=1 &&
     code.operands().size()!=2)
  {
    err_location(code);
    throw "decl statement takes one or two operands";
  }

  const exprt &op0=code.op0();

  if(op0.id()!="symbol")
  {
    err_location(op0);
    throw "decl statement expects symbol as first operand";
  }

  if (inductive_step)
    get_struct_components(op0);

  const irep_idt &identifier=op0.identifier();

  const symbolt &symbol=lookup(identifier);
  if(symbol.static_lifetime ||
     symbol.type.is_code())
	  return; // this is a SKIP!

  if(code.operands().size()==1)
  {
    copy(code, OTHER, dest);
  }
  else
  {
    exprt initializer;
    codet tmp(code);
    initializer=code.op1();
    tmp.operands().resize(1); // just resize the vector, this will get rid of op1

    goto_programt sideeffects;

    if(options.get_bool_option("atomicity-check"))
    {
      unsigned int globals = get_expr_number_globals(initializer);
      if(globals > 0)
        break_globals2assignments(initializer, dest,code.location());
    }

    if(initializer.is_typecast())
    {
      if(initializer.get("cast")=="dynamic")
      {
        exprt op0 = initializer.op0();
        initializer.swap(op0);

        if(!code.op1().is_empty())
        {
          exprt function = code.op1();
          // We must check if the is a exception list
          // If there is, we must throw the exception
          if (function.has_operands())
          {
            if (function.op0().has_operands())
            {
              const exprt& exception_list=
                  static_cast<const exprt&>(function.op0().op0().find("exception_list"));

              if(exception_list.is_not_nil())
              {
                // Let's create an instruction for bad_cast

                // Add new instruction throw
                goto_programt::targett t=dest.add_instruction(THROW);
                t->code=codet("cpp-throw");
                t->location=function.location();
                t->code.set("exception_list", exception_list);
              }
            }
          }
          else
          {
            remove_sideeffects(initializer, dest);
            dest.output(std::cout);
          }

          // break up into decl and assignment
          copy(tmp, OTHER, dest);
          code_assignt assign(code.op0(), initializer); // initializer is without sideeffect now
          assign.location()=tmp.location();
          copy(assign, ASSIGN, dest);
          return;
        }
      }
    }
    remove_sideeffects(initializer, sideeffects);
    dest.destructive_append(sideeffects);
    // break up into decl and assignment
    copy(tmp, OTHER, dest);

		if (initializer.is_symbol())
      nondet_vars.insert(std::pair<exprt,exprt>(code.op0(),initializer));

    code_assignt assign(code.op0(), initializer); // initializer is without sideeffect now
    assign.location()=tmp.location();
    copy(assign, ASSIGN, dest);
  }
}

/*******************************************************************\

Function: goto_convertt::convert_assign

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_assign(
  const code_assignt &code,
  goto_programt &dest)
{
  if(code.operands().size()!=2)
  {
    err_location(code);
    throw "assignment statement takes two operands";
  }

  exprt lhs=code.lhs(),
        rhs=code.rhs();

  remove_sideeffects(lhs, dest);

  if(rhs.id()=="sideeffect" &&
     rhs.statement()=="function_call")
  {
    if(rhs.operands().size()!=2)
    {
      err_location(rhs);
      throw "function_call sideeffect takes two operands";
    }

    Forall_operands(it, rhs)
      remove_sideeffects(*it, dest);

    do_function_call(lhs, rhs.op0(), rhs.op1().operands(), dest);
  }
  else if(rhs.id()=="sideeffect" &&
          (rhs.statement()=="cpp_new" ||
           rhs.statement()=="cpp_new[]"))
  {
    Forall_operands(it, rhs)
      remove_sideeffects(*it, dest);

    do_cpp_new(lhs, rhs, dest);
  }
  else
  {
    remove_sideeffects(rhs, dest);

    if (rhs.type().is_code())
    {
      convert(to_code(rhs), dest);
      return;
    }

    if(lhs.id()=="typecast")
    {
      assert(lhs.operands().size()==1);

      // move to rhs
      exprt tmp_rhs(lhs);
      tmp_rhs.op0()=rhs;
      rhs=tmp_rhs;

      // remove from lhs
      exprt tmp(lhs.op0());
      lhs.swap(tmp);
    }


    int atomic = 0;
    if(options.get_bool_option("atomicity-check"))
    {
      unsigned int globals = get_expr_number_globals(lhs);
      atomic = globals;
      globals += get_expr_number_globals(rhs);
      if(globals > 0 && (lhs.identifier().as_string().find("tmp$") == std::string::npos))
        break_globals2assignments(atomic,lhs,rhs, dest,code.location());
    }

    code_assignt new_assign(code);
    new_assign.lhs()=lhs;
    new_assign.rhs()=rhs;
    copy(new_assign, ASSIGN, dest);

    if(options.get_bool_option("atomicity-check"))
      if(atomic == -1)
        dest.add_instruction(ATOMIC_END);
  }

  if (inductive_step) {
    get_struct_components(lhs);
    if (rhs.is_constant() && is_ifthenelse_block()) {
      nondet_vars.insert(std::pair<exprt,exprt>(lhs,rhs));
    }
    else if ((is_for_block() || is_while_block()) && is_ifthenelse_block()) {
      nondet_varst::const_iterator cache_result;
      cache_result = nondet_vars.find(lhs);
      if (cache_result == nondet_vars.end())
        init_nondet_expr(lhs, dest);
    }
  }
}

/*******************************************************************\

Function: goto_convertt::break_globals2assignments

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::break_globals2assignments(int & atomic,exprt &lhs, exprt &rhs, goto_programt &dest, const locationt &location)
{

  if(!options.get_bool_option("atomicity-check"))
    return;

  exprt atomic_dest = exprt("and", typet("bool"));

  /* break statements such as a = b + c as follows:
   * tmp1 = b;
   * tmp2 = c;
   * atomic_begin
   * assert tmp1==b && tmp2==c
   * a = b + c
   * atomic_end
  */
  //break_globals2assignments_rec(lhs,atomic_dest,dest,atomic,location);
  break_globals2assignments_rec(rhs,atomic_dest,dest,atomic,location);

  if(atomic_dest.operands().size()==1)
  {
    exprt tmp;
    tmp.swap(atomic_dest.op0());
    atomic_dest.swap(tmp);
  }
  if(atomic_dest.operands().size() != 0)
  {
	// do an assert
	if(atomic > 0)
	{
	  dest.add_instruction(ATOMIC_BEGIN);
	  atomic = -1;
	}
	goto_programt::targett t=dest.add_instruction(ASSERT);
	t->guard.swap(atomic_dest);
	t->location=location;
	  t->location.comment("atomicity violation on assignment to " + lhs.identifier().as_string());
  }
}

/*******************************************************************\

Function: goto_convertt::break_globals2assignments

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::break_globals2assignments(exprt & rhs, goto_programt & dest, const locationt & location)
{

  if(!options.get_bool_option("atomicity-check"))
    return;

  if (rhs.operands().size()>0)
    if (rhs.op0().identifier().as_string().find("pthread") != std::string::npos)
 	  return ;

  if (rhs.operands().size()>0)
    if (rhs.op0().operands().size()>0)
 	  return ;

  exprt atomic_dest = exprt("and", typet("bool"));
  break_globals2assignments_rec(rhs,atomic_dest,dest,0,location);

  if(atomic_dest.operands().size()==1)
  {
    exprt tmp;
    tmp.swap(atomic_dest.op0());
    atomic_dest.swap(tmp);
  }

  if(atomic_dest.operands().size() != 0)
  {
    goto_programt::targett t=dest.add_instruction(ASSERT);
	t->guard.swap(atomic_dest);
	t->location=location;
    t->location.comment("atomicity violation");
  }
}

/*******************************************************************\

Function: goto_convertt::break_globals2assignments_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::break_globals2assignments_rec(exprt &rhs, exprt &atomic_dest, goto_programt &dest, int atomic, const locationt &location)
{

  if(!options.get_bool_option("atomicity-check"))
    return;

  if(rhs.id() == "dereference"
	|| rhs.id() == "implicit_dereference"
	|| rhs.id() == "index"
	|| rhs.id() == "member")
  {
    irep_idt identifier=rhs.op0().identifier();
    if (rhs.id() == "member")
    {
      const exprt &object=rhs.operands()[0];
      identifier=object.identifier();
    }
    else if (rhs.id() == "index")
    {
      identifier=rhs.op1().identifier();
    }

    if (identifier.empty())
	  return;

	const symbolt &symbol=lookup(identifier);

    if (!(identifier == "c::__ESBMC_alloc" || identifier == "c::__ESBMC_alloc_size")
          && (symbol.static_lifetime || symbol.type.is_dynamic_set()))
    {
	  // make new assignment to temp for each global symbol
	  symbolt &new_symbol=new_tmp_symbol(rhs.type());
	  equality_exprt eq_expr;
	  irept irep;
	  new_symbol.to_irep(irep);
	  eq_expr.lhs()=symbol_expr(new_symbol);
	  eq_expr.rhs()=rhs;
	  atomic_dest.copy_to_operands(eq_expr);

	  codet assignment("assign");
	  assignment.reserve_operands(2);
	  assignment.copy_to_operands(symbol_expr(new_symbol));
	  assignment.copy_to_operands(rhs);
	  assignment.location() = location;
	  assignment.comment("atomicity violation");
	  copy(assignment, ASSIGN, dest);

	  if(atomic == 0)
	    rhs=symbol_expr(new_symbol);

    }
  }
  else if(rhs.id() == "symbol")
  {
	const irep_idt &identifier=rhs.identifier();
	const symbolt &symbol=lookup(identifier);
	if(symbol.static_lifetime || symbol.type.is_dynamic_set())
	{
	  // make new assignment to temp for each global symbol
	  symbolt &new_symbol=new_tmp_symbol(rhs.type());
	  new_symbol.static_lifetime=true;
	  equality_exprt eq_expr;
	  irept irep;
	  new_symbol.to_irep(irep);
	  eq_expr.lhs()=symbol_expr(new_symbol);
	  eq_expr.rhs()=rhs;
	  atomic_dest.copy_to_operands(eq_expr);

	  codet assignment("assign");
	  assignment.reserve_operands(2);
	  assignment.copy_to_operands(symbol_expr(new_symbol));
	  assignment.copy_to_operands(rhs);

	  assignment.location() = rhs.find_location();
	  assignment.comment("atomicity violation");
	  copy(assignment, ASSIGN, dest);

	  if(atomic == 0)
	    rhs=symbol_expr(new_symbol);
    }
  }
  else if(!rhs.is_address_of())// && rhs.id() != "dereference")
  {
    Forall_operands(it, rhs)
	{
	  break_globals2assignments_rec(*it,atomic_dest,dest,atomic,location);
	}
  }
}

/*******************************************************************\

Function: goto_convertt::get_expr_number_globals

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

unsigned int goto_convertt::get_expr_number_globals(const exprt &expr)
{
  if(!options.get_bool_option("atomicity-check"))
	return 0;

  if(expr.is_address_of())
  	return 0;

  else if(expr.id() == "symbol")
  {
    const irep_idt &identifier=expr.identifier();
  	const symbolt &symbol=lookup(identifier);

    if (identifier == "c::__ESBMC_alloc"
    	|| identifier == "c::__ESBMC_alloc_size")
    {
      return 0;
    }
    else if (symbol.static_lifetime || symbol.type.is_dynamic_set())
    {
      return 1;
    }
  	else
  	{
  	  return 0;
  	}
  }

  unsigned int globals = 0;

  forall_operands(it, expr)
    globals += get_expr_number_globals(*it);

  return globals;
}

/*******************************************************************\

Function: goto_convertt::convert_init

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_init(
  const codet &code,
  goto_programt &dest)
{
  if(code.operands().size()!=2)
  {
    err_location(code);
    throw "init statement takes two operands";
  }

  // make it an assignment
  codet assignment=code;
  assignment.set_statement("assign");

  convert(to_code_assign(assignment), dest);
}

/*******************************************************************\

Function: goto_convertt::convert_cpp_delete

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_cpp_delete(
  const codet &code,
  goto_programt &dest)
{
  if(code.operands().size()!=1)
  {
    err_location(code);
    throw "cpp_delete statement takes one operand";
  }

  exprt tmp_op=code.op0();

  // we call the destructor, and then free
  const exprt &destructor=
    static_cast<const exprt &>(code.find("destructor"));

  if(destructor.is_not_nil())
  {
    if(code.statement()=="cpp_delete[]")
    {
      // build loop
    }
    else if(code.statement()=="cpp_delete")
    {
      exprt deref_op("dereference", tmp_op.type().subtype());
      deref_op.copy_to_operands(tmp_op);

      codet tmp_code=to_code(destructor);
      replace_new_object(deref_op, tmp_code);
      convert(tmp_code, dest);
    }
    else
      assert(0);
  }

  // preserve the call
  codet delete_statement("cpp_delete");
  delete_statement.location()=code.location();
  delete_statement.copy_to_operands(tmp_op);

  goto_programt::targett t_f=dest.add_instruction(OTHER);
  t_f->code=delete_statement;
  t_f->location=code.location();

  // now do "delete"
  exprt valid_expr("valid_object", bool_typet());
  valid_expr.copy_to_operands(tmp_op);

  // clear alloc bit
  goto_programt::targett t_c=dest.add_instruction(ASSIGN);
  t_c->code=code_assignt(valid_expr, false_exprt());
  t_c->location=code.location();

  exprt deallocated_expr("deallocated_object", bool_typet());
  deallocated_expr.copy_to_operands(tmp_op);

  //indicate that memory has been deallocated
  goto_programt::targett t_d=dest.add_instruction(ASSIGN);
  t_d->code=code_assignt(deallocated_expr, true_exprt());
  t_d->location=code.location();
}

/*******************************************************************\

Function: goto_convertt::convert_assert

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_assert(
  const codet &code,
  goto_programt &dest)
{

  if(code.operands().size()!=1)
  {
    err_location(code);
    throw "assert statement takes one operand";
  }

  exprt cond=code.op0();

  remove_sideeffects(cond, dest);

  if(options.get_bool_option("no-assertions"))
    return;

  if(options.get_bool_option("atomicity-check"))
  {
    unsigned int globals = get_expr_number_globals(cond);
    if(globals > 0)
	  break_globals2assignments(cond, dest,code.location());
  }

  goto_programt::targett t=dest.add_instruction(ASSERT);
  t->guard.swap(cond);
  t->location=code.location();
  t->location.property("assertion");
  t->location.user_provided(true);
}

/*******************************************************************\

Function: goto_convertt::convert_skip

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_skip(
  const codet &code,
  goto_programt &dest)
{
  goto_programt::targett t=dest.add_instruction(SKIP);
  t->location=code.location();
  t->code=code;
}

/*******************************************************************\

Function: goto_convertt::convert_assert

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_assume(
  const codet &code,
  goto_programt &dest)
{
  if(code.operands().size()!=1)
  {
    err_location(code);
    throw "assume statement takes one operand";
  }

  exprt op=code.op0();

  remove_sideeffects(op, dest);

  if(options.get_bool_option("atomicity-check"))
  {
    unsigned int globals = get_expr_number_globals(op);
    if(globals > 0)
	  break_globals2assignments(op, dest,code.location());
  }

  goto_programt::targett t=dest.add_instruction(ASSUME);
  t->guard.swap(op);
  t->location=code.location();
}

/*******************************************************************\

Function: goto_convertt::update_state_vector

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::update_state_vector(
  array_typet state_vector,
  goto_programt &dest)
{
  //set the type of the state vector
  state_vector.subtype() = state;

  std::string identifier;
  identifier = "kindice$"+i2string(state_counter);

  exprt lhs_index = symbol_exprt(identifier, int_type());
  exprt new_expr(exprt::with, state_vector);
  exprt lhs_array("symbol", state_vector);
  exprt rhs("symbol", state);

  std::string identifier_lhs, identifier_rhs;
  identifier_lhs = "s$"+i2string(state_counter);
  identifier_rhs = "cs$"+i2string(state_counter);

  lhs_array.identifier(identifier_lhs);
  rhs.identifier(identifier_rhs);

  //s[k]=cs
  new_expr.reserve_operands(3);
  new_expr.copy_to_operands(lhs_array);
  new_expr.copy_to_operands(lhs_index);
  new_expr.move_to_operands(rhs);
  code_assignt new_assign(lhs_array,new_expr);
  copy(new_assign, ASSIGN, dest);
}

/*******************************************************************\

Function: goto_convertt::assume_state_vector

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::assume_state_vector(
  array_typet state_vector,
  goto_programt &dest)
{
  //set the type of the state vector
  state_vector.subtype() = state;

  std::string identifier;
  identifier = "kindice$"+i2string(state_counter);

  exprt lhs_index = symbol_exprt(identifier, int_type());
  exprt new_expr(exprt::index, state);
  exprt lhs_array("symbol", state_vector);
  exprt rhs("symbol", state);

  std::string identifier_lhs, identifier_rhs;

  identifier_lhs = "s$"+i2string(state_counter);
  identifier_rhs = "cs$"+i2string(state_counter);

  lhs_array.identifier(identifier_lhs);
  rhs.identifier(identifier_rhs);

  //s[k]
  new_expr.reserve_operands(2);
  new_expr.copy_to_operands(lhs_array);
  new_expr.copy_to_operands(lhs_index);

  //assume(s[k]!=cs)
  exprt result_expr = gen_binary(exprt::notequal, bool_typet(), new_expr, rhs);
  assume_cond(result_expr, false, dest);

  //kindice=kindice+1
  exprt one_expr = gen_one(int_type());
  exprt rhs_expr = gen_binary(exprt::plus, int_type(), lhs_index, one_expr);
  code_assignt new_assign_plus(lhs_index,rhs_expr);
  copy(new_assign_plus, ASSIGN, dest);
}

/*******************************************************************\

Function: goto_convertt::convert_for

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_for(
  const codet &code,
  goto_programt &dest)
{
  DEBUGLOC;

  set_for_block(true);

  if(code.operands().size()!=4)
  {
    err_location(code);
    throw "for takes four operands";
  }

  // turn for(A; c; B) { P } into
  //  A; while(c) { P; B; }
  //-----------------------------
  //    A;
  // t: cs=nondet
  // u: sideeffects in c
  // c: k=0
  // v: if(!c) goto z;
  // f: s[k]=cs
  // w: P;
  // x: B;               <-- continue target
  // d: cs assign
  // e: assume
  // y: goto u;
  // z: ;                <-- break target
  // g: assume(!c)

  // A;
  code_blockt block;
  if(code.op0().is_not_nil())
  {
    block.copy_to_operands(code.op0());
    convert(block, dest);
  }

  exprt tmp=code.op1();

  if (inductive_step)
    replace_cond(tmp, dest);

  exprt cond=tmp;

  array_typet state_vector;

  // do the t label
  if(inductive_step)
  {
    //assert(cond.operands().size()==2);
    get_struct_components(cond);
    get_struct_components(code.op3());
    make_nondet_assign(dest);
  }

  goto_programt sideeffects;

  remove_sideeffects(cond, sideeffects);

  //unsigned int globals = get_expr_number_globals(cond);

  //if(globals > 0)
	//break_globals2assignments(cond, dest,code.location());

  // save break/continue targets
  break_continue_targetst old_targets(targets);

  // do the u label
  goto_programt::targett u=sideeffects.instructions.begin();

  // do the c label
  if (inductive_step)
    init_k_indice(dest);

  // do the v label
  goto_programt tmp_v;
  goto_programt::targett v=tmp_v.add_instruction();

  // do the z label
  goto_programt tmp_z;
  goto_programt::targett z=tmp_z.add_instruction(SKIP);

  // do the x label
  goto_programt tmp_x;
  if(code.op2().is_nil())
    tmp_x.add_instruction(SKIP);
  else
  {
    exprt tmp_B=code.op2();
    //clean_expr(tmp_B, tmp_x, false);
    remove_sideeffects(tmp_B, tmp_x, false);
    if(tmp_x.instructions.empty())
    {
      convert(to_code(code.op2()), tmp_x);
    }
  }

  // optimize the v label
  if(sideeffects.instructions.empty())
    u=v;

  // set the targets
  targets.set_break(z);
  targets.set_continue(tmp_x.instructions.begin());

  // v: if(!c) goto z;
  v->make_goto(z);
  v->guard=cond;
  v->guard.make_not();
  v->location=cond.location();

  // do the w label
  goto_programt tmp_w;
  convert(to_code(code.op3()), tmp_w);

  // y: goto u;
  goto_programt tmp_y;
  goto_programt::targett y=tmp_y.add_instruction();
  y->make_goto(u);
  y->guard.make_true();
  y->location=code.location();

  dest.destructive_append(sideeffects);
  dest.destructive_append(tmp_v);

  // do the f label
  if (inductive_step)
    update_state_vector(state_vector, dest);

  dest.destructive_append(tmp_w);

  if (inductive_step)
    increment_var(code.op1(), dest);

  dest.destructive_append(tmp_x);

  // do the d label
  if (inductive_step)
    assign_current_state(dest);

  // do the e label
  if (inductive_step)
    assume_state_vector(state_vector, dest);

  dest.destructive_append(tmp_y);
  dest.destructive_append(tmp_z);

  //do the g label
  if (!is_break() && !is_goto()
			&& inductive_step)
    assume_cond(cond, true, dest); //assume(!c)
  else if (k_induction)
    assert_cond(cond, true, dest); //assert(!c)

  // restore break/continue
  targets.restore(old_targets);
  set_for_block(false);
  state_counter++;
}

/*******************************************************************\

Function: goto_convertt::make_nondet_assign

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::make_nondet_assign(
  goto_programt &dest)
{
  u_int j=0;
  for (j=0; j < state.components().size(); j++)
  {
    exprt rhs_expr=side_effect_expr_nondett(state.components()[j].type());
    exprt new_expr(exprt::with, state);
    exprt lhs_expr("symbol", state);

    if (state.components()[j].type().is_array())
    {
      rhs_expr=exprt("array_of", state.components()[j].type());
      exprt value=side_effect_expr_nondett(state.components()[j].type().subtype());
      rhs_expr.move_to_operands(value);
    }

    std::string identifier;
    identifier = "cs$"+i2string(state_counter);
    lhs_expr.identifier(identifier);

    new_expr.reserve_operands(3);
    new_expr.copy_to_operands(lhs_expr);
    new_expr.copy_to_operands(exprt("member_name"));
    new_expr.move_to_operands(rhs_expr);

    if (!state.components()[j].operands().size())
    {
      new_expr.op1().component_name(state.components()[j].get_string("identifier"));
      assert(!new_expr.op1().get_string("component_name").empty());
    }
    else
    {
      forall_operands(it, state.components()[j])
      {
        new_expr.op1().component_name(it->get_string("identifier"));
        assert(!new_expr.op1().get_string("component_name").empty());
      }
    }
    code_assignt new_assign(lhs_expr,new_expr);
    copy(new_assign, ASSIGN, dest);
  }
}

/*******************************************************************\

Function: goto_convertt::assign_current_state

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::assign_current_state(
  goto_programt &dest)
{
  u_int j=0;
  for (j=0; j < state.components().size(); j++)
  {
    exprt rhs_expr(state.components()[j]);
    exprt new_expr(exprt::with, state);
    exprt lhs_expr("symbol", state);

    std::string identifier;

    identifier = "cs$"+i2string(state_counter);

    lhs_expr.identifier(identifier);

    new_expr.reserve_operands(3);
    new_expr.copy_to_operands(lhs_expr);
    new_expr.copy_to_operands(exprt("member_name"));
    new_expr.move_to_operands(rhs_expr);

    if (!state.components()[j].operands().size())
    {
      new_expr.op1().component_name(state.components()[j].get_string("identifier"));
      assert(!new_expr.op1().get_string("component_name").empty());
    }
    else
    {
      forall_operands(it, state.components()[j])
      {
        new_expr.op1().component_name(it->get_string("identifier"));
        assert(!new_expr.op1().get_string("component_name").empty());
      }
    }

    code_assignt new_assign(lhs_expr,new_expr);
    copy(new_assign, ASSIGN, dest);
  }
}

/*******************************************************************\

Function: goto_convertt::init_k_indice

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::init_k_indice(
  goto_programt &dest)
{
  std::string identifier;
  identifier = "kindice$"+i2string(state_counter);
  exprt lhs_index = symbol_exprt(identifier, int_type());
  exprt zero_expr = gen_zero(int_type());
  code_assignt new_assign(lhs_index,zero_expr);
  copy(new_assign, ASSIGN, dest);
}

/*******************************************************************\

Function: goto_convertt::assign_state_vector

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::assign_state_vector(
  const array_typet &state_vector,
  goto_programt &dest)
{
    //set the type of the state vector
    const_cast<typet&>(state_vector.subtype()) = state;

    std::string identifier;
    identifier = "kindice$"+i2string(state_counter);

    exprt lhs_index = symbol_exprt(identifier, int_type());
    exprt new_expr(exprt::with, state_vector);
    exprt lhs_array("symbol", state_vector);
    exprt rhs("symbol", state);

    std::string identifier_lhs, identifier_rhs;
    identifier_lhs = "s$"+i2string(state_counter);
    identifier_rhs = "cs$"+i2string(state_counter);

    lhs_array.identifier(identifier_lhs);
    rhs.identifier(identifier_rhs);

    // s[k]=cs
    new_expr.reserve_operands(3);
    new_expr.copy_to_operands(lhs_array);
    new_expr.copy_to_operands(lhs_index);
    new_expr.move_to_operands(rhs);

    code_assignt new_assign(lhs_array,new_expr);
    copy(new_assign, ASSIGN, dest);
}


/*******************************************************************\

Function: goto_convertt::assume_cond

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::assume_cond(
  const exprt &cond,
  const bool &neg,
  goto_programt &dest)
{
  goto_programt tmp_e;
  goto_programt::targett e=tmp_e.add_instruction(ASSUME);
  exprt result_expr = cond;
  if (neg)
    result_expr.make_not();
  e->guard.swap(result_expr);
  dest.destructive_append(tmp_e);
}

/*******************************************************************\

Function: goto_convertt::assert_cond

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::assert_cond(
  const exprt &cond,
  const bool &neg,
  goto_programt &dest)
{
  goto_programt tmp_e;
  goto_programt::targett e=tmp_e.add_instruction(ASSERT);
  exprt result_expr = cond;
  if (neg)
    result_expr.make_not();
  e->guard.swap(result_expr);
  dest.destructive_append(tmp_e);
}

/*******************************************************************\

Function: goto_convertt::disable_k_induction

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::disable_k_induction()
{
  k_induction=1;
  inductive_step=0;
  base_case=0;
}


/*******************************************************************\

Function: goto_convertt::print_msg_mem_alloc

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::print_msg_mem_alloc(void)
{
  std::cerr << "warning: this program contains dynamic memory allocation,"
            << " so we are not applying the inductive step to this program!"
            << std::endl;
  std::cout << "failed" << std::endl;
  disable_k_induction();
}

/*******************************************************************\

Function: goto_convertt::print_msg

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::print_msg(
  const exprt &tmp)
{
  std::cerr << "warning: this program " << tmp.location().get_file()
            << " contains a '" << tmp.id() << "' operator at line "
            << tmp.location().get_line()
            << ", so we are not applying the k-induction method to this program!"
            << std::endl;
  disable_k_induction();
}

/*******************************************************************\

Function: goto_convertt::check_op_const

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool goto_convertt::check_op_const(
  const exprt &tmp,
  const locationt &loc)
{
  if (tmp.is_constant() || tmp.type().id() == "pointer")
  {
    std::cerr << "warning: this program " << loc.get_file()
              << " contains a bounded loop at line " << loc.get_line()
	      << ", so we are not applying the k-induction method to this program!"
              << std::endl;
    disable_k_induction();
    return true;
  }

  return false;
}

/*******************************************************************\

Function: goto_convertt::init_nondet_expr

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::init_nondet_expr(
  exprt &tmp,
  goto_programt &dest)
{

  if (!tmp.is_symbol()) return ;
  exprt nondet_expr=side_effect_expr_nondett(tmp.type());
  code_assignt new_assign_nondet(tmp,nondet_expr);
  copy(new_assign_nondet, ASSIGN, dest);
  if (!is_ifthenelse_block())
  nondet_vars.insert(std::pair<exprt,exprt>(tmp,nondet_expr));
}


/*******************************************************************\

Function: goto_convertt::replace_infinite_loop

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::replace_infinite_loop(
  exprt &tmp,
  goto_programt &dest)
{
  //declare variable i$ of type uint
  std::string identifier;
  identifier = "c::i$"+i2string(state_counter);
  exprt indice = symbol_exprt(identifier, uint_type());

  get_struct_components(indice);

  //declare variables n$ of type uint
  identifier = "c::n$"+i2string(state_counter);
  exprt n_expr = symbol_exprt(identifier, uint_type());

  get_struct_components(n_expr);

  exprt zero_expr = gen_zero(uint_type());
  exprt nondet_expr=side_effect_expr_nondett(uint_type());

  //initialize i=0
  code_assignt new_assign(indice,zero_expr);
  copy(new_assign, ASSIGN, dest);


  //initialize n=nondet_uint();
  code_assignt new_assign_nondet(n_expr,nondet_expr);
  copy(new_assign_nondet, ASSIGN, dest);

  //assume that n>0;
  assume_cond(gen_binary(exprt::i_gt, bool_typet(), n_expr, zero_expr), false, dest);

  //replace the condition c by i<=n;
  tmp = gen_binary(exprt::i_le, bool_typet(), indice, n_expr);
}

/*******************************************************************\

Function: goto_convertt::set_expr_to_nondet

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::set_expr_to_nondet(
  exprt &tmp,
  goto_programt &dest)
{
  nondet_varst::const_iterator cache_result;
  if (tmp.op0().is_constant())
  {
    cache_result = nondet_vars.find(tmp.op1());
    if (cache_result == nondet_vars.end())
      init_nondet_expr(tmp.op1(), dest);
  }
  else
  {
    cache_result = nondet_vars.find(tmp.op0());
    if (cache_result == nondet_vars.end())
      init_nondet_expr(tmp.op0(), dest);
#if 0
    else {
      //declare variables x$ of type uint
      std::string identifier;
      identifier = "c::x$"+i2string(state_counter);
      exprt x_expr = symbol_exprt(identifier, uint_type());
      get_struct_components(x_expr);
      exprt nondet_expr=side_effect_expr_nondett(uint_type());

      //initialize x=nondet_uint();
      code_assignt new_assign_nondet(x_expr,nondet_expr);
      copy(new_assign_nondet, ASSIGN, dest);

      exprt new_expr = gen_binary(exprt::i_gt, bool_typet(), x_expr, tmp.op1());
			tmp.swap(new_expr);
		}
#endif
  }
}

/*******************************************************************\

Function: goto_convertt::replace_cond

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::replace_cond(
  exprt &tmp,
  goto_programt &dest)
{
  //std::cout << tmp.id() << std::endl;

  irep_idt exprid = tmp.id();

  if (tmp.is_true())
  {
    //replace_infinite_loop(tmp, dest);
  }
  else if (exprid == ">" ||  exprid == ">=")
  {
    assert(tmp.operands().size()==2);
    if (is_for_block()) {
      if (check_op_const(tmp.op0(), tmp.location()))
        return ;
    } else if (tmp.op0().is_typecast() || tmp.op1().is_typecast())
    	return ;

    set_expr_to_nondet(tmp, dest);

  }
  else if ( exprid == "<" ||  exprid == "<=")
  {
    //std::cout << tmp.pretty() << std::endl;
    if (is_for_block())
      if (check_op_const(tmp.op1(), tmp.location()))
        return ;

    nondet_varst::const_iterator cache_result;
    if (tmp.op1().is_constant())
    {
      cache_result = nondet_vars.find(tmp.op0());
      if (cache_result == nondet_vars.end())
        init_nondet_expr(tmp.op0(), dest);
    }
    else
    {
      cache_result = nondet_vars.find(tmp.op1());
      if (cache_result == nondet_vars.end())
        init_nondet_expr(tmp.op1(), dest);
    }
  }
  else if ( exprid == "and" || exprid == "or")
  {
    assert(tmp.operands().size()==2);
    assert(tmp.op0().operands().size()==2);
    assert(tmp.op1().operands().size()==2);

    //check whether we have the same variable
    if (!tmp.op0().op0().is_constant())
    {
      if ((tmp.op0().op0() == tmp.op1().op0()) ||
          (tmp.op0().op0() == tmp.op1().op1()))
      {
        print_msg(tmp);
      }
    }
    else if (!tmp.op0().op1().is_constant())
    {
      if ((tmp.op0().op1() == tmp.op1().op0()) ||
          (tmp.op0().op1() == tmp.op1().op1()))
      {
        print_msg(tmp);
      }
    }
  }
  else if (exprid == "notequal")
  {
    //std::cout << tmp.pretty() << std::endl;
    if (!tmp.op0().is_symbol())
      print_msg(tmp);

    set_expr_to_nondet(tmp, dest);
  }
  else
  {
    std::cerr << "warning: the expression '" << tmp.id()
	      << "' located at line " << tmp.location().get_line()
	      << " of " << tmp.location().get_file()
  	      << " is not supported yet" << std::endl;
    //std::cout << tmp.pretty() << std::endl;
    //assert(0);
  }
}

/*******************************************************************\

Function: goto_convertt::increment_var

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::increment_var(
  const exprt &var,
  goto_programt &dest)
{
  if (var.is_true())
  {
	std::string identifier;
	identifier = "c::i$"+i2string(state_counter);
	exprt lhs_expr = symbol_exprt(identifier, uint_type());

    //increment var by 1
    exprt one_expr = gen_one(uint_type());
    exprt rhs_expr = gen_binary(exprt::plus, uint_type(), lhs_expr, one_expr);
    code_assignt new_assign_indice(lhs_expr,rhs_expr);
    copy(new_assign_indice, ASSIGN, dest);
  }

}

/*******************************************************************\

Function: goto_convertt::convert_while

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_while(
  const codet &code,
  goto_programt &dest)
{
  if(code.operands().size()!=2)
  {
    err_location(code);
    throw "while takes two operands";
  }

  exprt tmp=code.op0();

  set_while_block(true);

  if(inductive_step)
    replace_cond(tmp, dest);

  array_typet state_vector;
  const exprt &cond=tmp;
  const locationt &location=code.location();


  //    while(c) P;
  //--------------------
  // t: cs=nondet
  // v: sideeffects in c
  // c: k=0
  // v: if(!c) goto z;
  // f: s[k]=cs
  // x: P;
  // d: cs assign
  // e: assume
  // y: goto v;          <-- continue target
  // z: ;                <-- break target
  // g: assume(!c)

  // do the t label
  if(inductive_step)
  {
    get_struct_components(code.op1());
    make_nondet_assign(dest);
  }

  // save break/continue targets
  break_continue_targetst old_targets(targets);

  // do the z label
  goto_programt tmp_z;
  goto_programt::targett z=tmp_z.add_instruction();
  z->make_skip();

  if (code.has_operands())
    if (code.op0().statement() == "decl-block")
    {
      if (!code.op0().op0().op0().is_nil() &&
          !code.op0().op0().op1().is_nil())
      {
        exprt *lhs=&code.op0().op0().op0(),
              *rhs=&code.op0().op0().op1();
        if(rhs->id()=="sideeffect" &&
            (rhs->statement()=="cpp_new" ||
                rhs->statement()=="cpp_new[]"))
        {
          remove_sideeffects(*rhs, dest);
          Forall_operands(it, *lhs)
            remove_sideeffects(*it, dest);
          code.op0() = code.op0().op0().op0();
          if (!code.op0().type().is_bool())
            code.op0().make_typecast(bool_typet());

          do_cpp_new(*lhs, *rhs, dest);
          cond = code.op0();
        }
      }
    }

  goto_programt tmp_branch;
  generate_conditional_branch(gen_not(cond), z, location, tmp_branch);

  // do the v label
  goto_programt::targett v=tmp_branch.instructions.begin();

  // do the y label
  goto_programt tmp_y;
  goto_programt::targett y=tmp_y.add_instruction();

  // set the targets
  targets.set_break(z);
  targets.set_continue(y);

  // do the x label
  goto_programt tmp_x;

  convert(to_code(code.op1()), tmp_x);

  // y: if(c) goto v;
  y->make_goto(v);
  y->guard.make_true();
  y->location=code.location();

  //do the c label
  if (inductive_step)
    init_k_indice(dest);

  dest.destructive_append(tmp_branch);

  // do the f label
  if (inductive_step)
    update_state_vector(state_vector, dest);

  if (inductive_step)
    increment_var(code.op0(), dest);

  dest.destructive_append(tmp_x);

  // do the d label
  if (inductive_step)
    assign_current_state(dest);

  // do the e label
  if (inductive_step)
    assume_state_vector(state_vector, dest);

  dest.destructive_append(tmp_y);

  dest.destructive_append(tmp_z);

  //do the g label
  if (!is_break() && !is_goto()
			&& (inductive_step))
    assume_cond(cond, true, dest); //assume(!c)
  else if (k_induction)
    assert_cond(tmp, true, dest); //assert(!c)

  // restore break/continue
  targets.restore(old_targets);

  state_counter++;
  set_while_block(false);
  set_break(false);
  set_goto(false);
}

/*******************************************************************\

Function: goto_convertt::convert_dowhile

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_dowhile(
  const codet &code,
  goto_programt &dest)
{
  if(code.operands().size()!=2)
  {
    err_location(code);
    throw "dowhile takes two operands";
  }

  // save location
  locationt condition_location=code.op0().find_location();

  exprt tmp=code.op0();

  goto_programt sideeffects;
  remove_sideeffects(tmp, sideeffects);

  set_while_block(true);

  if(inductive_step)
    replace_cond(tmp, dest);

  array_typet state_vector;
  const exprt &cond=tmp;

  //    do P while(c);
  //--------------------
  // t: cs=nondet
  // w: P;
  // x: sideeffects in c   <-- continue target
  // c: k=0
  // y: if(c) goto w;
  // f: s[k]=cs
  // d: cs assign
  // e: assume
  // z: ;                  <-- break target
  // g: assume(!c)

  // do the t label
  if(inductive_step)
  {
    get_struct_components(code.op1());
    make_nondet_assign(dest);
  }

  // save break/continue targets
  break_continue_targetst old_targets(targets);

  // do the y label
  goto_programt tmp_y;
  goto_programt::targett y=tmp_y.add_instruction();

  // do the z label
  goto_programt tmp_z;
  goto_programt::targett z=tmp_z.add_instruction();
  z->make_skip();

  // do the x label
  goto_programt::targett x;
  if(sideeffects.instructions.empty())
    x=y;
  else
    x=sideeffects.instructions.begin();

  //do the c label
  if (inductive_step)
    init_k_indice(dest);

  // set the targets
  targets.set_break(z);
  targets.set_continue(x);

  // do the w label
  goto_programt tmp_w;
  convert(to_code(code.op1()), tmp_w);
  goto_programt::targett w=tmp_w.instructions.begin();

  dest.destructive_append(tmp_w);
  dest.destructive_append(sideeffects);

#if 0
  if(options.get_bool_option("atomicity-check"))
  {
    unsigned int globals = get_expr_number_globals(cond);
    if(globals > 0)
	  break_globals2assignments(cond, dest,code.location());
  }
#endif

  // y: if(c) goto w;
  y->make_goto(w);
  y->guard=cond;
  y->location=condition_location;

  dest.destructive_append(tmp_y);

  // do the f label
  if (inductive_step)
    update_state_vector(state_vector, dest);

  // do the d label
  if (inductive_step)
    assign_current_state(dest);

  // do the e label
  if (inductive_step)
    assume_state_vector(state_vector, dest);

  dest.destructive_append(tmp_z);

  //do the g label
  if (!is_break() && !is_goto()
			&& (/*base_case ||*/ inductive_step))
    assume_cond(cond, true, dest); //assume(!c)
  else if (k_induction)
    assert_cond(tmp, true, dest); //assert(!c)

  // restore break/continue targets
  targets.restore(old_targets);
}

/*******************************************************************\

Function: goto_convertt::case_guard

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::case_guard(
  const exprt &value,
  const exprt::operandst &case_op,
  exprt &dest)
{
  dest=exprt("or", typet("bool"));
  dest.reserve_operands(case_op.size());

  forall_expr(it, case_op)
  {
    equality_exprt eq_expr;
    eq_expr.lhs()=value;
    eq_expr.rhs()=*it;
    dest.move_to_operands(eq_expr);
  }

  assert(dest.operands().size()!=0);

  if(dest.operands().size()==1)
  {
    exprt tmp;
    tmp.swap(dest.op0());
    dest.swap(tmp);
  }
}

/*******************************************************************\

Function: goto_convertt::convert_switch

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_switch(
  const codet &code,
  goto_programt &dest)
{
  // switch(v) {
  //   case x: Px;
  //   case y: Py;
  //   ...
  //   default: Pd;
  // }
  // --------------------
  // x: if(v==x) goto X;
  // y: if(v==y) goto Y;
  //    goto d;
  // X: Px;
  // Y: Py;
  // d: Pd;
  // z: ;

  if(code.operands().size()<2)
  {
    err_location(code);
    throw "switch takes at least two operands";
  }
  //switch declaration for C++x11
  if (code.op0().statement() == "decl-block")
    if (code.has_operands())
      if (!code.op0().op0().op0().is_nil() &&
          !code.op0().op0().op1().is_nil())
      {
        exprt lhs(code.op0().op0().op0());
        lhs.location()=code.op0().op0().location();
        exprt rhs(code.op0().op0().op1());

        rhs.type()=code.op0().op0().op1().type();

        code.op0() = code.op0().op0().op0();
        codet assignment("assign");
        assignment.copy_to_operands(lhs);
        assignment.move_to_operands(rhs);
        assignment.location()=lhs.location();
        convert(assignment, dest);
      }

  exprt argument=code.op0();

  goto_programt sideeffects;
  remove_sideeffects(argument, sideeffects);

  // save break/continue/default/cases targets
  break_continue_switch_targetst old_targets(targets);

  // do the z label
  goto_programt tmp_z;
  goto_programt::targett z=tmp_z.add_instruction();
  z->make_skip();

  // set the new targets -- continue stays as is
  targets.set_break(z);
  targets.set_default(z);
  targets.cases.clear();

  goto_programt tmp;

  forall_operands(it, code)
    if(it!=code.operands().begin())
    {
      goto_programt t;
      convert(to_code(*it), t);
      tmp.destructive_append(t);
    }

  goto_programt tmp_cases;

  for(casest::iterator it=targets.cases.begin();
      it!=targets.cases.end();
      it++)
  {
    const caset &case_ops=it->second;

    assert(!case_ops.empty());

    exprt guard_expr;
    case_guard(argument, case_ops, guard_expr);

    if(options.get_bool_option("atomicity-check"))
    {
      unsigned int globals = get_expr_number_globals(guard_expr);
      if(globals > 0)
        break_globals2assignments(guard_expr, tmp_cases,code.location());
    }

    goto_programt::targett x=tmp_cases.add_instruction();
    x->make_goto(it->first);
    x->guard.swap(guard_expr);
    x->location=case_ops.front().find_location();
  }

  {
    goto_programt::targett d_jump=tmp_cases.add_instruction();
    d_jump->make_goto(targets.default_target);
    d_jump->location=targets.default_target->location;
  }

  dest.destructive_append(sideeffects);
  dest.destructive_append(tmp_cases);
  dest.destructive_append(tmp);
  dest.destructive_append(tmp_z);

  // restore old targets
  targets.restore(old_targets);
}

/*******************************************************************\

Function: goto_convertt::convert_break

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_break(
  const code_breakt &code,
  goto_programt &dest)
{
  if(!targets.break_set)
  {
    err_location(code);
    throw "break without target";
  }

  goto_programt::targett t=dest.add_instruction();
  t->make_goto(targets.break_target);
  t->location=code.location();

  if ((/*base_case ||*/ inductive_step) &&
	(is_while_block()))
    set_break(true);
}

/*******************************************************************\

Function: goto_convertt::convert_return

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_return(
  const code_returnt &code,
  goto_programt &dest)
{
  if(!targets.return_set)
  {
    err_location(code);
    throw "return without target";
  }

  if(code.operands().size()!=0 &&
     code.operands().size()!=1)
  {
    err_location(code);
    throw "return takes none or one operand";
  }

  code_returnt new_code(code);

  if(new_code.has_return_value())
  {
    goto_programt sideeffects;
    remove_sideeffects(new_code.return_value(), sideeffects);
    dest.destructive_append(sideeffects);

    if(options.get_bool_option("atomicity-check"))
    {
      unsigned int globals = get_expr_number_globals(new_code.return_value());
      if(globals > 0)
        break_globals2assignments(new_code.return_value(), dest,code.location());
    }
  }

  if(targets.return_value)
  {
    if(!new_code.has_return_value())
    {
      err_location(new_code);
      throw "function must return value";
    }
  }
  else
  {
    if(new_code.has_return_value() &&
       new_code.return_value().type().id()!="empty")
    {
      err_location(new_code);
      throw "function must not return value";
    }
  }

  goto_programt::targett t=dest.add_instruction();
  t->make_return();
  t->code=new_code;
  t->location=new_code.location();
}

/*******************************************************************\

Function: goto_convertt::convert_continue

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_continue(
  const code_continuet &code,
  goto_programt &dest)
{
  if(!targets.continue_set)
  {
    err_location(code);
    throw "continue without target";
  }

  goto_programt::targett t=dest.add_instruction();
  t->make_goto(targets.continue_target);
  t->location=code.location();
}

/*******************************************************************\

Function: goto_convertt::convert_goto

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_goto(
  const codet &code,
  goto_programt &dest)
{
  goto_programt::targett t=dest.add_instruction();
  t->make_goto();
  t->location=code.location();
  t->code=code;

  // remember it to do target later
  targets.gotos.insert(t);

  if ((/*base_case ||*/ inductive_step) &&
	(is_while_block()))
    set_goto(true);
}

/*******************************************************************\

Function: goto_convertt::convert_non_deterministic_goto

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_non_deterministic_goto(
  const codet &code,
  goto_programt &dest)
{
  convert_goto(code, dest);
}

/*******************************************************************\

Function: goto_convertt::convert_atomic_begin

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_atomic_begin(
  const codet &code,
  goto_programt &dest)
{
  if(code.operands().size()!=0)
  {
    err_location(code);
    throw "atomic_begin expects no operands";
  }


  copy(code, ATOMIC_BEGIN, dest);
}

/*******************************************************************\

Function: goto_convertt::convert_atomic_end

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_atomic_end(
  const codet &code,
  goto_programt &dest)
{
  if(code.operands().size()!=0)
  {
    err_location(code);
    throw "atomic_end expects no operands";
  }

  copy(code, ATOMIC_END, dest);
}

/*******************************************************************\

Function: goto_convertt::convert_bp_enforce

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_bp_enforce(
  const codet &code,
  goto_programt &dest)
{
  if(code.operands().size()!=2)
  {
    err_location(code);
    error("bp_enfroce expects two arguments");
    throw 0;
  }

  // do an assume
  exprt op=code.op0();

  remove_sideeffects(op, dest);

  goto_programt::targett t=dest.add_instruction(ASSUME);
  t->guard=op;
  t->location=code.location();

  // change the assignments

  goto_programt tmp;
  convert(to_code(code.op1()), tmp);

  if(!op.is_true())
  {
    exprt constraint(op);
    make_next_state(constraint);

    Forall_goto_program_instructions(it, tmp)
    {
      if(it->is_assign())
      {
        assert(it->code.statement()=="assign");

        // add constrain
        codet constrain("bp_constrain");
        constrain.reserve_operands(2);
        constrain.move_to_operands(it->code);
        constrain.copy_to_operands(constraint);
        it->code.swap(constrain);

        it->type=OTHER;
      }
      else if(it->is_other() &&
              it->code.statement()=="bp_constrain")
      {
        // add to constraint
        assert(it->code.operands().size()==2);
        it->code.op1()=
          gen_and(it->code.op1(), constraint);
      }
    }
  }

  dest.destructive_append(tmp);
}

/*******************************************************************\

Function: goto_convertt::convert_bp_abortif

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_bp_abortif(
  const codet &code,
  goto_programt &dest)
{
  if(code.operands().size()!=1)
  {
    err_location(code);
    throw "bp_abortif expects one argument";
  }

  // do an assert
  exprt op=code.op0();

  remove_sideeffects(op, dest);

  op.make_not();

  goto_programt::targett t=dest.add_instruction(ASSERT);
  t->guard.swap(op);
  t->location=code.location();
}

/*******************************************************************\

Function: goto_convertt::generate_ifthenelse

  Inputs:

 Outputs:

 Purpose: if(guard) goto target;

\*******************************************************************/

void goto_convertt::generate_ifthenelse(
  const exprt &guard,
  goto_programt &true_case,
  goto_programt &false_case,
  const locationt &location,
  goto_programt &dest)
{
  if(true_case.instructions.empty() &&
     false_case.instructions.empty())
    return;

  // do guarded gotos directly
  if(false_case.instructions.empty() &&
     true_case.instructions.size()==1 &&
     true_case.instructions.back().is_goto() &&
     true_case.instructions.back().guard.is_true())
  {
    true_case.instructions.back().guard=guard;
    dest.destructive_append(true_case);
    return;
  }

  if(true_case.instructions.empty())
    return generate_ifthenelse(
      gen_not(guard), false_case, true_case, location, dest);

  bool has_else=!false_case.instructions.empty();

  //    if(c) P;
  //--------------------
  // v: if(!c) goto z;
  // w: P;
  // z: ;

  //    if(c) P; else Q;
  //--------------------
  // v: if(!c) goto y;
  // w: P;
  // x: goto z;
  // y: Q;
  // z: ;

  // do the x label
  goto_programt tmp_x;
  goto_programt::targett x=tmp_x.add_instruction();

  // do the z label
  goto_programt tmp_z;
  goto_programt::targett z=tmp_z.add_instruction();
  z->make_skip();

  // y: Q;
  goto_programt tmp_y;
  goto_programt::targett y;
  if(has_else)
  {
    tmp_y.swap(false_case);
    y=tmp_y.instructions.begin();
  }

  // v: if(!c) goto z/y;
  goto_programt tmp_v;
  generate_conditional_branch(
    gen_not(guard), has_else?y:z, location, tmp_v);

  // w: P;
  goto_programt tmp_w;
  tmp_w.swap(true_case);

  // x: goto z;
  x->make_goto(z);

  dest.destructive_append(tmp_v);
  dest.destructive_append(tmp_w);

  if(has_else)
  {
    dest.destructive_append(tmp_x);
    dest.destructive_append(tmp_y);
  }

  dest.destructive_append(tmp_z);
}

/*******************************************************************\

Function: goto_convertt::get_cs_member

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::get_cs_member(
  exprt &expr,
  exprt &result,
  const typet &type,
  bool &found)
{
  DEBUGLOC;

  found=false;
  std::string identifier;

  identifier = "cs$"+i2string(state_counter);

  exprt lhs_struct("symbol", state);
  lhs_struct.identifier(identifier);

  exprt new_expr(exprt::member, type);
  new_expr.reserve_operands(1);
  new_expr.copy_to_operands(lhs_struct);
  new_expr.identifier(expr.get_string("identifier"));
  new_expr.component_name(expr.get_string("identifier"));

  assert(!new_expr.get_string("component_name").empty());

  const struct_typet &struct_type = to_struct_type(lhs_struct.type());
  const struct_typet::componentst &components = struct_type.components();
  u_int i = 0;

  for (struct_typet::componentst::const_iterator
       it = components.begin();
       it != components.end();
       it++, i++)
  {
    if (it->get("name").compare(new_expr.get_string("component_name")) == 0)
      found=true;
  }

  if (!found)
  {
    new_expr = expr;
    std::cerr << "warning: the symbol '" << expr.get_string("identifier")
  		  << "' at line " << expr.location().get_line()
  		  << " is not a member of the state struct" << std::endl;
  }


  result = new_expr;
}

/*******************************************************************\

Function: goto_convertt::get_new_expr

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::get_new_expr(exprt &expr, exprt &new_expr, bool &found)
{
  DEBUGLOC;

  if (expr.is_symbol())
  {
    if (expr.type().is_pointer() &&
    		expr.type().subtype().is_symbol())
      found = true;
    else
      get_cs_member(expr, new_expr, expr.type(), found);
  }
  else if (expr.is_constant())
  {
    new_expr = expr;
    found=true;
  }
  else if (expr.is_index())
  {
    exprt array, index;
    assert(expr.operands().size()==2);

    //do we have an index of index?
    if (expr.op0().operands().size() == 2)
      get_new_expr(expr.op0(), array, found); // this should return another index
    else
      get_cs_member(expr.op0(), array, expr.op0().type(), found); //we have one index only

    get_cs_member(expr.op1(), index, expr.op1().type(), found);

    exprt tmp(exprt::index, expr.op0().type());
    tmp.reserve_operands(2);
    tmp.copy_to_operands(array);
    tmp.copy_to_operands(index);
    new_expr = tmp;
  }
  else if (expr.operands().size() == 1)
  {
    get_new_expr(expr.op0(), new_expr, found);
  }
  else if (expr.operands().size() == 2)
  {
    exprt operand0, operand1;
    get_new_expr(expr.op0(), operand0, found);
    get_new_expr(expr.op1(), operand1, found);

    new_expr = gen_binary(expr.id().as_string(), expr.type(), operand0, operand1);

    if (new_expr.op0().is_index())
      assert(new_expr.op0().type().id() == expr.op0().op0().type().id());
    else if (new_expr.op0().type()!=new_expr.op1().type())
   	  new_expr.op1().make_typecast(new_expr.op0().type());
    else
      assert(new_expr.op0().type()==new_expr.op1().type());
  }
  else
  {
    std::cerr << "warning: the expression '" << expr.pretty()
  		  << "' is not supported yet" << std::endl;
    assert(0);
  }

  if (!found) new_expr = expr;
}

/*******************************************************************\

Function: goto_convertt::replace_ifthenelse

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::replace_ifthenelse(
		exprt &expr)
{
  DEBUGLOC;

  bool found=false;

  if(expr.id()=="constant")
    return;

  if (expr.operands().size()==0 || expr.operands().size() == 1)
  {
    exprt new_expr;
    if (expr.operands().size())
      get_new_expr(expr.op0(), new_expr, found);
    else
      get_new_expr(expr, new_expr, found);

    if (!found) std::cout << "failed" << std::endl;
    assert(found);

    expr = new_expr;
  }
  else
  {
    assert(expr.operands().size()==2);

    if(expr.has_operands())
    {
      exprt::operandst::iterator it = expr.operands().begin();
      for( ; it != expr.operands().end(); ++it)
        replace_ifthenelse(*it);
      return;
    }

    nondet_varst::const_iterator result_op0 = nondet_vars.find(expr.op0());
    nondet_varst::const_iterator result_op1 = nondet_vars.find(expr.op1());

    if (result_op0 != nondet_vars.end() && result_op1 != nondet_vars.end())
      return;
    else if (expr.op0().is_constant() || expr.op1().is_constant()) {
      if (result_op0 != nondet_vars.end() || result_op1 != nondet_vars.end())
        return;
    }

    loop_varst::const_iterator cache_result = loop_vars.find(expr.op0());
    if (cache_result == loop_vars.end())
      return;

    assert(expr.op0().type() == expr.op1().type());

    exprt new_expr1, new_expr2;

    get_new_expr(expr.op0(), new_expr1, found);
    found=false;
    get_new_expr(expr.op1(), new_expr2, found);

    if (expr.op0().is_index())
      assert(new_expr1.type().id() == expr.op0().op0().type().id());
    else if (expr.op1().is_index())
      assert(new_expr2.type().id() == expr.op1().op0().type().id());
    else
      if (new_expr1.type().id() != new_expr2.type().id() ||
          new_expr1.type().width()!=new_expr2.type().width())
        new_expr2.make_typecast(new_expr1.type());

    expr = gen_binary(expr.id().as_string(), bool_typet(), new_expr1, new_expr2);
  }
}

/*******************************************************************\

Function: goto_convertt::convert_ifthenelse

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::convert_ifthenelse(
  const codet &code,
  goto_programt &dest)
{
  set_ifthenelse_block(true);

  if(code.operands().size()!=2 &&
    code.operands().size()!=3)
  {
    err_location(code);
    throw "ifthenelse takes two or three operands";
  }

  bool has_else=
    code.operands().size()==3 &&
    !code.op2().is_nil();

  const locationt &location=code.location();

  // convert 'then'-branch
  goto_programt tmp_op1;
  convert(to_code(code.op1()), tmp_op1);

  goto_programt tmp_op2;

  if(has_else)
    convert(to_code(code.op2()), tmp_op2);

  exprt tmp_guard;
  if (options.get_bool_option("control-flow-test")
    && code.op0().id() != "notequal" && code.op0().id() != "symbol"
    && code.op0().id() != "typecast" && code.op0().id() != "="
    && !is_thread
    && !options.get_bool_option("deadlock-check"))
  {
    symbolt &new_symbol=new_cftest_symbol(code.op0().type());
    irept irep;
    new_symbol.to_irep(irep);

    codet assignment("assign");
    assignment.reserve_operands(2);
    assignment.copy_to_operands(symbol_expr(new_symbol));
    assignment.copy_to_operands(code.op0());
    assignment.location() = code.op0().find_location();
    copy(assignment, ASSIGN, dest);

    tmp_guard=symbol_expr(new_symbol);
  }
  else if (code.op0().statement() == "decl-block")
  {
    exprt lhs(code.op0().op0().op0());
    lhs.location()=code.op0().op0().location();
    exprt rhs(code.op0().op0().op1());

    rhs.type()=code.op0().op0().op1().type();

    codet assignment("assign");
    assignment.copy_to_operands(lhs);
    assignment.move_to_operands(rhs);
    assignment.location()=lhs.location();
    convert(assignment, dest);

    tmp_guard=assignment.op0();
    if (!tmp_guard.type().is_bool())
      tmp_guard.make_typecast(bool_typet());
  }
  else
    tmp_guard=code.op0();

  remove_sideeffects(tmp_guard, dest);
  if (inductive_step && (is_for_block() || is_while_block()))
  {
    replace_ifthenelse(tmp_guard);

    if (!tmp_guard.type().is_bool())
      tmp_guard.make_typecast(bool_typet());
  }

  generate_ifthenelse(tmp_guard, tmp_op1, tmp_op2, location, dest);
  set_ifthenelse_block(false);
}

/*******************************************************************\

Function: goto_convertt::collect_operands

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::collect_operands(
  const exprt &expr,
  const irep_idt &id,
  std::list<exprt> &dest)
{
  if(expr.id()!=id)
  {
    dest.push_back(expr);
  }
  else
  {
    // left-to-right is important
    forall_operands(it, expr)
      collect_operands(*it, id, dest);
  }
}

/*******************************************************************\

Function: goto_convertt::generate_conditional_branch

  Inputs:

 Outputs:

 Purpose: if(guard) goto target;

\*******************************************************************/

void goto_convertt::generate_conditional_branch(
  const exprt &guard,
  goto_programt::targett target_true,
  const locationt &location,
  goto_programt &dest)
{

  if(!has_sideeffect(guard))
  {
    exprt g = guard;
    if(options.get_bool_option("atomicity-check"))
    {
      unsigned int globals = get_expr_number_globals(g);
      if(globals > 0)
        break_globals2assignments(g, dest,location);
    }
    // this is trivial
    goto_programt::targett t=dest.add_instruction();
    t->make_goto(target_true);
    t->guard=g;
    t->location=location;
    return;
  }

  // if(guard) goto target;
  //   becomes
  // if(guard) goto target; else goto next;
  // next: skip;

  goto_programt tmp;
  goto_programt::targett target_false=tmp.add_instruction();
  target_false->make_skip();

  generate_conditional_branch(guard, target_true, target_false, location, dest);

  dest.destructive_append(tmp);
}

/*******************************************************************\

Function: goto_convertt::generate_conditional_branch

  Inputs:

 Outputs:

 Purpose: if(guard) goto target;

\*******************************************************************/

void goto_convertt::generate_conditional_branch(
  const exprt &guard,
  goto_programt::targett target_true,
  goto_programt::targett target_false,
  const locationt &location,
  goto_programt &dest)
{
  if(guard.id()=="not")
  {
    assert(guard.operands().size()==1);
    // swap targets
    generate_conditional_branch(
      guard.op0(), target_false, target_true, location, dest);
    return;
  }

  if(!has_sideeffect(guard))
  {
	exprt g = guard;
	if(options.get_bool_option("atomicity-check"))
	{
	  unsigned int globals = get_expr_number_globals(g);
	  if(globals > 0)
		break_globals2assignments(g, dest,location);
	}

    // this is trivial
    goto_programt::targett t_true=dest.add_instruction();
    t_true->make_goto(target_true);
    t_true->guard=guard;
    t_true->location=location;

    goto_programt::targett t_false=dest.add_instruction();
    t_false->make_goto(target_false);
    t_false->guard=true_exprt();
    t_false->location=location;
    return;
  }

  if(guard.is_and())
  {
    // turn
    //   if(a && b) goto target_true; else goto target_false;
    // into
    //    if(!a) goto target_false;
    //    if(!b) goto target_false;
    //    goto target_true;

    std::list<exprt> op;
    collect_operands(guard, guard.id(), op);

    forall_expr_list(it, op)
      generate_conditional_branch(
        gen_not(*it), target_false, location, dest);

    goto_programt::targett t_true=dest.add_instruction();
    t_true->make_goto(target_true);
    t_true->guard=true_exprt();
    t_true->location=location;

    return;
  }
  else if(guard.id()=="or")
  {
    // turn
    //   if(a || b) goto target_true; else goto target_false;
    // into
    //   if(a) goto target_true;
    //   if(b) goto target_true;
    //   goto target_false;

    std::list<exprt> op;
    collect_operands(guard, guard.id(), op);

    forall_expr_list(it, op)
      generate_conditional_branch(
        *it, target_true, location, dest);

    goto_programt::targett t_false=dest.add_instruction();
    t_false->make_goto(target_false);
    t_false->guard=true_exprt();
    t_false->location=guard.location();

    return;
  }

  exprt cond=guard;
  remove_sideeffects(cond, dest);

  if(options.get_bool_option("atomicity-check"))
  {
    unsigned int globals = get_expr_number_globals(cond);
	if(globals > 0)
	  break_globals2assignments(cond, dest,location);
  }

  goto_programt::targett t_true=dest.add_instruction();
  t_true->make_goto(target_true);
  t_true->guard=cond;
  t_true->location=guard.location();

  goto_programt::targett t_false=dest.add_instruction();
  t_false->make_goto(target_false);
  t_false->guard=true_exprt();
  t_false->location=guard.location();
}

/*******************************************************************\

Function: goto_convertt::get_string_constant

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

const std::string &goto_convertt::get_string_constant(
  const exprt &expr)
{
  if(expr.id()=="typecast" &&
     expr.operands().size()==1)
    return get_string_constant(expr.op0());

  if(!expr.is_address_of() ||
     expr.operands().size()!=1 ||
     expr.op0().id()!="index" ||
     expr.op0().operands().size()!=2 ||
     expr.op0().op0().id()!="string-constant")
  {
    err_location(expr);
    str << "expected string constant, but got: "
          << expr.pretty() << std::endl;
    throw 0;
  }

  return expr.op0().op0().value().as_string();
}

/*******************************************************************\

Function: goto_convertt::new_tmp_symbol

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

symbolt &goto_convertt::new_tmp_symbol(const typet &type)
{
  symbolt new_symbol;
  symbolt *symbol_ptr;

  do {
    new_symbol.base_name="tmp$"+i2string(++temporary_counter);
    new_symbol.name=tmp_symbol_prefix+id2string(new_symbol.base_name);
    new_symbol.lvalue=true;
    new_symbol.type=type;
  } while (context.move(new_symbol, symbol_ptr));

  tmp_symbols.push_back(symbol_ptr->name);

  return *symbol_ptr;
}

/*******************************************************************\

Function: goto_convertt::new_cftest_symbol

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

symbolt &goto_convertt::new_cftest_symbol(const typet &type)
{
  static int cftest_counter=0;
  symbolt new_symbol;
  symbolt *symbol_ptr;

  do {
    new_symbol.base_name="cftest$"+i2string(++cftest_counter);
    new_symbol.name=tmp_symbol_prefix+id2string(new_symbol.base_name);
    new_symbol.lvalue=true;
    new_symbol.type=type;
  } while (context.move(new_symbol, symbol_ptr));

  tmp_symbols.push_back(symbol_ptr->name);

  return *symbol_ptr;
}

/*******************************************************************\

Function: goto_convertt::guard_program

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_convertt::guard_program(
  const guardt &guard,
  goto_programt &dest)
{
  if(guard.is_true()) return;

  // the target for the GOTO
  goto_programt::targett t=dest.add_instruction(SKIP);

  goto_programt tmp;
  tmp.add_instruction(GOTO);
  tmp.instructions.front().targets.push_back(t);
  tmp.instructions.front().guard=gen_not(guard.as_expr());
  tmp.destructive_append(dest);

  tmp.swap(dest);
}
