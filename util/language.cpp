/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "language.h"

/*******************************************************************\

Function: languaget::from_expr

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool languaget::from_expr(
  const exprt &expr,
  std::string &code,
  const namespacet &ns __attribute__((unused)))
{
  code=expr.pretty();
  return false;
}

/*******************************************************************\

Function: languaget::from_type

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool languaget::from_type(
  const typet &type,
  std::string &code,
  const namespacet &ns __attribute__((unused)))
{
  code=type.pretty();
  return false;
}
