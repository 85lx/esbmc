/*******************************************************************\

Module: Pointer Logic

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_POINTER_OFFSET_SIZE_H
#define CPROVER_POINTER_OFFSET_SIZE_H

#include <mp_arith.h>
#include <expr.h>
#include <namespace.h>
#include <std_types.h>
#include <irep2.h>

mp_integer member_offset(
  const struct_type2t &type,
  const irep_idt &member);

mp_integer pointer_offset_size(const type2t &type);

expr2tc compute_pointer_offset(const expr2tc &expr);

#endif
