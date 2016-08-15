#include "irep2.h"

#include <string.h>

#include <boost/static_assert.hpp>

#include <arith_tools.h>
#include <c_types.h>
#include <base_type.h>
#include <expr_util.h>
#include <type_byte_size.h>

expr2tc
expr2t::do_simplify(bool second __attribute__((unused))) const
{

  return expr2tc();
}

static expr2tc
typecast_check_return(const type2tc &type, expr2tc &expr)
{
  // If the expr is already nil, do nothing
  if(is_nil_expr(expr))
    return expr2tc();

  // Create a typecast of the result
  expr2tc typecast = expr2tc(new typecast2t(type, expr));

  // Try to simplify the typecast
  expr2tc simpl_typecast_res = expr2tc(typecast->do_simplify());

  // If we were able to simplify the typecast, return it
  if(!is_nil_expr(simpl_typecast_res))
    return expr2tc(simpl_typecast_res->clone());

  // Otherwise, return the explicit typecast
  return expr2tc(typecast->clone());
}

static expr2tc
try_simplification(const expr2tc& expr)
{
  expr2tc to_simplify = expr2tc(expr->do_simplify());
  if (is_nil_expr(to_simplify))
    to_simplify = expr2tc(expr->clone());
  return expr2tc(to_simplify->clone());
}

template<template<typename> class TFunctor, typename constructor>
static expr2tc
simplify_arith_2ops(
  const type2tc &type,
  const expr2tc &side_1,
  const expr2tc &side_2)
{
  if(!is_number_type(type) && !is_pointer_type(type))
    return expr2tc();

  // Try to recursively simplify nested operations both sides, if any
  expr2tc simplied_side_1 = try_simplification(side_1);
  expr2tc simplied_side_2 = try_simplification(side_2);

  if (!is_constant_expr(simplied_side_1)
      && !is_constant_expr(simplied_side_2))
  {
    // Were we able to simplify the sides?
    if((side_1 != simplied_side_1) || (side_2 != simplied_side_2))
    {
      expr2tc new_op =
        expr2tc(new constructor(type, simplied_side_1, simplied_side_2));

      return typecast_check_return(type, new_op);
    }

    return expr2tc();
  }

  expr2tc simpl_res = expr2tc();

  if(is_bv_type(simplied_side_1->type) || is_bv_type(simplied_side_2->type))
  {
    std::function<bool(const expr2tc&)> is_constant =
      (bool(*)(const expr2tc&)) &is_constant_int2t;

    std::function<BigInt& (expr2tc&)> get_value =
      [](expr2tc& c) -> BigInt&
        { return to_constant_int2t(c).value; };

    simpl_res =
      TFunctor<BigInt>::simplify(
        simplied_side_1, simplied_side_2, is_constant, get_value);
  }
  else if(is_fixedbv_type(simplied_side_1->type)
          || is_fixedbv_type(simplied_side_2->type))
  {
    std::function<bool(const expr2tc&)> is_constant =
      (bool(*)(const expr2tc&)) &is_constant_fixedbv2t;

    std::function<fixedbvt& (expr2tc&)> get_value =
      [](expr2tc& c) -> fixedbvt&
        { return to_constant_fixedbv2t(c).value; };

    simpl_res =
      TFunctor<fixedbvt>::simplify(
        simplied_side_1, simplied_side_2, is_constant, get_value);
  }
  else if(is_floatbv_type(simplied_side_1->type)
          || is_floatbv_type(simplied_side_2->type))
  {
    std::function<bool(const expr2tc&)> is_constant =
      (bool(*)(const expr2tc&)) &is_constant_floatbv2t;

    std::function<ieee_floatt& (expr2tc&)> get_value =
      [](expr2tc& c) -> ieee_floatt&
        { return to_constant_floatbv2t(c).value; };

    simpl_res =
      TFunctor<ieee_floatt>::simplify(
        simplied_side_1, simplied_side_2, is_constant, get_value);
  }
  else if(is_bool_type(simplied_side_1->type)
          || is_bool_type(simplied_side_2->type))
  {
    std::function<bool(const expr2tc&)> is_constant =
      (bool(*)(const expr2tc&)) &is_constant_bool2t;

    std::function<bool& (expr2tc&)> get_value =
      [](expr2tc& c) -> bool&
        { return to_constant_bool2t(c).value; };

    simpl_res =
      TFunctor<bool>::simplify(
        simplied_side_1, simplied_side_2, is_constant, get_value);
  }

  return typecast_check_return(type, simpl_res);
}

template<class constant_type>
struct Addtor
{
  static expr2tc simplify(
    expr2tc &op1,
    expr2tc &op2,
    std::function<bool(const expr2tc&)> is_constant,
    std::function<constant_type&(expr2tc&)> get_value)
  {
    // Two constants? Simplify to result of the addition
    if (is_constant(op1) && is_constant(op2))
    {
      auto c = expr2tc(op1->clone());
      get_value(c) += get_value(op2);
      return expr2tc(c);
    }

    if(is_constant(op1))
    {
      // Found a zero? Simplify to op2
      if(get_value(op1) == 0)
        return expr2tc(op2->clone());
    }

    if(is_constant(op2))
    {
      // Found a zero? Simplify to op1
      if(get_value(op2) == 0)
        return expr2tc(op1->clone());
    }

    return expr2tc();
  }
};

expr2tc
add2t::do_simplify(bool __attribute__((unused))) const
{
  return simplify_arith_2ops<Addtor, add2t>(type, side_1, side_2);
}

expr2tc
sub2t::do_simplify(bool second __attribute__((unused))) const
{
  // rewrite "a-b" to "a+(-b)" and call simplify add
  expr2tc neg = expr2tc(new neg2t(type, side_2->clone()));
  expr2tc new_add = expr2tc(new add2t(type, side_1->clone(), neg->clone()));

  return expr2tc(new_add->do_simplify());
}

template<class constant_type>
struct Multor
{
  static expr2tc simplify(
    expr2tc &op1,
    expr2tc &op2,
    std::function<bool(const expr2tc&)> is_constant,
    std::function<constant_type&(expr2tc&)> get_value)
  {
    // Two constants? Simplify to result of the multiplication
    if (is_constant(op1) && is_constant(op2))
    {
      auto c = expr2tc(op1->clone());
      get_value(c) *= get_value(op2);
      return expr2tc(c);
    }

    if(is_constant(op1))
    {
      // Found a zero? Simplify to zero
      if(get_value(op1) == 0)
      {
        auto c = expr2tc(op1->clone());
        get_value(c) = constant_type();
        return expr2tc(c);
      }

      // Found an one? Simplify to op2
      if(get_value(op1) == 1)
        return expr2tc(op2->clone());
    }

    if(is_constant(op2))
    {
      // Found a zero? Simplify to zero
      if(get_value(op2) == 0)
      {
        auto c = expr2tc(op2->clone());
        get_value(c) = constant_type();
        return expr2tc(c);
      }

      // Found an one? Simplify to op1
      if(get_value(op2) == 1)
        return expr2tc(op1->clone());
    }

    return expr2tc();
  }
};

expr2tc
mul2t::do_simplify(bool second __attribute__((unused))) const
{
  return simplify_arith_2ops<Multor, mul2t>(type, side_1, side_2);
}

template<class constant_type>
struct Divtor
{
  static expr2tc simplify(
    expr2tc &numerator,
    expr2tc &denominator,
    std::function<bool(const expr2tc&)> is_constant,
    std::function<constant_type&(expr2tc&)> get_value)
  {
    // Two constants? Simplify to result of the division
    if (is_constant(numerator) && is_constant(denominator))
    {
      auto c = expr2tc(numerator->clone());
      get_value(c) /= get_value(denominator);
      return expr2tc(c);
    }

    if(is_constant(numerator))
    {
      // Numerator is zero? Simplify to zero
      if(get_value(numerator) == 0)
      {
        auto c = expr2tc(numerator->clone());
        get_value(c) = constant_type();
        return expr2tc(c);
      }
    }

    if(is_constant(denominator))
    {
      // Denominator is zero? Don't simplify
      if(get_value(denominator) == 0)
        return expr2tc();

      // Denominator is one? Simplify to numerator's constant
      if(get_value(denominator) == 1)
        return expr2tc(numerator->clone());
    }

    return expr2tc();
  }
};

expr2tc
div2t::do_simplify(bool second __attribute__((unused))) const
{
  return simplify_arith_2ops<Divtor, div2t>(type, side_1, side_2);
}

expr2tc
modulus2t::do_simplify(bool second __attribute__((unused))) const
{

  if(!is_number_type(type))
    return expr2tc();

  // Try to recursively simplify nested operations both sides, if any
  expr2tc simplied_side_1 = try_simplification(side_1);
  expr2tc simplied_side_2 = try_simplification(side_2);

  if (!is_constant_expr(simplied_side_1)
      || !is_constant_expr(simplied_side_2))
  {
    // Were we able to simplify the sides?
    if((side_1 != simplied_side_1) || (side_2 != simplied_side_2))
    {
      expr2tc new_mod =
        expr2tc(new modulus2t(type, simplied_side_1, simplied_side_2));

      return typecast_check_return(type, new_mod);
    }

    return expr2tc();
  }

  if(is_bv_type(type))
  {
    const constant_int2t &numerator = to_constant_int2t(simplied_side_1);
    const constant_int2t &denominator = to_constant_int2t(simplied_side_2);

    auto c = numerator.value;
    c %= denominator.value;

    return expr2tc(new constant_int2t(type, c));
  }

  return expr2tc();
}

template<template<typename> class TFunctor, typename constructor>
static expr2tc
simplify_arith_1op(
  const type2tc &type,
  const expr2tc &value)
{
  if(!is_number_type(type))
    return expr2tc();

  // Try to recursively simplify nested operation, if any
  expr2tc to_simplify = try_simplification(value);
  if (!is_constant_expr(to_simplify))
  {
    // Were we able to simplify anything?
    if(value != to_simplify)
    {
      expr2tc new_neg = expr2tc(new constructor(type, to_simplify));
      return typecast_check_return(type, new_neg);
    }

    return expr2tc();
  }

  expr2tc simpl_res = expr2tc();

  if(is_bv_type(value->type))
  {
    std::function<constant_int2t& (expr2tc&)> to_constant =
      (constant_int2t&(*)(expr2tc&)) to_constant_int2t;

    simpl_res =
      TFunctor<constant_int2t>::simplify(to_simplify, to_constant);
  }
  else if(is_fixedbv_type(value->type))
  {
    std::function<constant_fixedbv2t& (expr2tc&)> to_constant =
      (constant_fixedbv2t&(*)(expr2tc&)) to_constant_fixedbv2t;

    simpl_res =
      TFunctor<constant_fixedbv2t>::simplify(to_simplify, to_constant);
  }
  else if(is_floatbv_type(value->type))
  {
    std::function<constant_floatbv2t& (expr2tc&)> to_constant =
      (constant_floatbv2t&(*)(expr2tc&)) to_constant_floatbv2t;

    simpl_res =
      TFunctor<constant_floatbv2t>::simplify(to_simplify, to_constant);
  }
  else if(is_bool_type(value->type))
  {
    std::function<constant_bool2t& (expr2tc&)> to_constant =
      (constant_bool2t&(*)(expr2tc&)) to_constant_bool2t;

    simpl_res =
      TFunctor<constant_bool2t>::simplify(to_simplify, to_constant);
  }

  return typecast_check_return(type, simpl_res);
}

template<class constant_type>
struct Negator
{
  static expr2tc simplify(
    const expr2tc &number,
    std::function<constant_type&(expr2tc&)> to_constant)
  {
    auto c = expr2tc(number->clone());
    to_constant(c).value = !to_constant(c).value;
    return expr2tc(c);
  }
};

expr2tc
neg2t::do_simplify(bool second __attribute__((unused))) const
{
  return simplify_arith_1op<Negator, neg2t>(type, value);
}

template<class constant_type>
struct abstor
{
  static expr2tc simplify(
    const expr2tc &number,
    std::function<constant_type&(expr2tc&)> to_constant)
  {
    auto c = expr2tc(number->clone());

    // When we call BigInt/fixedbv/floatbv constructor
    // with no argument, it generates the zero equivalent
    if(to_constant(c).value > 0)
      return expr2tc();

    to_constant(c).value = !to_constant(c).value;
    return expr2tc(c);
  }
};

expr2tc
abs2t::do_simplify(bool second __attribute__((unused))) const
{
  return simplify_arith_1op<abstor, abs2t>(type, value);
}

expr2tc
with2t::do_simplify(bool second __attribute__((unused))) const
{

  if (is_constant_struct2t(source_value)) {
    const constant_struct2t &c_struct = to_constant_struct2t(source_value);
    const constant_string2t &memb = to_constant_string2t(update_field);
    unsigned no = static_cast<const struct_union_data&>(*type.get())
                  .get_component_number(memb.value);
    assert(no < c_struct.datatype_members.size());

    // Clone constant struct, update its field according to this "with".
    constant_struct2tc s = expr2tc(c_struct.clone());
    s.get()->datatype_members[no] = update_value;
    return expr2tc(s);
  } else if (is_constant_union2t(source_value)) {
    const constant_union2t &c_union = to_constant_union2t(source_value);
    const union_type2t &thetype = to_union_type(c_union.type);
    const constant_string2t &memb = to_constant_string2t(update_field);
    unsigned no = static_cast<const struct_union_data&>(*c_union.type.get())
                  .get_component_number(memb.value);
    assert(no < thetype.member_names.size());

    // If the update value type matches the current lump of data's type, we can
    // just replace it with the new value. As far as I can tell, constant unions
    // only ever contain one member, and it's the member most recently written.
    if (thetype.members[no] != update_value->type)
      return expr2tc();

    std::vector<expr2tc> newmembers;
    newmembers.push_back(update_value);
    return expr2tc(new constant_union2t(type, newmembers));
  } else if (is_constant_array2t(source_value) &&
             is_constant_int2t(update_field)) {
    const constant_array2t &array = to_constant_array2t(source_value);
    const constant_int2t &index = to_constant_int2t(update_field);

    // Index may be out of bounds. That's an error in the program, but not in
    // the model we're generating, so permit it. Can't simplify it though.
    if (index.as_ulong() >= array.datatype_members.size())
      return expr2tc();

    constant_array2tc arr = expr2tc(array.clone());
    arr.get()->datatype_members[index.as_ulong()] = update_value;
    return expr2tc(arr);
  } else if (is_constant_array_of2t(source_value)) {
    const constant_array_of2t &array = to_constant_array_of2t(source_value);

    // We don't simplify away these withs if // the array_of is infinitely
    // sized. This is because infinitely sized arrays are no longer converted
    // correctly in the solver backend (they're simply not supported by SMT).
    // Thus it becomes important to be able to assign a value to a field in an
    // aray_of and not have it const propagatated away.
    const constant_array_of2t &thearray = to_constant_array_of2t(source_value);
    const array_type2t &arr_type = to_array_type(thearray.type);
    if (arr_type.size_is_infinite)
      return expr2tc();

    // We can eliminate this operation if the operand to this with is the same
    // as the initializer.
    if (update_value == array.initializer)
      return source_value;
    else
      return expr2tc();
  } else {
    return expr2tc();
  }
}

expr2tc
member2t::do_simplify(bool second __attribute__((unused))) const
{

  if (is_constant_struct2t(source_value) || is_constant_union2t(source_value)) {
    unsigned no =
      static_cast<const struct_union_data&>(*source_value->type.get())
      .get_component_number(member);

    // Clone constant struct, update its field according to this "with".
    expr2tc s;
    if (is_constant_struct2t(source_value)) {
      s = to_constant_struct2t(source_value).datatype_members[no];

      assert(is_pointer_type(type) ||
             base_type_eq(type, s->type, namespacet(contextt())));
    } else {
      // The constant array has some number of elements, up to the size of the
      // array, but possibly fewer. This is legal C. So bounds check first that
      // we can actually perform this member operation.
      const constant_union2t &uni = to_constant_union2t(source_value);
      if (uni.datatype_members.size() <= no)
        return expr2tc();

      s = uni.datatype_members[no];

      // If the type we just selected isn't compatible, it means that whatever
      // field is in the constant union /isn't/ the field we're selecting from
      // it. So don't simplify it, because we can't.
      if (!is_pointer_type(type) &&
          !base_type_eq(type, s->type, namespacet(contextt())))
        return expr2tc();
    }


    return s;
  } else {
    return expr2tc();
  }
}

expr2tc
pointer_offs_simplify_2(const expr2tc &offs, const type2tc &type)
{

  if (is_symbol2t(offs) || is_constant_string2t(offs)) {
    return expr2tc(new constant_int2t(type, BigInt(0)));
  } else if (is_index2t(offs)) {
    const index2t &index = to_index2t(offs);

    if (is_symbol2t(index.source_value) && is_constant_int2t(index.index)) {
      // We can reduce to that index offset.
      const array_type2t &arr = to_array_type(index.source_value->type);
      unsigned int widthbits = arr.subtype->get_width();
      unsigned int widthbytes = widthbits / 8;
      BigInt val = to_constant_int2t(index.index).value;
      val *= widthbytes;
      return expr2tc(new constant_int2t(type, val));
    } else if (is_constant_string2t(index.source_value) &&
               is_constant_int2t(index.index)) {
      // This can also be simplified to an array offset. Just return the index,
      // as the string elements are all 8 bit bytes.
      return index.index;
    } else {
      return expr2tc();
    }
  } else {
    return expr2tc();
  }
}

expr2tc
pointer_offset2t::do_simplify(bool second) const
{

  // XXX - this could be better. But the current implementation catches most
  // cases that ESBMC produces internally.

  if (second && is_address_of2t(ptr_obj)) {
    const address_of2t &addrof = to_address_of2t(ptr_obj);
    return pointer_offs_simplify_2(addrof.ptr_obj, type);
  } else if (is_typecast2t(ptr_obj)) {
    const typecast2t &cast = to_typecast2t(ptr_obj);
    expr2tc new_ptr_offs = expr2tc(new pointer_offset2t(type, cast.from));
    expr2tc reduced = new_ptr_offs->simplify();

    // No good simplification -> return nothing
    if (is_nil_expr(reduced))
      return reduced;

    // If it simplified to zero, that's fine, return that.
    if (is_constant_int2t(reduced) &&
        to_constant_int2t(reduced).value.is_zero())
      return reduced;

    // If it didn't reduce to zero, give up. Not sure why this is the case,
    // but it's what the old irep code does.
    return expr2tc();
  } else if (is_add2t(ptr_obj)) {
    const add2t &add = to_add2t(ptr_obj);

    // So, one of these should be a ptr type, or there isn't any point in this
    // being a pointer_offset irep.
    if (!is_pointer_type(add.side_1) &&
        !is_pointer_type(add.side_2))
      return expr2tc();

    // Can't have pointer-on-pointer arith.
    assert(!(is_pointer_type(add.side_1) &&
             is_pointer_type(add.side_2)));

    expr2tc ptr_op = (is_pointer_type(add.side_1)) ? add.side_1 : add.side_2;
    expr2tc non_ptr_op =
      (is_pointer_type(add.side_1)) ? add.side_2 : add.side_1;

    // Can't do any kind of simplification if the ptr op has a symbolic type.
    // Let the SMT layer handle this. In the future, can we pass around a
    // namespace?
    if (is_symbol_type(to_pointer_type(ptr_op->type).subtype))
      return expr2tc();

    // Turn the pointer one into pointer_offset.
    expr2tc new_ptr_op = expr2tc(new pointer_offset2t(type, ptr_op));
    // And multiply the non pointer one by the type size.
    type2tc ptr_int_type = get_int_type(config.ansi_c.pointer_width);
    type2tc ptr_subtype = to_pointer_type(ptr_op->type).subtype;
    mp_integer thesize = (is_empty_type(ptr_subtype)) ? 1
                          : type_byte_size(*ptr_subtype.get());
    constant_int2tc type_size(type, thesize);

    // SV-Comp workaround
    if (non_ptr_op->type->get_width() != type->get_width())
      non_ptr_op = typecast2tc(type, non_ptr_op);

    mul2tc new_non_ptr_op(type, non_ptr_op, type_size);

    expr2tc new_add = expr2tc(new add2t(type, new_ptr_op, new_non_ptr_op));

    // So, this add is a valid simplification. We may be able to simplify
    // further though.
    expr2tc tmp = new_add->simplify();
    if (is_nil_expr(tmp))
      return new_add;
    else
      return tmp;
  } else {
    return expr2tc();
  }
}

expr2tc
index2t::do_simplify(bool second __attribute__((unused))) const
{

  if (is_with2t(source_value)) {
    if (index == to_with2t(source_value).update_field) {
      // Index is the same as an update to the thing we're indexing; we can
      // just take the update value from the "with" below.
      return to_with2t(source_value).update_value;
    }

    return expr2tc();
  } else if (is_constant_array2t(source_value) && is_constant_int2t(index)) {
    const constant_array2t &arr = to_constant_array2t(source_value);
    const constant_int2t &idx = to_constant_int2t(index);

    // Index might be greater than the constant array size. This means we can't
    // simplify it, and the user might be eaten by an assertion failure in the
    // model. We don't have to think about this now though.
    if (idx.value.is_negative())
      return expr2tc();

    unsigned long the_idx = idx.as_ulong();
    if (the_idx >= arr.datatype_members.size())
      return expr2tc();

    return arr.datatype_members[the_idx];
  } else if (is_constant_string2t(source_value) && is_constant_int2t(index)) {
    const constant_string2t &str = to_constant_string2t(source_value);
    const constant_int2t &idx = to_constant_int2t(index);

    // Same index situation
    unsigned long the_idx = idx.as_ulong();
    if (the_idx > str.value.as_string().size()) // allow reading null term.
      return expr2tc();

    // String constants had better be some kind of integer type
    assert(is_bv_type(type));
    unsigned long val = str.value.as_string().c_str()[the_idx];
    return expr2tc(new constant_int2t(type, BigInt(val)));
  } else if (is_constant_array_of2t(source_value)) {
    // Only thing this index can evaluate to is the default value of this array
    return to_constant_array_of2t(source_value).initializer;
  } else {
    return expr2tc();
  }
}

expr2tc
not2t::do_simplify(bool second __attribute__((unused))) const
{

  if (is_not2t(value))
    // These negate.
    return to_not2t(value).value;

  if (!is_constant_bool2t(value))
    return expr2tc();

  const constant_bool2t &val = to_constant_bool2t(value);
  return expr2tc(new constant_bool2t(!val.value));
}

template<template<typename> class TFunctor, typename constructor>
static expr2tc
simplify_logic_2ops(
  const type2tc &type,
  const expr2tc &side_1,
  const expr2tc &side_2)
{
  if(!is_number_type(type))
    return expr2tc();

  // Try to recursively simplify nested operations both sides, if any
  expr2tc simplied_side_1 = try_simplification(side_1);
  expr2tc simplied_side_2 = try_simplification(side_2);

  if (!is_constant_expr(simplied_side_1) && !is_constant_expr(simplied_side_2))
  {
    // Were we able to simplify the sides?
    if((side_1 != simplied_side_1) || (side_2 != simplied_side_2))
    {
      expr2tc new_op =
        expr2tc(new constructor(simplied_side_1, simplied_side_2));

      return typecast_check_return(type, new_op);
    }

    return expr2tc();
  }

  expr2tc simpl_res = expr2tc();

  if(is_bv_type(simplied_side_1->type) || is_bv_type(simplied_side_2->type))
  {
    std::function<bool(const expr2tc&)> is_constant =
      (bool(*)(const expr2tc&)) &is_constant_int2t;

    std::function<BigInt& (expr2tc&)> get_value =
      [](expr2tc& c) -> BigInt&
        { return to_constant_int2t(c).value; };

    simpl_res =
      TFunctor<BigInt>::simplify(
        simplied_side_1, simplied_side_2, is_constant, get_value);
  }
  else if(is_fixedbv_type(simplied_side_1->type)
          || is_fixedbv_type(simplied_side_2->type))
  {
    std::function<bool(const expr2tc&)> is_constant =
      (bool(*)(const expr2tc&)) &is_constant_fixedbv2t;

    std::function<fixedbvt& (expr2tc&)> get_value =
      [](expr2tc& c) -> fixedbvt&
        { return to_constant_fixedbv2t(c).value; };

    simpl_res =
      TFunctor<fixedbvt>::simplify(
        simplied_side_1, simplied_side_2, is_constant, get_value);
  }
  else if(is_floatbv_type(simplied_side_1->type)
          || is_floatbv_type(simplied_side_2->type))
  {
    std::function<bool(const expr2tc&)> is_constant =
      (bool(*)(const expr2tc&)) &is_constant_floatbv2t;

    std::function<ieee_floatt& (expr2tc&)> get_value =
      [](expr2tc& c) -> ieee_floatt&
        { return to_constant_floatbv2t(c).value; };

    simpl_res =
      TFunctor<ieee_floatt>::simplify(
        simplied_side_1, simplied_side_2, is_constant, get_value);
  }
  else if(is_bool_type(simplied_side_1->type)
          || is_bool_type(simplied_side_2->type))
  {
    std::function<bool(const expr2tc&)> is_constant =
      (bool(*)(const expr2tc&)) &is_constant_bool2t;

    std::function<bool& (expr2tc&)> get_value =
      [](expr2tc& c) -> bool&
        { return to_constant_bool2t(c).value; };

    simpl_res =
      TFunctor<bool>::simplify(
        simplied_side_1, simplied_side_2, is_constant, get_value);
  }

  return typecast_check_return(type, simpl_res);
}

template<class constant_type>
struct Andtor
{
  static expr2tc simplify(
    expr2tc &op1,
    expr2tc &op2,
    std::function<bool(const expr2tc&)> is_constant,
    std::function<constant_type&(expr2tc&)> get_value)
  {
    // Two constants? Simplify to result of the and
    if (is_constant(op1) && is_constant(op2))
      return expr2tc(
        new constant_bool2t(!(get_value(op1) == 0) && !(get_value(op2) == 0)));

    if(is_constant(op1))
    {
      // False? never true
      if(get_value(op1) == 0)
        return expr2tc(op1->clone());
      else
        // constant true; other operand determines truth
        return expr2tc(op2->clone());
    }

    if(is_constant(op2))
    {
      // False? never true
      if(get_value(op2) == 0)
        return expr2tc(op2->clone());
      else
        // constant true; other operand determines truth
        return expr2tc(op1->clone());
    }

    return expr2tc();
  }
};

expr2tc
and2t::do_simplify(bool second __attribute__((unused))) const
{
  return simplify_logic_2ops<Andtor, and2t>(type, side_1, side_2);
}

template<class constant_type>
struct Ortor
{
  static expr2tc simplify(
    expr2tc &op1,
    expr2tc &op2,
    std::function<bool(const expr2tc&)> is_constant,
    std::function<constant_type&(expr2tc&)> get_value)
  {
    // Two constants? Simplify to result of the or
    if (is_constant(op1) && is_constant(op2))
      return expr2tc(
        new constant_bool2t(!(get_value(op1) == 0) || !(get_value(op2) == 0)));

    if(is_constant(op1))
    {
      // True? Simplify to op2
      if(!(get_value(op1) == 0))
        return true_expr;
    }

    if(is_constant(op2))
    {
      // True? Simplify to op1
      if(!(get_value(op2) == 0))
        return true_expr;
    }

    return expr2tc();
  }
};

expr2tc
or2t::do_simplify(bool second __attribute__((unused))) const
{
  return simplify_logic_2ops<Ortor, or2t>(type, side_1, side_2);
}

template<class constant_type>
struct Xortor
{
  static expr2tc simplify(
    expr2tc &op1,
    expr2tc &op2,
    std::function<bool(const expr2tc&)> is_constant,
    std::function<constant_type&(expr2tc&)> get_value)
  {
    // Two constants? Simplify to result of the xor
    if (is_constant(op1) && is_constant(op2))
      return expr2tc(
        new constant_bool2t(!(get_value(op1) == 0) ^ !(get_value(op2) == 0)));

    if(is_constant(op1))
    {
      // False? Simplify to op2
      if(get_value(op1) == 0)
        return expr2tc(op2->clone());
    }

    if(is_constant(op2))
    {
      // False? Simplify to op1
      if(get_value(op2) == 0)
        return expr2tc(op1->clone());
    }

    return expr2tc();
  }
};

expr2tc
xor2t::do_simplify(bool second __attribute__((unused))) const
{
  return simplify_logic_2ops<Xortor, xor2t>(type, side_1, side_2);
}

template<class constant_type>
struct Impliestor
{
  static expr2tc simplify(
    expr2tc &op1,
    expr2tc &op2,
    std::function<bool(const expr2tc&)> is_constant,
    std::function<constant_type&(expr2tc&)> get_value)
  {
    // False => * evaluate to true, always
    if(is_constant(op1) && (get_value(op1) == 0))
      return true_expr;

    // Otherwise, the only other thing that will make this expr always true is
    // if side 2 is true.
    if(is_constant(op2) && !(get_value(op2) == 0))
      return true_expr;

    return expr2tc();
  }
};

expr2tc
implies2t::do_simplify(bool second __attribute__((unused))) const
{
  return simplify_logic_2ops<Impliestor, implies2t>(type, side_1, side_2);
}

template<typename constructor>
static expr2tc
do_bit_munge_operation(
  std::function<int64_t(int64_t, int64_t)> opfunc,
  const type2tc &type,
  const expr2tc &side_1,
  const expr2tc &side_2)
{
  // Try to recursively simplify nested operations both sides, if any
  expr2tc simplied_side_1 = try_simplification(side_1);
  expr2tc simplied_side_2 = try_simplification(side_2);

  if (!is_constant_expr(simplied_side_1)
      && !is_constant_expr(simplied_side_2))
  {
    // Were we able to simplify the sides?
    if((side_1 != simplied_side_1) || (side_2 != simplied_side_2))
    {
      expr2tc new_op =
        expr2tc(new constructor(type, simplied_side_1, simplied_side_2));

      return typecast_check_return(type, new_op);
    }

    return expr2tc();
  }

  // Only support integer and's. If you're a float, pointer, or whatever, you're
  // on your own.
  if (!is_constant_int2t(side_1) || !is_constant_int2t(side_2))
    return expr2tc();

  // So - we can't make BigInt by itself do an and operation. But we can dump
  // it to a binary representation, and then and that.
  const constant_int2t &int1 = to_constant_int2t(side_1);
  const constant_int2t &int2 = to_constant_int2t(side_2);

  // Drama: BigInt does *not* do any kind of twos compliment representation.
  // In fact, negative numbers are stored as positive integers, but marked as
  // being negative. To get around this, perform operations in an {u,}int64,
  if (((int1.value.get_len() * sizeof(BigInt::onedig_t)) > sizeof(int64_t))
      || ((int2.value.get_len() * sizeof(BigInt::onedig_t)) > sizeof(int64_t)))
    return expr2tc();

  // Dump will zero-prefix and right align the output number.
  int64_t val1 = int1.value.to_int64();
  int64_t val2 = int2.value.to_int64();

  if (int1.value.is_negative()) {
    if (val1 & 0x8000000000000000ULL) {
      // Too large to fit, negative, in an int64_t.
      return expr2tc();
    } else {
      val1 = -val1;
    }
  }

  if (int2.value.is_negative()) {
    if (val2 & 0x8000000000000000ULL) {
      // Too large to fit, negative, in an int64_t.
      return expr2tc();
    } else {
      val2 = -val2;
    }
  }

  val1 = opfunc(val1, val2);

  // This has potentially become negative. Check the top bit.
  if (val1 & (1 << (type->get_width() - 1)) && is_signedbv_type(type)) {
    // Sign extend.
    val1 |= -1LL << (type->get_width());
  }

  // And now, restore, paying attention to whether this is supposed to be
  // signed or not.
  constant_int2t *theint;
  if (is_signedbv_type(type))
    theint = new constant_int2t(type, BigInt(val1));
  else
    theint = new constant_int2t(type, BigInt((uint64_t)val1));

  return expr2tc(theint);
}

expr2tc
bitand2t::do_simplify(bool second __attribute__((unused))) const
{
  std::function<int64_t(int64_t, int64_t)> op =
    [] (int64_t op1, int64_t op2)
      { return (op1 & op2); };

  return do_bit_munge_operation<bitand2t>(op, type, side_1, side_2);
}

expr2tc
bitor2t::do_simplify(bool second __attribute__((unused))) const
{
  std::function<int64_t(int64_t, int64_t)> op =
    [] (int64_t op1, int64_t op2)
      { return (op1 | op2); };

  return do_bit_munge_operation<bitor2t>(op, type, side_1, side_2);
}

expr2tc
bitxor2t::do_simplify(bool second __attribute__((unused))) const
{
  std::function<int64_t(int64_t, int64_t)> op =
    [] (int64_t op1, int64_t op2)
      { return (op1 ^ op2); };

  return do_bit_munge_operation<bitxor2t>(op, type, side_1, side_2);
}

expr2tc
bitnand2t::do_simplify(bool second __attribute__((unused))) const
{
  std::function<int64_t(int64_t, int64_t)> op =
    [] (int64_t op1, int64_t op2)
      { return ~(op1 & op2); };

  return do_bit_munge_operation<bitnand2t>(op, type, side_1, side_2);
}

expr2tc
bitnor2t::do_simplify(bool second __attribute__((unused))) const
{
  std::function<int64_t(int64_t, int64_t)> op =
    [] (int64_t op1, int64_t op2)
      { return ~(op1 | op2); };

  return do_bit_munge_operation<bitnor2t>(op, type, side_1, side_2);
}

expr2tc
bitnxor2t::do_simplify(bool second __attribute__((unused))) const
{
  std::function<int64_t(int64_t, int64_t)> op =
    [] (int64_t op1, int64_t op2)
      { return ~(op1 ^ op2); };

  return do_bit_munge_operation<bitnxor2t>(op, type, side_1, side_2);
}

expr2tc
bitnot2t::do_simplify(bool second __attribute__((unused))) const
{
  std::function<int64_t(int64_t, int64_t)> op =
    [] (int64_t op1, int64_t op2 __attribute__((unused)))
      { return ~(op1); };

  return do_bit_munge_operation<bitnot2t>(op, type, value, value);
}

expr2tc
shl2t::do_simplify(bool second __attribute__((unused))) const
{
  std::function<int64_t(int64_t, int64_t)> op =
    [] (int64_t op1, int64_t op2)
      { return (op1 << op2); };

  return do_bit_munge_operation<shl2t>(op, type, side_1, side_2);
}

expr2tc
lshr2t::do_simplify(bool second __attribute__((unused))) const
{
  std::function<int64_t(int64_t, int64_t)> op =
    [] (int64_t op1, int64_t op2)
      { return ((uint64_t)op1) >> ((uint64_t)op2); };

  return do_bit_munge_operation<lshr2t>(op, type, side_1, side_2);
}

expr2tc
ashr2t::do_simplify(bool second __attribute__((unused))) const
{
  std::function<int64_t(int64_t, int64_t)> op =
    [] (int64_t op1, int64_t op2)
      { return (op1 >> op2); };

  return do_bit_munge_operation<ashr2t>(op, type, side_1, side_2);
}

expr2tc
typecast2t::do_simplify(bool second) const
{

  // Follow approach of old irep, i.e., copy it
  if (type == from->type)
  {
    // Typecast to same type means this can be eliminated entirely
    return from;
  }
  else if (is_constant_expr(from))
  {
    // Casts from constant operands can be done here.
    if (is_constant_bool2t(from) && is_bv_type(type))
    {
      // int/float/double to bool
      if (to_constant_bool2t(from).value)
        return expr2tc(new constant_int2t(type, BigInt(1)));
      else
        return expr2tc(new constant_int2t(type, BigInt(0)));
    }
    else if (is_number_type(from) && is_bool_type(type))
    {
      // bool to int/float/double
      if(is_bv_type(from))
      {
        const constant_int2t &theint = to_constant_int2t(from);
        if(theint.value.is_zero())
          return false_expr;
        else
          return true_expr;
      }
      else if(is_fixedbv_type(from))
      {
        const constant_fixedbv2t &fbv = to_constant_fixedbv2t(from);
        if(fbv.value.is_zero())
          return false_expr;
        else
          return true_expr;
      }
      else if(is_floatbv_type(from))
      {
        const constant_floatbv2t &fbv = to_constant_floatbv2t(from);
        if(fbv.value.is_zero())
          return false_expr;
        else
          return true_expr;
      }
    }
    else if (is_bv_type(from) && is_number_type(type))
    {
      // int to int/float/double
      const constant_int2t &theint = to_constant_int2t(from);

      if(is_bv_type(type))
      {
        // If we are typecasting from integer to a smaller integer,
        // this will return the number with the smaller size
        exprt number =
          from_integer(theint.value, migrate_type_back(type));

        BigInt new_number;
        if(to_integer(number, new_number))
          return expr2tc();

        return expr2tc(new constant_int2t(type, new_number));
      }
      else if(is_fixedbv_type(type))
      {
        fixedbvt fbv;
        fbv.spec = to_fixedbv_type(migrate_type_back(type));
        fbv.from_integer(theint.value);
        return expr2tc(new constant_fixedbv2t(type, fbv));
      }
      else if(is_floatbv_type(type))
      {
        ieee_floatt fbv;
        fbv.spec = to_floatbv_type(migrate_type_back(type));
        fbv.from_integer(theint.value);
        return expr2tc(new constant_floatbv2t(type, fbv));
      }
    }
    else if (is_fixedbv_type(from) && is_number_type(type))
    {
      // float/double to int/float/double
      fixedbvt fbv(to_constant_fixedbv2t(from).value);

      if(is_bv_type(type))
      {
        return expr2tc(new constant_int2t(type, fbv.to_integer()));
      }
      else if(is_fixedbv_type(type))
      {
        fbv.round(to_fixedbv_type(migrate_type_back(type)));
        return expr2tc(new constant_fixedbv2t(type, fbv));
      }
    }
    else if (is_floatbv_type(from) && is_number_type(type))
    {
      // float/double to int/float/double
      ieee_floatt fbv(to_constant_floatbv2t(from).value);

      if(is_bv_type(type))
      {
        return expr2tc(new constant_int2t(type, fbv.to_integer()));
      }
      else if(is_floatbv_type(type))
      {
        fbv.change_spec(to_floatbv_type(migrate_type_back(type)));
        return expr2tc(new constant_floatbv2t(type, fbv));
      }
    }
  }
  else if (is_bool_type(type))
  {
    // Bool type -> turn into equality with zero
    exprt zero = gen_zero(migrate_type_back(from->type));

    expr2tc zero2;
    migrate_expr(zero, zero2);

    expr2tc eq = expr2tc(new equality2t(from, zero2));
    expr2tc noteq = expr2tc(new not2t(eq));
    return noteq;
  }
  else if (is_symbol2t(from)
           && to_symbol2t(from).thename == "NULL"
           && is_pointer_type(type))
  {
    // Casts of null can operate on null directly. So long as we're casting it
    // to a pointer. Code like 32_floppy casts it to an int though; were we to
    // simplify that away, we end up with type errors.
    return from;
  }
  else if (is_pointer_type(type) && is_pointer_type(from))
  {
    // Casting from one pointer to another is meaningless... except when there's
    // pointer arithmetic about to be applied to it. So, only remove typecasts
    // that don't change the subtype width.
    const pointer_type2t &ptr_to = to_pointer_type(type);
    const pointer_type2t &ptr_from = to_pointer_type(from->type);

    if (is_symbol_type(ptr_to.subtype) || is_symbol_type(ptr_from.subtype) ||
        is_code_type(ptr_to.subtype) || is_code_type(ptr_from.subtype))
      return expr2tc(); // Not worth thinking about

    if (is_array_type(ptr_to.subtype) &&
        is_symbol_type(get_array_subtype(ptr_to.subtype)))
      return expr2tc(); // Not worth thinking about

    if (is_array_type(ptr_from.subtype) &&
        is_symbol_type(get_array_subtype(ptr_from.subtype)))
      return expr2tc(); // Not worth thinking about

    try {
      unsigned int to_width = (is_empty_type(ptr_to.subtype)) ? 8
                              : ptr_to.subtype->get_width();
      unsigned int from_width = (is_empty_type(ptr_from.subtype)) ? 8
                              : ptr_from.subtype->get_width();

      if (to_width == from_width)
        return from;
      else
        return expr2tc();
    } catch (array_type2t::dyn_sized_array_excp*e) {
      // Something crazy, and probably C++ based, occurred. Don't attempt to
      // simplify.
      return expr2tc();
    }
  }
  else if (is_typecast2t(from) && type == from->type)
  {
    // Typecast from a typecast can be eliminated. We'll be simplified even
    // further by the caller.
    return expr2tc(new typecast2t(type, to_typecast2t(from).from));
  }
  else if (second
           && is_bv_type(type)
           && is_bv_type(from)
           && is_arith_type(from)
           && (from->type->get_width() <= type->get_width()))
  {
    // So, if this is an integer type, performing an integer arith operation,
    // and the type we're casting to isn't _supposed_ to result in a loss of
    // information, push the cast downwards.
    std::list<expr2tc> set2;
    from->foreach_operand([&set2, this] (const expr2tc &e)
    {
      expr2tc cast = expr2tc(new typecast2t(type, e));
      set2.push_back(cast);
    });

    // Now clone the expression and update its operands.
    expr2tc newobj = expr2tc(from->clone());
    newobj.get()->type = type;

    std::list<expr2tc>::const_iterator it2 = set2.begin();
    newobj.get()->Foreach_operand([this, &it2] (expr2tc &e) {
        e= *it2;
        it2++;
      }
    );

    // Caller won't simplify us further if it's called us with second=true, so
    // give simplification another shot ourselves.
    expr2tc tmp = newobj->simplify();
    if (is_nil_expr(tmp))
      return newobj;
    else
      return tmp;
  }

  return expr2tc();
}

expr2tc
address_of2t::do_simplify(bool second __attribute__((unused))) const
{

  // NB: address of never has its operands simplified below its feet for sanitys
  // sake.
  // Only attempt to simplify indexes. Whatever we're taking the address of,
  // we can't simplify away the symbol.
  if (is_index2t(ptr_obj)) {
    const index2t &idx = to_index2t(ptr_obj);
    const pointer_type2t &ptr_type = to_pointer_type(type);

    // Don't simplify &a[0]
    if (is_constant_int2t(idx.index) &&
        to_constant_int2t(idx.index).value.is_zero())
      return expr2tc();

    expr2tc new_index = idx.index->simplify();
    if (is_nil_expr(new_index))
      new_index = idx.index;

    expr2tc zero = expr2tc(new constant_int2t(index_type2(), BigInt(0)));
    expr2tc new_idx = expr2tc(new index2t(idx.type, idx.source_value, zero));
    expr2tc sub_addr_of = expr2tc(new address_of2t(ptr_type.subtype, new_idx));

    return expr2tc(new add2t(type, sub_addr_of, new_index));
  } else {
    return expr2tc();
  }
}

template<template<typename> class TFunctor, typename constructor>
static expr2tc
simplify_relations(
  const type2tc &type,
  const expr2tc &side_1,
  const expr2tc &side_2)
{
  if(!is_number_type(type))
    return expr2tc();

  // Try to recursively simplify nested operations both sides, if any
  expr2tc simplied_side_1 = try_simplification(side_1);
  expr2tc simplied_side_2 = try_simplification(side_2);

  if (!is_constant_expr(simplied_side_1) || !is_constant_expr(simplied_side_2))
  {
    // Were we able to simplify the sides?
    if((side_1 != simplied_side_1) || (side_2 != simplied_side_2))
    {
      expr2tc new_op =
        expr2tc(new constructor(simplied_side_1, simplied_side_2));

      return typecast_check_return(type, new_op);
    }

    return expr2tc();
  }

  expr2tc simpl_res = expr2tc();

  if(is_bv_type(simplied_side_1->type) || is_bv_type(simplied_side_2->type))
  {
    std::function<bool(const expr2tc&)> is_constant =
      (bool(*)(const expr2tc&)) &is_constant_int2t;

    std::function<BigInt& (expr2tc&)> get_value =
      [](expr2tc& c) -> BigInt&
        { return to_constant_int2t(c).value; };

    simpl_res =
      TFunctor<BigInt>::simplify(
        simplied_side_1, simplied_side_2, is_constant, get_value);
  }
  else if(is_fixedbv_type(simplied_side_1->type)
          || is_fixedbv_type(simplied_side_2->type))
  {
    std::function<bool(const expr2tc&)> is_constant =
      (bool(*)(const expr2tc&)) &is_constant_fixedbv2t;

    std::function<fixedbvt& (expr2tc&)> get_value =
      [](expr2tc& c) -> fixedbvt&
        { return to_constant_fixedbv2t(c).value; };

    simpl_res =
      TFunctor<fixedbvt>::simplify(
        simplied_side_1, simplied_side_2, is_constant, get_value);
  }
  else if(is_floatbv_type(simplied_side_1->type)
          || is_floatbv_type(simplied_side_2->type))
  {
    std::function<bool(const expr2tc&)> is_constant =
      (bool(*)(const expr2tc&)) &is_constant_floatbv2t;

    std::function<ieee_floatt& (expr2tc&)> get_value =
      [](expr2tc& c) -> ieee_floatt&
        { return to_constant_floatbv2t(c).value; };

    simpl_res =
      TFunctor<ieee_floatt>::simplify(
        simplied_side_1, simplied_side_2, is_constant, get_value);
  }
  else if(is_bool_type(simplied_side_1->type)
          || is_bool_type(simplied_side_2->type))
  {
    std::function<bool(const expr2tc&)> is_constant =
      (bool(*)(const expr2tc&)) &is_constant_bool2t;

    std::function<bool& (expr2tc&)> get_value =
      [](expr2tc& c) -> bool&
        { return to_constant_bool2t(c).value; };

    simpl_res =
      TFunctor<bool>::simplify(
        simplied_side_1, simplied_side_2, is_constant, get_value);
  }

  return typecast_check_return(type, simpl_res);
}

template<class constant_type>
struct Equalitytor
{
  static expr2tc simplify(
    expr2tc &op1,
    expr2tc &op2,
    std::function<bool(const expr2tc&)> is_constant __attribute__((unused)),
    std::function<constant_type&(expr2tc&)> get_value)
  {
    bool res = (get_value(op1) == get_value(op2));
    return expr2tc(new constant_bool2t(res));
  }
};

expr2tc
equality2t::do_simplify(bool second __attribute__((unused))) const
{
  return simplify_relations<Equalitytor, equality2t>(type, side_1, side_2);
}

template<class constant_type>
struct Notequaltor
{
  static expr2tc simplify(
    expr2tc &op1,
    expr2tc &op2,
    std::function<bool(const expr2tc&)> is_constant __attribute__((unused)),
    std::function<constant_type&(expr2tc&)> get_value)
  {
    bool res = (get_value(op1) != get_value(op2));
    return expr2tc(new constant_bool2t(res));
  }
};

expr2tc
notequal2t::do_simplify(bool second __attribute__((unused))) const
{
  return simplify_relations<Notequaltor, notequal2t>(type, side_1, side_2);
}

template<class constant_type>
struct Lessthantor
{
  static expr2tc simplify(
    expr2tc &op1,
    expr2tc &op2,
    std::function<bool(const expr2tc&)> is_constant __attribute__((unused)),
    std::function<constant_type&(expr2tc&)> get_value)
  {
    bool res = (get_value(op1) < get_value(op2));
    return expr2tc(new constant_bool2t(res));
  }
};

expr2tc
lessthan2t::do_simplify(bool second __attribute__((unused))) const
{
  return simplify_relations<Lessthantor, lessthan2t>(type, side_1, side_2);
}

template<class constant_type>
struct Greaterthantor
{
  static expr2tc simplify(
    expr2tc &op1,
    expr2tc &op2,
    std::function<bool(const expr2tc&)> is_constant __attribute__((unused)),
    std::function<constant_type&(expr2tc&)> get_value)
  {
    bool res = (get_value(op1) > get_value(op2));
    return expr2tc(new constant_bool2t(res));
  }
};

expr2tc
greaterthan2t::do_simplify(bool second __attribute__((unused))) const
{
  return simplify_relations<Greaterthantor, greaterthan2t>(type, side_1, side_2);
}

template<class constant_type>
struct Lessthanequaltor
{
  static expr2tc simplify(
    expr2tc &op1,
    expr2tc &op2,
    std::function<bool(const expr2tc&)> is_constant __attribute__((unused)),
    std::function<constant_type&(expr2tc&)> get_value)
  {
    bool res = (get_value(op1) <= get_value(op2));
    return expr2tc(new constant_bool2t(res));
  }
};

expr2tc
lessthanequal2t::do_simplify(bool second __attribute__((unused))) const
{
  return simplify_relations<Lessthanequaltor, lessthanequal2t>(type, side_1, side_2);
}

template<class constant_type>
struct Greaterthanequaltor
{
  static expr2tc simplify(
    expr2tc &op1,
    expr2tc &op2,
    std::function<bool(const expr2tc&)> is_constant __attribute__((unused)),
    std::function<constant_type&(expr2tc&)> get_value)
  {
    bool res = (get_value(op1) >= get_value(op2));
    return expr2tc(new constant_bool2t(res));
  }
};

expr2tc
greaterthanequal2t::do_simplify(bool second __attribute__((unused))) const
{
  return simplify_relations<Greaterthanequaltor, greaterthanequal2t>(type, side_1, side_2);
}

expr2tc
if2t::do_simplify(bool second __attribute__((unused))) const
{
  if (is_constant_expr(cond)) {
    // We can simplify this.
    if (is_constant_bool2t(cond)) {
      if (to_constant_bool2t(cond).value) {
        return true_value;
      } else {
        return false_value;
      }
    } else {
      // Cast towards a bool type.
      expr2tc cast = expr2tc(new typecast2t(type_pool.get_bool(), cond));
      cast = cast->simplify();
      assert(!is_nil_expr(cast) && "We should always be able to cast a "
             "constant value to a constant bool");

      if (to_constant_bool2t(cast).value) {
        return true_value;
      } else {
        return false_value;
      }
    }
  } else {
    return expr2tc();
  }
}

expr2tc
overflow2t::do_simplify(bool second __attribute__((unused))) const
{
  unsigned int num_const = 0;
  bool simplified = false;

  // Non constant expression. We can't just simplify the operand, because it has
  // to remain the operation we expect (i.e., add2t shouldn't distribute itself)
  // so simplify its operands right here.
  if (second)
    return expr2tc();

  expr2tc new_operand = operand->clone();
  new_operand.get()->Foreach_operand([this, &simplified, &num_const] (expr2tc &e) {
      expr2tc tmp = (*e).simplify();
      if (!is_nil_expr(tmp)) {
        e = tmp;
        simplified = true;
      }

      if (is_constant_expr(e))
        num_const++;
    }
  );

  // If we don't have two constant operands, we can't simplify this expression.
  // We also don't want the underlying addition / whatever to become
  // distributed, so if the sub expressions are simplifiable, return a new
  // overflow with simplified subexprs, but no distribution.
  // The 'simplified' test guards against a continuous chain of simplifying the
  // same overflow expression over and over again.
  if (num_const != 2) {
    if (simplified)
      return expr2tc(new overflow2t(new_operand));
    else
      return expr2tc();
  }

  // Can only simplify ints
  if (!is_bv_type(new_operand))
    return expr2tc(new overflow2t(new_operand));

  // We can simplify that expression, so do it. And how do we detect overflows?
  // Perform the operation twice, once with a small type, one with huge, and
  // see if they differ. Max we can do is 64 bits, so if the expression already
  // has that size, give up.
  if (new_operand->type->get_width() == 64)
    return expr2tc();

  expr2tc simpl_op = new_operand->simplify();
  assert(is_constant_expr(simpl_op));
  expr2tc op_with_big_type = new_operand->clone();
  op_with_big_type.get()->type = (is_signedbv_type(new_operand))
                                 ? type_pool.get_int(64)
                                 : type_pool.get_uint(64);
  op_with_big_type = op_with_big_type->simplify();

  // Now ensure they're the same.
  equality2t eq(simpl_op, op_with_big_type);
  expr2tc tmp = eq.simplify();

  // And the inversion of that is the result of this overflow operation (i.e.
  // if not equal, then overflow).
  tmp = expr2tc(new not2t(tmp));
  tmp = tmp->simplify();
  assert(!is_nil_expr(tmp) && is_constant_bool2t(tmp));
  return tmp;
}

// Heavily inspired by cbmc's simplify_exprt::objects_equal_address_of
static expr2tc
obj_equals_addr_of(const expr2tc &a, const expr2tc &b)
{

  if (is_symbol2t(a) && is_symbol2t(b)) {
    if (a == b)
      return true_expr;
  } else if (is_index2t(a) && is_index2t(b)) {
    return obj_equals_addr_of(to_index2t(a).source_value,
                              to_index2t(b).source_value);
  } else if (is_member2t(a) && is_member2t(b)) {
    return obj_equals_addr_of(to_member2t(a).source_value,
                              to_member2t(b).source_value);
  } else if (is_constant_string2t(a) && is_constant_string2t(b)) {
    bool val = (to_constant_string2t(a).value == to_constant_string2t(b).value);
    if (val)
      return true_expr;
    else
      return false_expr;
  }

  return expr2tc();
}

expr2tc
same_object2t::do_simplify(bool second __attribute__((unused))) const
{

  if (is_address_of2t(side_1) && is_address_of2t(side_2))
    return obj_equals_addr_of(to_address_of2t(side_1).ptr_obj,
                              to_address_of2t(side_2).ptr_obj);

  if (is_symbol2t(side_1) && is_symbol2t(side_2) &&
      to_symbol2t(side_1).get_symbol_name() == "NULL" &&
      to_symbol2t(side_1).get_symbol_name() == "NULL")
    return true_expr;

  return expr2tc();
}

expr2tc
concat2t::do_simplify(bool second __attribute__((unused))) const
{

  if (!is_constant_int2t(side_1) || !is_constant_int2t(side_2))
    return expr2tc();

  const mp_integer &value1 = to_constant_int2t(side_1).value;
  const mp_integer &value2 = to_constant_int2t(side_2).value;

  // k; Take the values, and concatenate. Side 1 has higher end bits.
  mp_integer accuml = value1;
  accuml *= (1ULL << side_2->type->get_width());
  accuml += value2;

  return constant_int2tc(type, accuml);
}
