/*
 * llvmtypecheck.cpp
 *
 *  Created on: Jul 23, 2015
 *      Author: mramalho
 */

#include "llvm_convert.h"

#include <std_code.h>
#include <std_expr.h>
#include <expr_util.h>
#include <mp_arith.h>

#include <ansi-c/c_types.h>
#include <ansi-c/convert_integer_literal.h>

#include <boost/filesystem.hpp>

llvm_convertert::llvm_convertert(contextt &_context)
  : context(_context),
    ns(context),
    current_location(locationt()),
    current_path(""),
    current_function_name(""),
    current_scope_var_num(1),
    sm(nullptr)
{
}

llvm_convertert::~llvm_convertert()
{
}

bool llvm_convertert::convert()
{
  if(convert_top_level_decl())
    return true;

  return false;
}

bool llvm_convertert::convert_top_level_decl()
{
  // Iterate through each translation unit and their global symbols, creating
  // symbols as we go.
  for (auto &translation_unit : ASTs)
  {
    for (clang::ASTUnit::top_level_iterator
      it = translation_unit->top_level_begin();
      it != translation_unit->top_level_end();
      it++)
    {
      set_source_manager((*it)->getASTContext().getSourceManager());
      update_current_location((*it)->getLocation());

      exprt dummy_decl;
      convert_decl(**it, dummy_decl);
    }
  }

  return false;
}

void llvm_convertert::convert_decl(
  const clang::Decl& decl,
  exprt &new_expr)
{
  switch (decl.getKind()) {
    case clang::Decl::Typedef:
    {
      const clang::TypedefDecl &tdd =
        static_cast<const clang::TypedefDecl&>(decl);
      convert_typedef(tdd, new_expr);
      break;
    }

    case clang::Decl::Var:
    {
      const clang::VarDecl &vd =
        static_cast<const clang::VarDecl&>(decl);
      convert_var(vd, new_expr);
      break;
    }

    case clang::Decl::Function:
    {
      const clang::FunctionDecl &fd =
        static_cast<const clang::FunctionDecl&>(decl);
      convert_function(fd);
      break;
    }

    // Apparently if you insert a semicolon at the end of a
    // function declaration, this AST is created, so just
    // ignore it
    case clang::Decl::Empty:
      break;

    case clang::Decl::Record:
    default:
      std::cerr << "Unrecognized / unimplemented decl type ";
      std::cerr << decl.getDeclKindName() << std::endl;
      decl.dumpColor();
      abort();
  }

  new_expr.location() = current_location;
}

void llvm_convertert::convert_typedef(
  const clang::TypedefDecl &tdd,
  exprt &new_expr)
{
  // Get type
  typet t;
  get_type(tdd.getUnderlyingType(), t);

  symbolt symbol;
  get_default_symbol(
    symbol,
    t,
    tdd.getName().str(),
    get_modulename_from_path() + "::" + tdd.getName().str());

  symbol.is_type = true;

  move_symbol_to_context(symbol);

  if(current_function_name!= "")
    new_expr = code_skipt();
}

void llvm_convertert::convert_var(
  const clang::VarDecl &vd,
  exprt &new_expr)
{
  // Get type
  typet t;
  get_type(vd.getType(), t);

  std::string identifier =
    get_var_name(vd.getName().str(), vd.hasLocalStorage());

  symbolt symbol;
  get_default_symbol(symbol, t, vd.getName().str(), identifier);

  // This is not local, so has static lifetime
  if (!vd.hasLocalStorage())
  {
    symbol.static_lifetime = true;
    symbol.value = gen_zero(t);

    // Add location to value since it is only added on get_expr
    symbol.value.location() = current_location;
  }

  if (vd.hasExternalStorage())
    symbol.is_extern = true;
  symbol.lvalue = true;

  // We have to add the symbol before converting the initial assignment
  // because we might have something like 'int x = x + 1;' which is
  // completely wrong but allowed by the language
  move_symbol_to_context(symbol);

  // Now get the symbol back to continue the conversion
  symbolt &added_symbol = context.symbols.find("c::" + identifier)->second;

  code_declt decl;
  decl.operands().push_back(symbol_expr(added_symbol));

  if(vd.hasInit())
  {
    const clang::Expr *value = vd.getInit();
    exprt val;
    get_expr(*value, val);

    added_symbol.value = val;
    decl.operands().push_back(val);
  }

  new_expr = decl;

  // Save the variable address and name to the object map
  std::size_t address = reinterpret_cast<std::size_t>(&vd);
  object_map[address] = identifier;

  // Increment current scope variable number. If the variable
  // is global/static, it has no impact. If the variable is
  // scoped, than it will force a unique name on it
  ++current_scope_var_num;
}

void llvm_convertert::convert_function(const clang::FunctionDecl &fd)
{
  std::string old_function_name = current_function_name;
  current_function_name = fd.getName().str();

  // Set initial variable name, it will be used for variables' name
  // This will be reset everytime a function is parsed
  current_scope_var_num = 1;

  // Build function's type
  code_typet type;

  // Return type
  const clang::QualType ret_type = fd.getReturnType();
  typet return_type;
  get_type(ret_type, return_type);
  type.return_type() = return_type;

  // We convert the parameters first so their symbol are added to context
  // before converting the body, as they may appear on the function body
  for (const auto &pdecl : fd.params()) {
    code_typet::argumentt param;
    convert_function_params(pdecl, param);
    type.arguments().push_back(param);
  }

  symbolt symbol;
  get_default_symbol(
    symbol,
    type,
    fd.getName().str(),
    fd.getName().str());

  symbol.lvalue = true;

  // We need: a type, a name, and an optional body
  clang::Stmt *body = NULL;
  if (fd.isThisDeclarationADefinition() && fd.hasBody())
    body = fd.getBody();

  if(body)
    get_expr(*body, symbol.value);

  // see if we have it already
  symbolst::iterator old_it=context.symbols.find(symbol.name);
  if(old_it==context.symbols.end())
  {
    move_symbol_to_context(symbol);
  }
  else
  {
    symbolt &old_symbol = old_it->second;
    check_symbol_redefinition(old_symbol, symbol);
  }

  // Save the function address and name to the object map
  std::size_t address = reinterpret_cast<std::size_t>(&fd);
  object_map[address] = fd.getName().str();

  current_function_name = old_function_name;
}

void llvm_convertert::convert_function_params(
  clang::ParmVarDecl *pdecl,
  code_typet::argumentt &param)
{
  typet param_type;
  get_type(pdecl->getOriginalType(), param_type);

  std::string name = pdecl->getName().str();

  symbolt param_symbol;
  get_default_symbol(
    param_symbol,
    param_type,
    name,
    get_param_name(name));

  param_symbol.lvalue = true;
  param_symbol.file_local = true;
  param_symbol.is_actual = true;

  param.type() = param_type;
  param.base_name(name);
  param.cmt_identifier(param_symbol.name.as_string());
  param.location() = param_symbol.location;

  move_symbol_to_context(param_symbol);

  // Save the function's param address and name to the object map
  std::size_t address = reinterpret_cast<std::size_t>(pdecl);
  object_map[address] = get_param_name(name);
}

void llvm_convertert::get_type(
  const clang::QualType &q_type,
  typet &new_type)
{
  const clang::Type &the_type = *q_type.getTypePtrOrNull();

  switch (the_type.getTypeClass()) {
    case clang::Type::Builtin:
    {
      const clang::BuiltinType &bt =
        static_cast<const clang::BuiltinType&>(the_type);
      get_builtin_type(bt, new_type);
      break;
    }

    case clang::Type::Typedef:
    {
      const clang::TypedefType &pt =
        static_cast<const clang::TypedefType &>(the_type);
      clang::QualType q_typedef_type = pt.getDecl()->getUnderlyingType();
      get_type(q_typedef_type, new_type);
      break;
    }

    case clang::Type::Pointer:
    {
      const clang::PointerType &pt =
        static_cast<const clang::PointerType &>(the_type);
      const clang::QualType &pointee = pt.getPointeeType();

      typet sub_type;
      get_type(pointee, sub_type);

      new_type = gen_pointer_type(sub_type);
      break;
    }

    // Those two here appears when we make a function call, e.g:
    // FunctionNoProto: int x = fun()
    // FunctionProto: int x = fun(a, b)
    case clang::Type::FunctionNoProto:
    case clang::Type::FunctionProto:
      break;

    default:
      std::cerr << "No clang <=> ESBMC migration for type "
                << the_type.getTypeClassName() << std::endl;
      the_type.dump();
      abort();
  }

  if(q_type.isConstQualified())
    new_type.cmt_constant(true);
}

void llvm_convertert::get_builtin_type(
  const clang::BuiltinType& bt,
  typet& new_type)
{
  switch (bt.getKind()) {
    case clang::BuiltinType::Void:
      new_type = empty_typet();
      break;

    case clang::BuiltinType::Bool:
      new_type = bool_type();
      break;

    case clang::BuiltinType::Char_U:
    case clang::BuiltinType::UChar:
      new_type = unsignedbv_typet(config.ansi_c.char_width);
      break;

    case clang::BuiltinType::Char16:
      new_type = unsignedbv_typet(16);
      break;

    case clang::BuiltinType::Char32:
      new_type = unsignedbv_typet(32);
      break;

    case clang::BuiltinType::Char_S:
    case clang::BuiltinType::SChar:
      new_type = signedbv_typet(config.ansi_c.char_width);
      break;

    case clang::BuiltinType::UShort:
      new_type = unsignedbv_typet(config.ansi_c.short_int_width);
      break;

    case clang::BuiltinType::UInt:
      new_type = uint_type();
      break;

    case clang::BuiltinType::ULong:
      new_type = long_uint_type();
      break;

    case clang::BuiltinType::ULongLong:
      new_type = long_long_uint_type();
      break;

    case clang::BuiltinType::UInt128:
      // Various simplification / big-int related things use uint64_t's...
      std::cerr << "No support for uint128's in ESBMC right now, sorry" << std::endl;
      abort();
      break;

    case clang::BuiltinType::Short:
      new_type = signedbv_typet(config.ansi_c.short_int_width);
      break;

    case clang::BuiltinType::Int:
      new_type = int_type();
      break;

    case clang::BuiltinType::Long:
      new_type = long_int_type();
      break;

    case clang::BuiltinType::LongLong:
      new_type = long_long_int_type();
      break;

    case clang::BuiltinType::Int128:
      // Various simplification / big-int related things use uint64_t's...
      std::cerr << "No support for uint128's in ESBMC right now, sorry" << std::endl;
      abort();
      break;

    case clang::BuiltinType::Float:
      new_type = float_type();
      break;

    case clang::BuiltinType::Double:
      new_type = double_type();
      break;

    case clang::BuiltinType::LongDouble:
      new_type = long_double_type();
      break;

    default:
      std::cerr << "Unrecognized clang builtin type "
      << bt.getName(clang::PrintingPolicy(clang::LangOptions())).str()
      << std::endl;
      abort();
  }
}

void llvm_convertert::get_expr(
  const clang::Stmt& stmt,
  exprt& new_expr)
{
  update_current_location(stmt.getLocStart());

  switch(stmt.getStmtClass())
  {
    case clang::Stmt::IntegerLiteralClass:
    {
      const clang::IntegerLiteral &integer =
        static_cast<const clang::IntegerLiteral&>(stmt);
      llvm::APInt val = integer.getValue();
      assert(val.getBitWidth() <= 64 && "Too large an integer found, sorry");

      typet the_type;
      get_type(integer.getType(), the_type);
      assert(the_type.is_unsignedbv() || the_type.is_signedbv());

      exprt bval;
      if (the_type.is_unsignedbv()) {
        uint64_t the_val = val.getZExtValue();
        convert_integer_literal(integer2string(the_val) + "u", bval, 10);
      } else {
        int64_t the_val = val.getSExtValue();
        convert_integer_literal(integer2string(the_val), bval, 10);
      }

      new_expr.swap(bval);
      break;
    }

    case clang::Stmt::ImplicitCastExprClass:
    case clang::Stmt::CStyleCastExprClass:
    {
      const clang::CastExpr &cast =
        static_cast<const clang::CastExpr &>(stmt);
      get_cast_expr(cast, new_expr);
      break;
    }

    case clang::Stmt::CompoundStmtClass:
    {
      const clang::CompoundStmt &compound_stmt =
        static_cast<const clang::CompoundStmt &>(stmt);

      code_blockt block;
      for (const auto &stmt : compound_stmt.body()) {
        exprt statement;
        get_expr(*stmt, statement);

        // Before push the expr to the block, we must check if
        // the code is an codet, e.g., sideeffects are exprts
        // and must be converted
        convert_exprt_inside_block(statement);

        block.operands().push_back(statement);
      }

      // Set the end location for blocks, we get all the information
      // from the current location (file, line and function name) then
      // we change the line number
      locationt end_location;
      end_location = current_location;
      end_location.set_line(
        sm->getSpellingLineNumber(compound_stmt.getLocEnd()));
      block.end_location(end_location);

      new_expr = block;
      break;
    }

    case clang::Stmt::DeclStmtClass:
    {
      const clang::DeclStmt &decl =
        static_cast<const clang::DeclStmt&>(stmt);

      const auto &declgroup = decl.getDeclGroup();

      codet decls("decl-block");
      for (clang::DeclGroupRef::const_iterator
        it = declgroup.begin();
        it != declgroup.end();
        it++)
      {
        exprt single_decl;
        convert_decl(**it, single_decl);
        decls.operands().push_back(single_decl);
      }

      new_expr = decls;
      break;
    }

    case clang::Stmt::BinaryOperatorClass:
    {
      const clang::BinaryOperator &binop =
        static_cast<const clang::BinaryOperator&>(stmt);
      get_binary_operator_expr(binop, new_expr);
      break;
    }

    case clang::Stmt::DeclRefExprClass:
    {
      const clang::DeclRefExpr &decl =
        static_cast<const clang::DeclRefExpr&>(stmt);

      const clang::Decl &dcl =
        static_cast<const clang::Decl&>(*decl.getDecl());

      get_decl_expr(dcl, new_expr);
      break;
    }

    case clang::Stmt::ReturnStmtClass:
    {
      const clang::ReturnStmt &ret =
        static_cast<const clang::ReturnStmt&>(stmt);

      const clang::Expr &retval = *ret.getRetValue();
      exprt val;
      get_expr(retval, val);

      code_returnt ret_expr;
      ret_expr.return_value() = val;

      new_expr = ret_expr;
      break;
    }

    case clang::Stmt::CallExprClass:
    {
      const clang::CallExpr &function_call =
        static_cast<const clang::CallExpr &>(stmt);

      const clang::Stmt *callee = function_call.getCallee();
      exprt callee_expr;
      get_expr(*callee, callee_expr);

      typet type;
      get_type(function_call.getType(), type);

      side_effect_expr_function_callt call;
      call.function() = callee_expr;
      call.type() = type;

      for (const clang::Expr *arg : function_call.arguments()) {
        exprt single_arg;
        get_expr(*arg, single_arg);
        call.arguments().push_back(single_arg);
      }

      // Let's check if the symbol for this function call is defined
      // on the context, if it isn't, we should create and add a symbol
      // without value, so esbmc will replace the function call by
      // a non deterministic call later on
      symbolst::iterator old_it=context.symbols.find(callee_expr.identifier());
      if(old_it==context.symbols.end())
      {
        code_typet func_type;
        func_type.return_type() = type;

        symbolt symbol;
        get_default_symbol(
          symbol,
          func_type,
          callee_expr.name().as_string(),
          callee_expr.name().as_string());
        move_symbol_to_context(symbol);
      }

      new_expr = call;
      break;
    }

    // This is an expr surrounded by parenthesis, we'll ignore it for
    // now, and check its subexpression
    case clang::Stmt::ParenExprClass:
    {
      const clang::ParenExpr& p =
        static_cast<const clang::ParenExpr &>(stmt);
      get_expr(*p.getSubExpr(), new_expr);
      break;
    }

    case clang::Stmt::UnaryOperatorClass:
    {
      const clang::UnaryOperator &uniop =
        static_cast<const clang::UnaryOperator &>(stmt);
      get_unary_operator_expr(uniop, new_expr);
      break;
    }

    case clang::Stmt::IfStmtClass:
    {
      const clang::IfStmt &ifstmt =
        static_cast<const clang::IfStmt &>(stmt);

      exprt cond;
      get_expr(*ifstmt.getCond(), cond);

      exprt then;
      get_expr(*ifstmt.getThen(), then);

      codet if_expr("ifthenelse");
      if_expr.copy_to_operands(cond, then);

      if(ifstmt.getElse())
      {
        exprt else_expr;
        get_expr(*ifstmt.getElse(), else_expr);
        if_expr.copy_to_operands(else_expr);
      }

      new_expr = if_expr;
      break;
    }

    // This is the ternary if
    case clang::Stmt::ConditionalOperatorClass:
    {
      const clang::ConditionalOperator &ternary_if =
        static_cast<const clang::ConditionalOperator &>(stmt);

      exprt cond;

      // For some strange reason, the cond is wrapped around an integer
      // typecast, don't really know why, so ignore it
      if(ternary_if.getCond()->getStmtClass() == clang::Stmt::CStyleCastExprClass
         || ternary_if.getCond()->getStmtClass() == clang::Stmt::ImplicitCastExprClass)
      {
        const clang::CastExpr &cast =
          static_cast<const clang::CastExpr &>(*ternary_if.getCond());
        get_expr(*cast.getSubExpr(), cond);
      }
      else
        get_expr(*ternary_if.getCond(), cond);

      // If the condition is not of boolean type, it must be casted
      gen_typecast(cond, bool_type());

      exprt then;
      get_expr(*ternary_if.getTrueExpr(), then);

      exprt if_expr("if", bool_type());
      if_expr.copy_to_operands(cond, then);

      exprt else_expr;
      get_expr(*ternary_if.getFalseExpr(), else_expr);
      if_expr.copy_to_operands(else_expr);

      new_expr = if_expr;
      break;
    }

    // This is the gcc's ternary if extension
    case clang::Stmt::BinaryConditionalOperatorClass:
    {
      const clang::BinaryConditionalOperator &ternary_if =
        static_cast<const clang::BinaryConditionalOperator &>(stmt);

      exprt cond;
      // For some strange reason, the cond is wrapped around an integer
      // typecast, don't really know why, so ignore it
      if(ternary_if.getCond()->getStmtClass() == clang::Stmt::CStyleCastExprClass
          || ternary_if.getCond()->getStmtClass() == clang::Stmt::ImplicitCastExprClass)
      {
        const clang::CastExpr &cast =
            static_cast<const clang::CastExpr &>(*ternary_if.getCond());
        get_expr(*cast.getSubExpr(), cond);
      }
      else
        get_expr(*ternary_if.getCond(), cond);

      // If the condition is not of boolean type, it must be casted
      gen_typecast(cond, bool_type());

      exprt then;
      get_expr(*ternary_if.getTrueExpr(), then);

      exprt if_expr("if", bool_type());
      if_expr.copy_to_operands(cond, then);

      exprt else_expr;
      get_expr(*ternary_if.getFalseExpr(), else_expr);
      if_expr.copy_to_operands(else_expr);

      new_expr = if_expr;
      break;
    }

    case clang::Stmt::OpaqueValueExprClass:
    {
      const clang::OpaqueValueExpr &opaque_expr =
        static_cast<const clang::OpaqueValueExpr &>(stmt);
      get_expr(*opaque_expr.getSourceExpr(), new_expr);
      break;
    }

    case clang::Stmt::WhileStmtClass:
    {
      const clang::WhileStmt &while_stmt =
        static_cast<const clang::WhileStmt &>(stmt);

      exprt cond;
      get_expr(*while_stmt.getCond(), cond);
      gen_typecast(cond, bool_type());

      codet body;
      get_expr(*while_stmt.getBody(), body);

      code_whilet code_while;
      code_while.cond() = cond;
      code_while.body() = body;

      new_expr = code_while;
      break;
    }

    case clang::Stmt::DoStmtClass:
    {
      const clang::DoStmt &do_stmt =
        static_cast<const clang::DoStmt &>(stmt);

      exprt cond;
      get_expr(*do_stmt.getCond(), cond);
      gen_typecast(cond, bool_type());

      codet body;
      get_expr(*do_stmt.getBody(), body);

      code_dowhilet code_while;
      code_while.cond() = cond;
      code_while.body() = body;

      new_expr = code_while;
      break;
    }

    case clang::Stmt::ForStmtClass:
    {
      const clang::ForStmt &for_stmt =
        static_cast<const clang::ForStmt &>(stmt);

      codet init = code_skipt();
      if(for_stmt.getInit())
        get_expr(*for_stmt.getInit(), init);

      exprt cond = true_exprt();
      if(for_stmt.getCond())
      {
        get_expr(*for_stmt.getCond(), cond);
        gen_typecast(cond, bool_type());
      }

      exprt inc = nil_exprt();
      if(for_stmt.getInc())
      {
        exprt iter;
        get_expr(*for_stmt.getInc(), iter);

        // The increment should be a code_expressiont
        inc = codet("expression");
        inc.copy_to_operands(iter);
      }

      codet body;
      get_expr(*for_stmt.getBody(), body);

      code_fort code_for;
      code_for.init() = init;
      code_for.cond() = cond;
      code_for.iter() = inc;
      code_for.body() = body;

      new_expr = code_for;
      break;
    }

    case clang::Stmt::BreakStmtClass:
    {
      new_expr = code_breakt();
      break;
    }

    case clang::Stmt::ContinueStmtClass:
    {
      new_expr = code_continuet();
      break;
    }

    case clang::Stmt::LabelStmtClass:
    {
      const clang::LabelStmt &label_stmt =
        static_cast<const clang::LabelStmt &>(stmt);

      exprt sub_stmt;
      get_expr(*label_stmt.getSubStmt(), sub_stmt);

      codet label("label");
      label.add("label") = irept(label_stmt.getName());
      label.copy_to_operands(sub_stmt);

      new_expr = label;
      break;
    }

    case clang::Stmt::NullStmtClass:
    {
      new_expr = code_skipt();
      break;
    }

    case clang::Stmt::GotoStmtClass:
    {
      const clang::GotoStmt &goto_stmt =
        static_cast<const clang::GotoStmt &>(stmt);

      code_gotot code_goto;
      code_goto.set_destination(goto_stmt.getLabel()->getName().str());

      new_expr = code_goto;
      break;
    }

    case clang::Stmt::SwitchStmtClass:
    {
      const clang::SwitchStmt &switch_stmt =
        static_cast<const clang::SwitchStmt &>(stmt);

      exprt value;
      get_expr(*switch_stmt.getCond(), value);

      codet body;
      get_expr(*switch_stmt.getBody(), body);

      code_switcht switch_code;
      switch_code.value() = value;
      switch_code.body() = body;

      new_expr = switch_code;
      break;
    }

    case clang::Stmt::CaseStmtClass:
    {
      const clang::CaseStmt &case_stmt =
        static_cast<const clang::CaseStmt &>(stmt);

      exprt value;
      get_expr(*case_stmt.getLHS(), value);

      exprt sub_stmt;
      get_expr(*case_stmt.getSubStmt(), sub_stmt);

      codet label("label");

      exprt &case_ops=label.add_expr("case");
      case_ops.copy_to_operands(value);

      label.copy_to_operands(sub_stmt);

      new_expr = label;
      break;
    }

    case clang::Stmt::DefaultStmtClass:
    {
      const clang::DefaultStmt &default_stmt =
        static_cast<const clang::DefaultStmt &>(stmt);

      exprt sub_stmt;
      get_expr(*default_stmt.getSubStmt(), sub_stmt);

      codet label("label");
      label.set("default", true);
      label.copy_to_operands(sub_stmt);

      new_expr = label;
      break;
    }

    default:
      std::cerr << "Conversion of unsupported clang expr: \"";
      std::cerr << stmt.getStmtClassName() << "\" to expression" << std::endl;
      stmt.dump();
      abort();
  }

  new_expr.location() = current_location;
}

void llvm_convertert::get_decl_expr(
  const clang::Decl& decl,
  exprt& new_expr)
{
  std::string identifier;
  typet type;

  switch(decl.getKind())
  {
    case clang::Decl::Var:
    {
      const clang::VarDecl &vd =
        static_cast<const clang::VarDecl&>(decl);

      std::size_t address = reinterpret_cast<std::size_t>(&vd);
      identifier = object_map.find(address)->second;

      get_type(vd.getType(), type);
      break;
    }

    case clang::Decl::ParmVar:
    {
      const clang::ParmVarDecl &vd =
        static_cast<const clang::ParmVarDecl&>(decl);

      std::size_t address = reinterpret_cast<std::size_t>(&vd);
      identifier = object_map.find(address)->second;

      get_type(vd.getType(), type);
      break;
    }

    case clang::Decl::Function:
    {
      const clang::FunctionDecl &fd =
        static_cast<const clang::FunctionDecl&>(decl);

      std::size_t address = reinterpret_cast<std::size_t>(&fd);

      // We may not find the function's symbol, because it was
      // not defined or is undefined at all
      object_mapst::iterator it = object_map.find(address);
      if(it == object_map.end())
        identifier = fd.getName().str();
      else
        identifier = it->second;

      get_type(fd.getType(), type);
      break;
    }

    default:
      std::cerr << "Conversion of unsupported clang decl operator: \"";
      std::cerr << decl.getDeclKindName() << "\" to expression" << std::endl;
      decl.dump();
      abort();
  }

  new_expr = exprt("symbol", type);
  new_expr.identifier("c::" + identifier);
  new_expr.name(identifier);
}

void llvm_convertert::get_cast_expr(
  const clang::CastExpr& cast,
  exprt& new_expr)
{
  exprt expr;
  get_expr(*cast.getSubExpr(), expr);

  typet type;
  get_type(cast.getType(), type);

  switch(cast.getCastKind())
  {
    case clang::CK_FunctionToPointerDecay:
      break;

    case clang::CK_IntegralCast:
    case clang::CK_IntegralToBoolean:
    case clang::CK_IntegralToFloating:

    case clang::CK_FloatingToIntegral:
    case clang::CK_FloatingToBoolean:
    case clang::CK_FloatingCast:

    case clang::CK_BitCast:
    case clang::CK_LValueToRValue:
      gen_typecast(expr, type);
      break;

    default:
      std::cerr << "Conversion of unsupported clang cast operator: \"";
      std::cerr << cast.getCastKindName() << "\" to expression" << std::endl;
      cast.dumpColor();
      abort();
  }

  new_expr = expr;
}

void llvm_convertert::get_unary_operator_expr(
  const clang::UnaryOperator& uniop,
  exprt& new_expr)
{
  typet uniop_type;
  get_type(uniop.getType(), uniop_type);

  exprt unary_sub;
  get_expr(*uniop.getSubExpr(), unary_sub);

  switch (uniop.getOpcode())
  {
    case clang::UO_Plus:
      new_expr = exprt("unary+", uniop_type);
      break;

    case clang::UO_Minus:
      new_expr = exprt("unary-", uniop_type);
      break;

    case clang::UO_Not:
      new_expr = exprt("bitnot", uniop_type);
      break;

    case clang::UO_LNot:
      new_expr = exprt("not", bool_type());
      break;

    case clang::UO_PreInc:
      new_expr = side_effect_exprt("preincrement", uniop_type);
      break;

    case clang::UO_PreDec:
      new_expr = side_effect_exprt("predecrement", uniop_type);
      break;

    case clang::UO_PostInc:
      new_expr = side_effect_exprt("postincrement", uniop_type);
      break;

    case clang::UO_PostDec:
      new_expr = side_effect_exprt("postdecrement", uniop_type);
      break;

    case clang::UO_AddrOf:
      new_expr = exprt("address_of", uniop_type);
      break;

    case clang::UO_Deref:
      new_expr = exprt("dereference", uniop_type);
      break;

    default:
      std::cerr << "Conversion of unsupported clang unary operator: \"";
      std::cerr << clang::UnaryOperator::getOpcodeStr(uniop.getOpcode()).str()
                << "\" to expression" << std::endl;
      uniop.dumpColor();
      abort();
  }

  new_expr.operands().push_back(unary_sub);
}

void llvm_convertert::get_binary_operator_expr(
  const clang::BinaryOperator& binop,
  exprt& new_expr)
{
  exprt lhs;
  get_expr(*binop.getLHS(), lhs);

  exprt rhs;
  get_expr(*binop.getRHS(), rhs);

  // The resultant type from the binary operation
  // For boolean operation, we always assign bool type regardless
  // of what llvm says
  typet binop_type;
  get_type(binop.getType(), binop_type);

  switch(binop.getOpcode())
  {
    case clang::BO_Assign:
      new_expr = codet("assign");
      gen_typecast(rhs, lhs.type());
      break;

    case clang::BO_Add:
      new_expr = exprt("+", binop_type);
      break;

    case clang::BO_Sub:
      new_expr = exprt("-", binop_type);
      break;

    case clang::BO_Mul:
      new_expr = exprt("*", binop_type);
      break;

    case clang::BO_Div:
      new_expr = exprt("/", binop_type);
      break;

    case clang::BO_Shl:
      new_expr = exprt("shl", binop_type);
      break;

    case clang::BO_Shr:
      new_expr = exprt("ashr", binop_type);
      break;

    case clang::BO_Rem:
      new_expr = exprt("mod", binop_type);
      break;

    case clang::BO_And:
      new_expr = exprt("bitand", binop_type);
      break;

    case clang::BO_Xor:
      new_expr = exprt("bitxor", binop_type);
      break;

    case clang::BO_Or:
      new_expr = exprt("bitor", binop_type);
      break;

    case clang::BO_LT:
      new_expr = exprt("<", bool_type());
      break;

    case clang::BO_GT:
      new_expr = exprt(">", bool_type());
      break;

    case clang::BO_LE:
      new_expr = exprt("<=", bool_type());
      break;

    case clang::BO_GE:
      new_expr = exprt(">=", bool_type());
      break;

    case clang::BO_EQ:
      new_expr = exprt("=", bool_type());
      break;

    case clang::BO_NE:
      new_expr = exprt("notequal", bool_type());
      break;

    case clang::BO_LAnd:
      new_expr = exprt("and", bool_type());
      gen_typecast(lhs, bool_type());
      gen_typecast(rhs, bool_type());
      break;

    case clang::BO_LOr:
      new_expr = exprt("or", bool_type());
      gen_typecast(lhs, bool_type());
      gen_typecast(rhs, bool_type());
      break;

    default:
      std::cerr << "Conversion of unsupported clang binary operator: \"";
      std::cerr << binop.getOpcodeStr().str() << "\" to expression" << std::endl;
      binop.dumpColor();
      abort();
  }

  new_expr.copy_to_operands(lhs, rhs);
}

void llvm_convertert::gen_typecast(
  exprt &expr,
  typet type)
{
  if(expr.type() != type)
  {
    exprt new_expr;

    // I don't think that this code is really needed,
    // but it removes typecasts, which is good
    // It should simplify constants to either true or
    // false when casting them to bool
    if(type.is_bool() and expr.is_constant())
    {
      mp_integer value=string2integer(expr.cformat().as_string());
      if(value != 0)
        new_expr = true_exprt();
      else
       new_expr = false_exprt();
    }
    else
      new_expr = typecast_exprt(expr, type);

    new_expr.location() = expr.location();
    expr.swap(new_expr);
  }
}

void llvm_convertert::get_default_symbol(symbolt& symbol)
{
  symbol.mode = "C";
  symbol.module = get_modulename_from_path();
  symbol.location = current_location;
}

void llvm_convertert::get_default_symbol(
  symbolt& symbol,
  typet type,
  std::string base_name,
  std::string pretty_name)
{
  get_default_symbol(symbol);
  symbol.type = type;
  symbol.base_name = base_name;
  symbol.pretty_name = pretty_name;
  symbol.name = "c::" + pretty_name;
}

std::string llvm_convertert::get_var_name(
  std::string name,
  bool is_local)
{
  if(!is_local)
    return name;

  std::string pretty_name = get_modulename_from_path() + "::";
  if(current_function_name!= "")
    pretty_name += current_function_name + "::";
  pretty_name += integer2string(current_scope_var_num) + "::";
  pretty_name += name;

  return pretty_name;
}

std::string llvm_convertert::get_param_name(std::string name)
{
  std::string pretty_name = get_modulename_from_path() + "::";
  if(current_function_name!= "")
    pretty_name += current_function_name + "::";
  pretty_name += name;

  return pretty_name;
}

void llvm_convertert::set_source_manager(
  clang::SourceManager& source_manager)
{
  sm = &source_manager;
}

void llvm_convertert::update_current_location(
  clang::SourceLocation source_location)
{
  current_path =sm->getFilename(source_location).str();

  current_location.set_file(get_filename_from_path());
  current_location.set_line(sm->getSpellingLineNumber(source_location));
  current_location.set_function(current_function_name);
}

std::string llvm_convertert::get_modulename_from_path()
{
  return  boost::filesystem::path(current_path).stem().string();
}

std::string llvm_convertert::get_filename_from_path()
{
  return  boost::filesystem::path(current_path).filename().string();
}

void llvm_convertert::move_symbol_to_context(
  symbolt& symbol)
{
  if (context.move(symbol)) {
    std::cerr << "Couldn't add symbol " << symbol.name
        << " to symbol table" << std::endl;
    abort();
  }
}

void llvm_convertert::check_symbol_redefinition(
  symbolt& old_symbol,
  symbolt& new_symbol)
{
  if(old_symbol.type.is_code())
  {
    if(new_symbol.value.is_not_nil())
    {
      if(old_symbol.value.is_not_nil())
      {
        std::cerr << "multiple definition of `" << new_symbol.display_name()
                  << "':" << std::endl << "first defined: "
                  << old_symbol.location.as_string() << std::endl
                  << "redefinition: " << new_symbol.location.as_string()
                  << std::endl;
        abort();
      }
      else
      {
        // overwrite location
        old_symbol.location=new_symbol.location;

        // move body
        old_symbol.value.swap(new_symbol.value);
      }
    }
  }
}

void llvm_convertert::convert_exprt_inside_block(
  exprt& expr)
{
  if(expr.is_code())
    return;

  codet code("expression");
  code.copy_to_operands(expr);

  expr.swap(code);
}
