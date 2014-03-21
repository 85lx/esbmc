#ifndef _ESBMC_SOLVERS_SMT_CNF_CONV_H_
#define _ESBMC_SOLVERS_SMT_CNF_CONV_H_

#include <solvers/smt/smt_conv.h>
#include "bitblast_conv.h"

class cnf_convt : public sat_iface, public bitblast_convt
{
public:
  cnf_convt(bool int_encoding, const namespacet &_ns, bool is_cpp);
  ~cnf_convt();

  // The API we require:
  virtual void setto(literalt a, bool val) = 0;
  virtual void lcnf(const bvt &bv) = 0;

  // The API we're implementing: all reducing to cnf(), eventually.
  virtual literalt lnot(literalt a);
  virtual literalt lselect(literalt a, literalt b, literalt c);
  virtual literalt lequal(literalt a, literalt b);
  virtual literalt limplies(literalt a, literalt b);
  virtual literalt lxor(literalt a, literalt b);
  virtual literalt lor(literalt a, literalt b);
  virtual literalt land(literalt a, literalt b);
  virtual void gate_xor(literalt a, literalt b, literalt o);
  virtual void gate_or(literalt a, literalt b, literalt o);
  virtual void gate_and(literalt a, literalt b, literalt o);
  virtual void set_equal(literalt a, literalt b);
};

#endif /* _ESBMC_SOLVERS_SMT_CNF_CONV_H_ */
