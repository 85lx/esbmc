#include "solve.h"

#ifdef Z3
#include <solvers/z3/z3_conv.h>
#endif
#include <solvers/smtlib/smtlib_conv.h>

#include <solvers/smt/smt_tuple.h>

// For the purpose of vastly reducing build times:
smt_convt *
create_new_metasmt_minisat_solver(bool int_encoding, bool is_cpp,
                                  const namespacet &ns);
smt_convt *
create_new_metasmt_z3_solver(bool int_encoding, bool is_cpp,
                             const namespacet &ns);
smt_convt *
create_new_metasmt_boolector_solver(bool int_encoding, bool is_cpp,
                                    const namespacet &ns);
smt_convt *
create_new_metasmt_sword_solver(bool int_encoding, bool is_cpp,
                                const namespacet &ns);
smt_convt *
create_new_metasmt_stp_solver(bool int_encoding, bool is_cpp,
                                const namespacet &ns);
smt_convt *
create_new_minisat_solver(bool int_encoding, const namespacet &ns, bool is_cpp,
                          const optionst &opts);
smt_convt *
create_new_mathsat_solver(bool int_encoding, bool is_cpp, const namespacet &ns);
smt_convt *
create_new_cvc_solver(bool int_encoding, bool is_cpp, const namespacet &ns);
smt_convt *
create_new_boolector_solver(bool int_encoding, bool is_cpp,
                            const namespacet &ns, const optionst &options);

static smt_convt *
create_z3_solver(bool is_cpp __attribute__((unused)),
                 bool int_encoding __attribute__((unused)),
                 const namespacet &ns __attribute__((unused)))
{
#ifndef Z3
    std::cerr << "Sorry, Z3 support was not built into this version of ESBMC"
              << std::endl;
    abort();
#else
    return new z3_convt(int_encoding, is_cpp, ns);
#endif
}

static smt_convt *
create_minisat_solver(bool int_encoding __attribute__((unused)),
                      const namespacet &ns __attribute__((unused)),
                      bool is_cpp __attribute__((unused)),
                      const optionst &options __attribute__((unused)))
{
#ifndef MINISAT
    std::cerr << "Sorry, MiniSAT support was not built into this version of "
              "ESBMC" << std::endl;
    abort();
#else
    return create_new_minisat_solver(int_encoding, ns, is_cpp, options);
#endif
}

static smt_convt *
create_metasmt_minisat_solver(bool is_cpp __attribute__((unused)),
                              bool int_encoding __attribute__((unused)),
                              const namespacet &ns __attribute__((unused)))
{
#if !defined(METASMT) || !defined(MINISAT)
    std::cerr << "Sorry, metaSMT minisat support was not built into this "
                 "version of " << "ESBMC" << std::endl;
    abort();
#else
    return create_new_metasmt_minisat_solver(int_encoding, is_cpp, ns);
#endif
}

static smt_convt *
create_metasmt_z3_solver(bool is_cpp __attribute__((unused)),
                         bool int_encoding __attribute__((unused)),
                         const namespacet &ns __attribute__((unused)))
{
#if !defined(METASMT) || !defined(Z3)
    std::cerr << "Sorry, metaSMT Z3 support was not built into this version of "
              << "ESBMC" << std::endl;
    abort();
#else
    return create_new_metasmt_z3_solver(int_encoding, is_cpp, ns);
#endif
}

static smt_convt *
create_metasmt_boolector_solver(bool is_cpp __attribute__((unused)),
                                bool int_encoding __attribute__((unused)),
                                const namespacet &ns __attribute__((unused)))
{
#if !defined(METASMT) || !defined(BOOLECTOR)
    std::cerr << "Sorry, metaSMT Boolector support was not built into this "
              << "version of ESBMC" << std::endl;
    abort();
#else
    return create_new_metasmt_boolector_solver(int_encoding, is_cpp, ns);
#endif
}

static smt_convt *
create_metasmt_sword_solver(bool is_cpp __attribute__((unused)),
                            bool int_encoding __attribute__((unused)),
                            const namespacet &ns __attribute__((unused)))
{
#if !defined(METASMT) || !defined(SWORD)
    std::cerr << "Sorry, SWORD support was not built into this version of ESBMC"
              << std::endl;
    abort();
#else
    return create_new_metasmt_sword_solver(int_encoding, is_cpp, ns);
#endif
}

static smt_convt *
create_metasmt_stp_solver(bool is_cpp __attribute__((unused)),
                          bool int_encoding __attribute__((unused)),
                          const namespacet &ns __attribute__((unused)))
{
#if !defined(METASMT) || !defined(STP)
    std::cerr << "Sorry, STP support was not built into this version of ESBMC"
              << std::endl;
    abort();
#else
    return create_new_metasmt_stp_solver(int_encoding, is_cpp, ns);
#endif
}

static smt_convt *
create_mathsat_solver(bool is_cpp __attribute__((unused)),
                                bool int_encoding __attribute__((unused)),
                                const namespacet &ns __attribute__((unused)))
{
#if !defined(MATHSAT)
    std::cerr << "Sorry, MathSAT support was not built into this "
              << "version of ESBMC" << std::endl;
    abort();
#else
    return create_new_mathsat_solver(int_encoding, is_cpp, ns);
#endif
}

static smt_convt *
create_cvc_solver(bool is_cpp __attribute__((unused)),
                                bool int_encoding __attribute__((unused)),
                                const namespacet &ns __attribute__((unused)))
{
#if !defined(USECVC)
    std::cerr << "Sorry, CVC support was not built into this "
              << "version of ESBMC" << std::endl;
    abort();
#else
    return create_new_cvc_solver(int_encoding, is_cpp, ns);
#endif
}

static smt_convt *
create_boolector_solver(bool is_cpp __attribute__((unused)),
                                bool int_encoding __attribute__((unused)),
                                const namespacet &ns __attribute__((unused)),
                                const optionst &options __attribute__((unused)))
{
#if !defined(BOOLECTOR)
    std::cerr << "Sorry, Boolector support was not built into this "
              << "version of ESBMC" << std::endl;
    abort();
#else
    return create_new_boolector_solver(is_cpp, int_encoding, ns, options);
#endif
}

static const unsigned int num_of_solvers = 9;
static const std::string list_of_solvers[] =
{ "z3", "smtlib", "minisat", "metasmt", "boolector", "sword", "stp", "mathsat", "cvc"};

static smt_convt *
pick_solver(bool is_cpp, bool int_encoding, const namespacet &ns,
            const optionst &options, tuple_iface **tuple_api)
{
  unsigned int i, total_solvers = 0;
  for (i = 0; i < num_of_solvers; i++)
    total_solvers += (options.get_bool_option(list_of_solvers[i])) ? 1 : 0;

  *tuple_api = NULL;

  if (total_solvers == 0) {
    std::cerr << "No solver specified; defaulting to Z3" << std::endl;
  } else if (total_solvers > 1) {
    // Metasmt is one fewer solver.
    if (options.get_bool_option("metasmt") && total_solvers == 2) {
      ;
    } else {
      std::cerr << "Please only specify one solver" << std::endl;
      abort();
    }
  }

  if (options.get_bool_option("smtlib")) {
    return new smtlib_convt(int_encoding, ns, is_cpp, options);
  } else if (options.get_bool_option("mathsat")) {
    return create_mathsat_solver(int_encoding, is_cpp, ns);
  } else if (options.get_bool_option("cvc")) {
    return create_cvc_solver(int_encoding, is_cpp, ns);
  } else if (options.get_bool_option("metasmt")) {
    if (options.get_bool_option("minisat")) {
      return create_metasmt_minisat_solver(is_cpp, int_encoding, ns);
    } else if (options.get_bool_option("z3")) {
      return create_metasmt_z3_solver(is_cpp, int_encoding, ns);
    } else if (options.get_bool_option("boolector")) {
      return create_metasmt_boolector_solver(is_cpp, int_encoding, ns);
    } else if (options.get_bool_option("sword")) {
      return create_metasmt_sword_solver(is_cpp, int_encoding, ns);
    } else if (options.get_bool_option("stp")) {
      return create_metasmt_stp_solver(is_cpp, int_encoding, ns);
    } else {
      std::cerr << "You must specify a backend solver when using the metaSMT "
                << "framework" << std::endl;
      abort();
    }
  } else if (options.get_bool_option("minisat")) {
    return create_minisat_solver(int_encoding, ns, is_cpp, options);
  } else if (options.get_bool_option("boolector")) {
    return create_boolector_solver(is_cpp, int_encoding, ns, options);
  } else {
    z3_convt *cvt =
      static_cast<z3_convt*>(create_z3_solver(is_cpp, int_encoding, ns));
    *tuple_api = static_cast<tuple_iface*>(cvt);
    return cvt;
  }
}

smt_convt *
create_solver_factory1(const std::string &solver_name, bool is_cpp,
                       bool int_encoding, const namespacet &ns,
                       const optionst &options,
                       tuple_iface **tuple_api)
{
  if (solver_name == "")
    // Pick one based on options.
    return pick_solver(is_cpp, int_encoding, ns, options, tuple_api);

  *tuple_api = NULL;

  if (solver_name == "z3") {
    z3_convt *cvt =
      static_cast<z3_convt*>(create_z3_solver(is_cpp, int_encoding, ns));
    *tuple_api = static_cast<tuple_iface*>(cvt);
    return cvt;
  } else if (solver_name == "mathsat") {
    return create_mathsat_solver(int_encoding, is_cpp, ns);
  } else if (solver_name == "cvc") {
    return create_cvc_solver(int_encoding, is_cpp, ns);
  } else if (solver_name == "smtlib") {
    return new smtlib_convt(int_encoding, ns, is_cpp, options);
  } else if (solver_name == "metasmt") {
    if (options.get_bool_option("minisat")) {
      return create_metasmt_minisat_solver(is_cpp, int_encoding, ns);
    } else {
      std::cerr << "You must specify a backend solver when using the metaSMT "
                << "framework" << std::endl;
      abort();
    }
  } else if (options.get_bool_option("minisat")) {
    return create_minisat_solver(int_encoding, ns, is_cpp, options);
  } else if (options.get_bool_option("boolector")) {
    return create_boolector_solver(is_cpp, int_encoding, ns, options);
  } else {
    std::cerr << "Unrecognized solver \"" << solver_name << "\" created"
              << std::endl;
    abort();
  }
}


smt_convt *
create_solver_factory(const std::string &solver_name, bool is_cpp,
                      bool int_encoding, const namespacet &ns,
                      const optionst &options)
{
  tuple_iface *tuple_api = NULL;
  smt_convt *ctx = create_solver_factory1(solver_name, is_cpp, int_encoding, ns, options, &tuple_api);

  bool node_flat = options.get_bool_option("tuple-node-flattener");
  bool sym_flat = options.get_bool_option("tuple-sym-flattener");

  // Pick a tuple flattener to use. If the solver has native support, and no
  // options were given, use that by default
  if (tuple_api != NULL && !node_flat && !sym_flat)
    ctx->set_tuple_iface(tuple_api);
  // Use the node flattener if specified
  else if (node_flat)
    ctx->set_tuple_iface(new smt_tuple_node_flattener(ctx, ns));
  // Use the symbol flattener if specified
  else if (sym_flat)
    ctx->set_tuple_iface(new smt_tuple_sym_flattener(ctx, ns));
  // Default: node flattener
  else
    ctx->set_tuple_iface(new smt_tuple_node_flattener(ctx, ns));

  ctx->smt_post_init();
  return ctx;
}
