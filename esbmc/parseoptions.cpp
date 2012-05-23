/*******************************************************************\

Module: Main Module

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <fstream>
#include <memory>

#ifndef _WIN32
extern "C" {
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/sendfile.h>
#include <sys/time.h>
#include <sys/types.h>
}
#endif

#include <irep.h>
#include <config.h>
#include <expr_util.h>

#include <goto-programs/goto_convert_functions.h>
#include <goto-programs/goto_check.h>
#include <goto-programs/goto_inline.h>
#include <goto-programs/show_claims.h>
#include <goto-programs/set_claims.h>
#include <goto-programs/read_goto_binary.h>
#include <goto-programs/string_abstraction.h>
#include <goto-programs/string_instrumentation.h>
#include <goto-programs/loop_numbers.h>

#include <goto-programs/add_race_assertions.h>

#include <pointer-analysis/value_set_analysis.h>
#include <pointer-analysis/goto_program_dereference.h>
#include <pointer-analysis/add_failed_symbols.h>
#include <pointer-analysis/show_value_sets.h>

#include <langapi/mode.h>
#include <langapi/languages.h>

#include <ansi-c/c_preprocess.h>

#include "parseoptions.h"
#include "bmc.h"
#include "version.h"

// jmorse - could be somewhere better

#ifndef _WIN32
void
timeout_handler(int dummy __attribute__((unused)))
{

  std::cout << "Timed out" << std::endl;

  // Unfortunately some highly useful pieces of code hook themselves into
  // aexit and attempt to free some memory. That doesn't really make sense to
  // occur on exit, but more importantly doesn't mix well with signal handlers,
  // and results in the allocator locking against itself. So use _exit instead
  _exit(1);
}
#endif

/*******************************************************************\

Function: cbmc_parseoptionst::set_verbosity

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void cbmc_parseoptionst::set_verbosity(messaget &message)
{
  int v=8;

  if(cmdline.isset("verbosity"))
  {
    v=atoi(cmdline.getval("verbosity"));
    if(v<0)
      v=0;
    else if(v>9)
      v=9;
  }

  message.set_verbosity(v);
}

/*******************************************************************\

Function: cbmc_parseoptionst::get_command_line_options

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void cbmc_parseoptionst::get_command_line_options(optionst &options)
{
  if(config.set(cmdline))
  {
    exit(1);
  }

  options.cmdline(cmdline);

  if(cmdline.isset("arrays-uf-always"))
    options.set_option("arrays-uf", "always");
  else if(cmdline.isset("arrays-uf-never"))
    options.set_option("arrays-uf", "never");
  else
    options.set_option("arrays-uf", "auto");

  if(cmdline.isset("z3-bv"))
  {
    options.set_option("z3", true);
    options.set_option("z3-bv", true);
    options.set_option("int-encoding", false);
  }

  if (cmdline.isset("lazy"))
    options.set_option("no-assume-guarantee", false);
  else
	options.set_option("no-assume-guarantee", true);

  if (cmdline.isset("eager"))
    options.set_option("no-assume-guarantee", true);
  else
  	options.set_option("no-assume-guarantee", false);

  if(cmdline.isset("btor"))
  {
    options.set_option("btor", true);
  }

  if(cmdline.isset("z3-ir"))
  {
    options.set_option("z3", true);
    options.set_option("z3-ir", true);
    options.set_option("int-encoding", true);
  }

  if(cmdline.isset("no-slice"))
    options.set_option("no-assume-guarantee", false);

  options.set_option("string-abstraction", true);
  options.set_option("fixedbv", true);

  if (!options.get_bool_option("z3"))
  {
    // If no solver options given, default to z3 integer encoding
    options.set_option("z3", true);
    options.set_option("int-encoding", true);
  }

  if(cmdline.isset("qf_aufbv"))
  {
	options.set_option("qf_aufbv", true);
    options.set_option("smt", true);
    options.set_option("z3", true);
  }

  if(cmdline.isset("qf_auflira"))
  {
	options.set_option("qf_auflira", true);
	options.set_option("smt", true);
    options.set_option("z3", true);
    options.set_option("int-encoding", true);
  }


   if(cmdline.isset("context-switch"))
     options.set_option("context-switch", cmdline.getval("context-switch"));
   else
     options.set_option("context-switch", -1);

   if(cmdline.isset("uw-model"))
   {
     options.set_option("uw-model", true);
     options.set_option("schedule", true);
   }
   else
     options.set_option("uw-model", false);

   if(cmdline.isset("no-lock-check"))
     options.set_option("no-lock-check", true);
   else
     options.set_option("no-lock-check", false);

   if(cmdline.isset("deadlock-check"))
   {
     options.set_option("deadlock-check", true);
     options.set_option("atomicity-check", false);
     //disable all other checks
     //options.set_option("no-bounds-check", true);
     //options.set_option("no-div-by-zero-check", true);
     //options.set_option("overflow-check", false);
     //options.set_option("no-pointer-check", true);
     options.set_option("no-assertions", true);
     //options.set_option("no-lock-check", true);
   }
   else
     options.set_option("deadlock-check", false);

  if (cmdline.isset("smtlib-ileave-num"))
    options.set_option("smtlib-ileave-num", cmdline.getval("smtlib-ileave-num"));
  else
    options.set_option("smtlib-ileave-num", "1");

  if(cmdline.isset("inlining"))
    options.set_option("inlining", true);

  // jmorse
  if(cmdline.isset("timeout")) {
#ifdef _WIN32
    std::cerr << "Timeout unimplemented on Windows, sorry" << std::endl;
    abort();
#else
    int len, mult, timeout;

    const char *time = cmdline.getval("timeout");
    len = strlen(time);
    if (!isdigit(time[len-1])) {
      switch (time[len-1]) {
      case 's':
        mult = 1;
        break;
      case 'm':
        mult = 60;
        break;
      case 'h':
        mult = 3600;
        break;
      case 'd':
        mult = 86400;
        break;
      default:
        std::cerr << "Unrecognized timeout suffix" << std::endl;
        abort();
      }
    } else {
      mult = 1;
    }

    timeout = strtol(time, NULL, 10);
    timeout *= mult;
    signal(SIGALRM, timeout_handler);
    alarm(timeout);
#endif
  }

  if(cmdline.isset("memlimit")) {
#ifdef _WIN32
    std::cerr << "Can't memlimit on Windows, sorry" << std::endl;
    abort();
#else
    unsigned long len, mult, size;

    const char *limit = cmdline.getval("memlimit");
    len = strlen(limit);
    if (!isdigit(limit[len-1])) {
      switch (limit[len-1]) {
      case 'b':
        mult = 1;
        break;
      case 'k':
        mult = 1024;
        break;
      case 'm':
        mult = 1024*1024;
        break;
      case 'g':
        mult = 1024*1024*1024;
        break;
      default:
        std::cerr << "Unrecognized memlimit suffix" << std::endl;
        abort();
      }
    } else {
      mult = 1024*1024;
    }

    size = strtol(limit, NULL, 10);
    size *= mult;

    struct rlimit lim;
    lim.rlim_cur = size;
    lim.rlim_max = size;
    if (setrlimit(RLIMIT_AS, &lim) != 0) {
      perror("Couldn't set memory limit");
      abort();
    }
#endif
  }

#ifndef _WIN32
  struct rlimit lim;
  if (cmdline.isset("enable-core-dump")) {
    lim.rlim_cur = RLIM_INFINITY;
    lim.rlim_max = RLIM_INFINITY;
    if (setrlimit(RLIMIT_CORE, &lim) != 0) {
      perror("Couldn't unlimit core dump size");
      abort();
    }
  } else {
    lim.rlim_cur = 0;
    lim.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &lim) != 0) {
      perror("Couldn't disable core dump size");
      abort();
    }
  }
#endif

  config.options = options;
}

/*******************************************************************\

Function: cbmc_parseoptionst::doit

  Inputs:

 Outputs:

 Purpose: invoke main modules

\*******************************************************************/

int cbmc_parseoptionst::doit()
{
  goto_functionst goto_functions;

  if(cmdline.isset("version"))
  {
    std::cout << ESBMC_VERSION << std::endl;
    return 0;
  }

  //
  // unwinding of transition systems
  //

  if(cmdline.isset("module") ||
    cmdline.isset("gen-interface"))

  {
    error("This version has no support for "
          " hardware modules.");
    return 1;
  }

  //
  // command line options
  //

  get_command_line_options(options);
  set_verbosity(*this);

  if(cmdline.isset("preprocess"))
  {
    preprocessing();
    return 0;
  }

  if(get_goto_program(goto_functions))
    return 6;

  if(cmdline.isset("show-claims"))
  {
    const namespacet ns(context);
    show_claims(ns, get_ui(), goto_functions);
    return 0;
  }

  if(set_claims(goto_functions))
    return 7;

  // slice according to property

  // do actual BMC
  bmct bmc(goto_functions, options, context, ui_message_handler);
  get_command_line_options(bmc.options);
  set_verbosity(bmc);
  return do_bmc(bmc);
}

/*******************************************************************\

Function: cbmc_parseoptionst::set_claims

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool cbmc_parseoptionst::set_claims(goto_functionst &goto_functions)
{
  try
  {
    if(cmdline.isset("claim"))
      ::set_claims(goto_functions, cmdline.get_values("claim"));
  }

  catch(const char *e)
  {
    error(e);
    return true;
  }

  catch(const std::string e)
  {
    error(e);
    return true;
  }

  catch(int)
  {
    return true;
  }

  return false;
}

/*******************************************************************\

Function: cbmc_parseoptionst::get_goto_program

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool cbmc_parseoptionst::get_goto_program(goto_functionst &goto_functions)
{
  try
  {
    if(cmdline.isset("binary"))
    {
      status("Reading GOTO program from file");

      if(read_goto_binary(goto_functions))
        return true;

      if(cmdline.isset("show-symbol-table"))
      {
        show_symbol_table();
        return true;
      }
    }
    else
    {
      if(cmdline.args.size()==0)
      {
        error("Please provide a program to verify");
        return true;
      }

      if(parse()) return true;
      if(typecheck()) return true;
      //if(get_modules()) return true;
      if(final()) return true;

      if(cmdline.isset("show-symbol-table"))
      {
        show_symbol_table();
        return true;
      }

      // we no longer need any parse trees or language files
      clear_parse();

      status("Generating GOTO Program");

      goto_convert(
        context, options, goto_functions,
        ui_message_handler);
    }

    if(process_goto_program(goto_functions))
      return true;
  }

  catch(const char *e)
  {
    error(e);
    return true;
  }

  catch(const std::string e)
  {
    error(e);
    return true;
  }

  catch(int)
  {
    return true;
  }

  return false;
}

/*******************************************************************\

Function: cbmc_parseoptionst::preprocessing

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void cbmc_parseoptionst::preprocessing()
{
  try
  {
    if(cmdline.args.size()!=1)
    {
      error("Please provide one program to preprocess");
      return;
    }

    std::string filename=cmdline.args[0];

    std::ifstream infile(filename.c_str());

    if(!infile)
    {
      error("failed to open input file");
      return;
    }

    if (c_preprocess(infile, filename, std::cout, false, *get_message_handler()))
      error("PREPROCESSING ERROR");
  }

  catch(const char *e)
  {
    error(e);
  }

  catch(const std::string e)
  {
    error(e);
  }

  catch(int)
  {
  }
}

void cbmc_parseoptionst::add_property_monitors(goto_functionst &goto_functions)
{
  std::map<std::string, std::string> strings;

  symbolst::const_iterator it;
  for (it = context.symbols.begin(); it != context.symbols.end(); it++) {
    if (it->first.as_string().find("__ESBMC_property_") != std::string::npos) {
      // Munge back into the shape of an actual string
      std::string str = "";
      forall_operands(iter2, it->second.value) {
        char c = (char)strtol(iter2->value().as_string().c_str(), NULL, 2);
        if (c != 0)
          str += c;
        else
          break;
      }

      strings[it->first.as_string()] = str;
    }
  }

  std::map<std::string, std::pair<std::set<std::string>, expr2tc> > monitors;
  std::map<std::string, std::string>::const_iterator str_it;
  for (str_it = strings.begin(); str_it != strings.end(); str_it++) {
    if (str_it->first.find("$type") == std::string::npos) {
      std::set<std::string> used_syms;
      expr2tc main_expr;
      std::string prop_name = str_it->first.substr(20, std::string::npos);
      main_expr = calculate_a_property_monitor(prop_name, strings, used_syms);
      monitors[prop_name] = std::pair<std::set<std::string>, expr2tc>
                                      (used_syms, main_expr);
    }
  }

  if (monitors.size() == 0)
    return;

  Forall_goto_functions(f_it, goto_functions) {
    goto_functions_templatet<goto_programt>::goto_functiont &func = f_it->second;
    goto_programt &prog = func.body;
    Forall_goto_program_instructions(p_it, prog) {
      add_monitor_exprs(p_it, prog.instructions, monitors);
    }
  }

  return;
}

static void replace_symbol_names(expr2tc &e, std::string prefix, std::map<std::string, std::string> &strings, std::set<std::string> &used_syms)
{

  if (is_symbol2t(e)) {
    symbol2t &thesym = to_symbol2t(e);
    std::string sym = thesym.name.as_string();

// Originally this piece of code renamed all the symbols in the property
// expression to ones specified by the user. However, there's no easy way of
// working out what the full name of a particular symbol you're looking for
// is, so it's unused for the moment.
#if 0
    // Remove leading "c::"
    sym = sym.substr(3, sym.size() - 3);

    sym = prefix + "_" + sym;
    if (strings.find(sym) == strings.end())
      assert(0 && "Missing symbol mapping for property monitor");

    sym = strings[sym];
    e.identifier(sym);
#endif

    used_syms.insert(sym);
  } else {
    Forall_operands2(it, oper_list, e)
      replace_symbol_names(**it, prefix, strings, used_syms);
  }

  return;
}

expr2tc cbmc_parseoptionst::calculate_a_property_monitor(std::string name, std::map<std::string, std::string> &strings, std::set<std::string> &used_syms)
{
  exprt main_expr;
  std::map<std::string, std::string>::const_iterator it;

  namespacet ns(context);
  languagest languages(ns, MODE_C);

  std::string expr_str = strings["c::__ESBMC_property_" + name];
  std::string dummy_str = "";

  languages.to_expr(expr_str, dummy_str, main_expr, ui_message_handler);

  expr2tc new_main_expr;
  migrate_expr(main_expr, new_main_expr);
  replace_symbol_names(new_main_expr, name, strings, used_syms);

  return new_main_expr;
}

void cbmc_parseoptionst::add_monitor_exprs(goto_programt::targett insn, goto_programt::instructionst &insn_list, std::map<std::string, std::pair<std::set<std::string>, expr2tc> >monitors)
{

  // So the plan: we've been handed an instruction, look for assignments to a
  // symbol we're looking for. When we find one, append a goto instruction that
  // re-evaluates a proposition expression. Because there can be more than one,
  // we put re-evaluations in atomic blocks.

  if (!insn->is_assign())
    return;

  code_assign2t &assign = to_code_assign2t(insn->code);

  // XXX - this means that we can't make propositions about things like
  // the contents of an array and suchlike.
  if (!is_symbol2t(assign.target))
    return;

  symbol2t &sym = to_symbol2t(assign.target);

  // Is this actually an assignment that we're interested in?
  std::map<std::string, std::pair<std::set<std::string>, expr2tc> >::const_iterator it;
  std::string sym_name = sym.name.as_string();
  std::set<std::pair<std::string, expr2tc> > triggered;
  for (it = monitors.begin(); it != monitors.end(); it++) {
    if (it->second.first.find(sym_name) == it->second.first.end())
      continue;

    triggered.insert(std::pair<std::string, expr2tc>(it->first, it->second.second));
  }

  if (triggered.empty())
    return;

  goto_programt::instructiont new_insn;

  new_insn.type = ATOMIC_BEGIN;
  new_insn.function = insn->function;
  insn_list.insert(insn, new_insn);

  insn++;

  new_insn.type = ASSIGN;
  std::set<std::pair<std::string, expr2tc> >::const_iterator trig_it;
  for (trig_it = triggered.begin(); trig_it != triggered.end(); trig_it++) {
    std::string prop_name = "c::" + trig_it->first + "_status";
    expr2tc sym = expr2tc(new symbol2t(type_pool.get_bool(), prop_name));
    new_insn.code = expr2tc(new code_assign2t(sym, trig_it->second));
    new_insn.function = insn->function;

    // new_insn location field not set - I believe it gets numbered later.
    insn_list.insert(insn, new_insn);
  }

  type2tc uint32 = type_pool.get_uint(32);
  new_insn.type = ASSIGN;
  new_insn.function = insn->function;
  expr2tc c_expr = expr2tc(new constant_int2t(uint32, BigInt(1)));
  expr2tc ltlsym = expr2tc(new symbol2t(uint32, "c::_ltl2ba_transition_count"));
  expr2tc e = expr2tc(new add2t(uint32, ltlsym, c_expr));

  new_insn.code = expr2tc(new code_assign2t(ltlsym, e));
  insn_list.insert(insn, new_insn);

  new_insn.type = ATOMIC_END;
  new_insn.function = insn->function;
  insn_list.insert(insn, new_insn);

  new_insn.type = FUNCTION_CALL;
  new_insn.function = insn->function;
  type2tc blank_code_type = type2tc(new code_type2t(std::vector<type2tc>(),
                                    type2tc(), std::vector<irep_idt>(), false));
  new_insn.code =
    expr2tc(new code_function_call2t(expr2tc(),
                     expr2tc(new symbol2t(blank_code_type,"c::__ESBMC_yield")),
                     std::vector<expr2tc>()));
  insn_list.insert(insn, new_insn);

  return;
}

#include <symbol.h>

static unsigned int calc_globals_used(const namespacet &ns, const expr2tc &expr)
{

  if (!is_symbol2t(expr)) {
    unsigned int globals = 0;

    forall_operands2(it, oper_list, expr)
      globals += calc_globals_used(ns, **it);

    return globals;
  }

  std::string identifier = to_symbol2t(expr).name.as_string();
  const symbolt &sym = ns.lookup(identifier);

  if (identifier == "c::__ESBMC_alloc" || identifier == "c::__ESBMC_alloc_size")
    return 0;

  if (sym.static_lifetime || sym.type.is_dynamic_set())
    return 1;

  return 0;
}

void cbmc_parseoptionst::print_ileave_points(namespacet &ns,
                             goto_functionst &goto_functions)
{
  bool print_insn;

  forall_goto_functions(fit, goto_functions) {
    forall_goto_program_instructions(pit, fit->second.body) {
      print_insn = false;
      switch (pit->type) {
        case GOTO:
        case ASSUME:
        case ASSERT:
          if (calc_globals_used(ns, pit->guard) > 0)
            print_insn = true;
          break;
        case ASSIGN:
          if (calc_globals_used(ns, pit->code) > 0)
            print_insn = true;
          break;
        case FUNCTION_CALL:
          {
            code_function_call2t deref_code =
              to_code_function_call2t(pit->code);

            if (is_symbol2t(deref_code.function) &&
                to_symbol2t(deref_code.function).name == "c::__ESBMC_yield")
              print_insn = true;
          }
          break;
        default:
          break;
      }

      if (print_insn)
        fit->second.body.output_instruction(ns, pit->function, std::cout, pit, true, false);
    }
  }

  return;
}

/*******************************************************************\

Function: cbmc_parseoptionst::read_goto_binary

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool cbmc_parseoptionst::read_goto_binary(
  goto_functionst &goto_functions)
{
  std::ifstream in(cmdline.getval("binary"), std::ios::binary);

  if(!in)
  {
    error(
      std::string("Failed to open `")+
      cmdline.getval("binary")+
      "'");
    return true;
  }

  ::read_goto_binary(
    in, context, goto_functions, *get_message_handler());

  return false;
}

/*******************************************************************\

Function: cbmc_parseoptionst::process_goto_program

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

static void
relink_calls_from_to(expr2tc &irep, irep_idt from_name, irep_idt to_name)
{

   if (is_symbol2t(irep)) {
    if (to_symbol2t(irep).name == from_name)
      to_symbol2t(irep).name = to_name;

    return;
  } else {
    Forall_operands2(it, oper_list, irep)
      relink_calls_from_to(**it, from_name, to_name);
  }

  return;
}

bool cbmc_parseoptionst::process_goto_program(goto_functionst &goto_functions)
{
  try
  {
    if(cmdline.isset("string-abstraction"))
    {
      string_instrumentation(
        context, *get_message_handler(), goto_functions);
    }

    namespacet ns(context);

    // do partial inlining
    if(!cmdline.isset("inlining"))
      goto_partial_inline(goto_functions, ns, ui_message_handler);

    if(!cmdline.isset("show-features"))
    {
      // add generic checks
      goto_check(ns, options, goto_functions);
    }

    if(cmdline.isset("string-abstraction"))
    {
      status("String Abstraction");
      string_abstraction(context,
        *get_message_handler(), goto_functions);
    }

    status("Pointer Analysis");
    value_set_analysist value_set_analysis(ns);
    value_set_analysis(goto_functions);

    // show it?
    if(cmdline.isset("show-value-sets"))
    {
      show_value_sets(get_ui(), goto_functions, value_set_analysis);
      return true;
    }

    status("Adding Pointer Checks");

    // add pointer checks
    pointer_checks(
      goto_functions, ns, options, value_set_analysis);

    // add failed symbols
    add_failed_symbols(context, ns);

    // add re-evaluations of monitored properties
    add_property_monitors(goto_functions);

    // recalculate numbers, etc.
    goto_functions.update();

    // add loop ids
    goto_functions.compute_loop_numbers();

    if(cmdline.isset("data-races-check"))
    {
      status("Adding Data Race Checks");

      add_race_assertions(
        value_set_analysis,
        context,
        goto_functions);

      value_set_analysis.
        update(goto_functions);
    }

    // show it?
    if(cmdline.isset("show-loops"))
    {
      show_loop_numbers(get_ui(), goto_functions);
      return true;
    }

    if(cmdline.isset("show-features"))
    {
      // add generic checks
      goto_check(ns, options, goto_functions);
      return true;
    }

    if (cmdline.isset("show-ileave-points"))
    {
      print_ileave_points(ns, goto_functions);
      return true;
    }

    // Rename pthread functions depending on whether we're doing deadlock
    // checking or not.
    if (options.get_bool_option("deadlock-check")) {
      goto_functionst::function_mapt::iterator checkit;
      irep_idt mutex_lock("c::pthread_mutex_lock");
      irep_idt lock_check("c::pthread_mutex_lock_check");

      checkit = goto_functions.function_map.begin();
      for (; checkit != goto_functions.function_map.end(); checkit++) {
        goto_programt::instructionst::iterator it =
          checkit->second.body.instructions.begin();
        for (; it != checkit->second.body.instructions.end(); it++) {
          relink_calls_from_to(it->code, mutex_lock, lock_check);
          relink_calls_from_to(it->guard, mutex_lock, lock_check);
        }
      }

      irep_idt cond_wait("c::pthread_cond_wait");
      irep_idt cond_check("c::pthread_cond_wait_check");
      checkit = goto_functions.function_map.begin();
      for (; checkit != goto_functions.function_map.end(); checkit++) {
        goto_programt::instructionst::iterator it =
          checkit->second.body.instructions.begin();
        for (; it != checkit->second.body.instructions.end(); it++) {
          relink_calls_from_to(it->code, cond_wait, cond_check);
          relink_calls_from_to(it->guard, cond_wait, cond_check);
        }
      }
    }

    // show it?
    if(cmdline.isset("show-goto-functions"))
    {
      goto_functions.output(ns, std::cout);
      return true;
    }
  }

  catch(const char *e)
  {
    error(e);
    return true;
  }

  catch(const std::string e)
  {
    error(e);
    return true;
  }

  catch(int)
  {
    return true;
  }

  return false;
}

/*******************************************************************\

Function: cbmc_parseoptionst::do_bmc

  Inputs:

 Outputs:

 Purpose: invoke main modules

\*******************************************************************/

int cbmc_parseoptionst::do_bmc(bmct &bmc1)
{
  bmc1.set_ui(get_ui());

  // do actual BMC

  status("Starting Bounded Model Checking");

  bmc1.run();

#ifndef _WIN32
  if (bmc1.options.get_bool_option("memstats")) {
    int fd = open("/proc/self/status", O_RDONLY);
    sendfile(2, fd, NULL, 100000);
    close(fd);
  }
#endif

  return 0;
}


/*******************************************************************\

Function: cbmc_parseoptionst::help

  Inputs:

 Outputs:

 Purpose: display command line help

\*******************************************************************/

void cbmc_parseoptionst::help()
{
  std::cout <<
    "\n"
    "* * *           ESBMC " ESBMC_VERSION "          * * *\n"
    "\n"
    "Usage:                       Purpose:\n"
    "\n"
    " esbmc [-?] [-h] [--help]      show help\n"
    " esbmc file.c ...              source file names\n"
    "\n"
    "Additonal options:\n\n"
    " --- front-end options ---------------------------------------------------------\n\n"
    " -I path                      set include path\n"
    " -D macro                     define preprocessor macro\n"
    " --preprocess                 stop after preprocessing\n"
    " --inlining                   inlining function calls\n"
    " --program-only               only show program expression\n"
    " --all-claims                 keep all claims\n"
    " --show-loops                 show the loops in the program\n"
    " --show-claims                only show claims\n"
    " --show-vcc                   show the verification conditions\n"
    " --show-features              only show features\n"
    " --document-subgoals          generate subgoals documentation\n"
    " --no-library                 disable built-in abstract C library\n"
    " --binary                     read goto program instead of source code\n"
    " --llvm-metadata Filename     read the metadata file generated by LLVM\n"
    " --little-endian              allow little-endian word-byte conversions\n"
    " --big-endian                 allow big-endian word-byte conversions\n"
    " --16, --32, --64             set width of machine word\n"
    " --version                    show current ESBMC version and exit\n\n"
    " --- BMC options ---------------------------------------------------------------\n\n"
    " --function name              set main function name\n"
    " --claim nr                   only check specific claim\n"
    " --depth nr                   limit search depth\n"
    " --unwind nr                  unwind nr times\n"
    " --unwindset nr               unwind given loop nr times\n"
    " --no-unwinding-assertions    do not generate unwinding assertions\n"
    " --no-slice                   do not remove unused equations\n\n"
    " --- solver configuration ------------------------------------------------------\n\n"
    " --z3-bv                      use Z3 with bit-vector arithmetic\n"
    " --z3-ir                      use Z3 with integer/real arithmetic\n"
    " --eager                      use eager instantiation with Z3\n"
    " --lazy                       use lazy instantiation with Z3 (default)\n"
    " --btor                       output VCCs in BTOR format (experimental)\n"
    " --qf_aufbv                   output VCCs in QF_AUFBV format (experimental)\n"
    " --qf_auflira                 output VCCs in QF_AUFLIRA format (experimental)\n"
    " --outfile Filename           output VCCs in SMT lib format to given file\n\n"
    " --- property checking ---------------------------------------------------------\n\n"
    " --no-assertions              ignore assertions\n"
    " --no-bounds-check            do not do array bounds check\n"
    " --no-div-by-zero-check       do not do division by zero check\n"
    " --no-pointer-check           do not do pointer check\n"
    " --memory-leak-check          enable memory leak check check\n"
    " --overflow-check             enable arithmetic over- and underflow check\n"
    " --deadlock-check             enable global and local deadlock check with mutex\n"
    " --data-races-check           enable data races check\n"
    " --atomicity-check            enable atomicity check at visible assignments\n\n"
    " --- scheduling approaches -----------------------------------------------------\n\n"
    " --schedule                   use schedule recording approach \n"
    " --uw-model                   use under-approximation and widening approach\n"
    " --core-size nr               limit num of assumpts in UW model(experimental)\n"
    " --round-robin                use the round robin scheduling approach\n"
    " --time-slice                 set the time slice of the round robin algorithm \n\n"
    " --- concurrency checking -----------------------------------------------------\n\n"
    " --context-switch nr          limit number of context switches for each thread \n"
    " --state-hashing              enable state-hashing, prunes duplicate states\n"
    " --control-flow-test          enable context switch before control flow tests\n"
    " --no-lock-check              do not do lock acquisition ordering check\n"
    " --no-por                     do not do partial order reduction\n"
#if 0
    " --unsigned-char              make \"char\" unsigned by default\n"
    " --show-symbol-table          show symbol table\n"
    " --show-goto-functions        show goto program\n"
    " --ppc-macos                  set MACOS/PPC architecture\n"
#endif
    #ifdef _WIN32
    " --i386-macos                 set MACOS/I386 architecture\n"
    " --i386-linux                 set Linux/I386 architecture\n"
    " --i386-win32                 set Windows/I386 architecture (default)\n"
    #else
    #ifdef __APPLE__
    " --i386-macos                 set MACOS/I386 architecture (default)\n"
    " --i386-linux                 set Linux/I386 architecture\n"
    " --i386-win32                 set Windows/I386 architecture\n"
    #else
#if 0
    " --i386-macos                 set MACOS/I386 architecture\n"
    " --i386-linux                 set Linux/I386 architecture (default)\n"
    " --i386-win32                 set Windows/I386 architecture\n"
#endif
    #endif
    #endif
//    " --no-arch                    don't set up an architecture\n"
#if 0
    " --arrays-uf-never            never turn arrays into uninterpreted functions\n"
    " --arrays-uf-always           always turn arrays into uninterpreted functions\n"
#endif
#if 0
    " --xml-ui                     use XML-formatted output\n"
    " --int-encoding               encode variables as integers\n"
    " --round-to-nearest           IEEE floating point rounding mode (default)\n"
    " --round-to-plus-inf          IEEE floating point rounding mode\n"
    " --round-to-minus-inf         IEEE floating point rounding mode\n"
    " --round-to-zero              IEEE floating point rounding mode\n"

    " --ecp                        perform equivalence checking of programs\n"
#endif
    "\n --- Miscellaneous options -----------------------------------------------------\n\n"
    " --memlimit                   configure memory limit, of form \"100m\" or \"2g\"\n"
    " --timeout                    configure time limit, integer followed by {s,m,h}\n"
    " --enable-core-dump           don't disable core dump output\n"
    "\n";
}
