/*
 * llvmtypecheck.h
 *
 *  Created on: Jul 23, 2015
 *      Author: mramalho
 */

#ifndef LLVM_FRONTEND_LLVM_CONVERT_H_
#define LLVM_FRONTEND_LLVM_CONVERT_H_

#define __STDC_LIMIT_MACROS
#define __STDC_FORMAT_MACROS

#include <context.h>
#include <namespace.h>
#include <std_types.h>

#include <clang/Frontend/ASTUnit.h>
#include <clang/AST/Type.h>
#include <clang/AST/Expr.h>

class llvm_convertert
{
public:
  llvm_convertert(
    contextt &_context,
    std::vector<std::unique_ptr<clang::ASTUnit> > &_ASTs);
  virtual ~llvm_convertert();

  bool convert();

private:
  clang::ASTContext *ASTContext;
  contextt &context;
  namespacet ns;
  std::vector<std::unique_ptr<clang::ASTUnit> > &ASTs;

  unsigned int current_scope_var_num;
  unsigned int anon_var_counter;
  unsigned int anon_tag_counter;

  clang::SourceManager *sm;

  typedef std::map<std::size_t, std::string> object_mapt;
  object_mapt object_map;

  typedef std::map<std::size_t, std::string> type_mapt;
  type_mapt type_map;

  void dump_type_map();
  void dump_object_map();

  bool convert_builtin_types();
  bool convert_top_level_decl();

  bool get_decl(
    const clang::Decl &decl,
    exprt &new_expr);

  bool get_var(
    const clang::VarDecl &vd,
    exprt &new_expr);

  bool get_function(
    const clang::FunctionDecl &fd);

  bool get_function_params(
    const clang::ParmVarDecl &pdecl,
    exprt &param);

  bool get_struct_union_class(
    const clang::RecordDecl &recordd);

  bool get_struct_union_class_fields(
    const clang::RecordDecl &recordd,
    struct_union_typet &type);

  bool get_type(
    const clang::QualType &the_type,
    typet &new_type);

  bool get_builtin_type(
    const clang::BuiltinType &bt,
    typet &new_type);

  bool get_expr(
    const clang::Stmt &stmt,
    exprt &new_expr);

  bool get_decl_ref(
    const clang::Decl &decl,
    exprt &new_expr);

  bool get_binary_operator_expr(
    const clang::BinaryOperator &binop,
    exprt &new_expr);

  bool get_compound_assign_expr(
    const clang::CompoundAssignOperator& compop,
    exprt& new_expr);

  bool get_unary_operator_expr(
    const clang::UnaryOperator &uniop,
    exprt &new_expr);

  bool get_cast_expr(
    const clang::CastExpr &cast,
    exprt &new_expr);

  void get_default_symbol(
    symbolt &symbol,
    typet type,
    std::string base_name,
    std::string pretty_name,
    locationt location);

  void get_field_name(
    const clang::FieldDecl &fd,
    std::string &name);

  void get_var_name(
    const clang::VarDecl &vd,
    std::string &name);

  void get_function_param_name(
    const clang::ParmVarDecl &pd,
    std::string &name);

  void get_function_name(
    const clang::FunctionDecl& fd,
    std::string &base_name,
    std::string &pretty_name);

  bool get_tag_name(
    const clang::RecordDecl& recordd,
    std::string &identifier);

  void get_start_location_from_stmt(
    const clang::Stmt& stmt,
    locationt &location);

  void get_final_location_from_stmt(
    const clang::Stmt& stmt,
    locationt &location);

  void get_location_from_decl(
    const clang::Decl& decl,
    locationt &location);

  void set_location(
    clang::PresumedLoc &PLoc,
    std::string &function_name,
    locationt &location);

  void get_presumed_location(
    const clang::SourceLocation &loc,
    clang::PresumedLoc &PLoc);

  std::string get_filename_from_path(std::string path);
  std::string get_modulename_from_path(std::string path);

  void convert_expression_to_code(exprt& expr);

  void move_symbol_to_context(symbolt &symbol);

  void check_symbol_redefinition(
    symbolt &old_symbol,
    symbolt &new_symbol);

  bool convert_character_literal(
    const clang::CharacterLiteral &char_literal,
    exprt &dest);

  bool convert_string_literal(
    const clang::StringLiteral &string_literal,
    exprt &dest);

  bool convert_integer_literal(
    const clang::IntegerLiteral &integer_literal,
    exprt &dest);

  bool convert_float_literal(
    const clang::FloatingLiteral &floating_literal,
    exprt &dest);

  std::string parse_float(
    llvm::SmallVector<char, 32> &src,
    mp_integer &significand,
    mp_integer &exponent);

  bool search_add_type_map(
    const clang::TagDecl &tag,
    type_mapt::iterator &type_it);

  const clang::Decl* get_DeclContext_from_Stmt(
    const clang::Stmt &stmt);

  const clang::FunctionDecl* get_top_FunctionDecl_from_Stmt(
    const clang::Stmt &stmt);

  bool convert_this_decl(const clang::Decl &decl);
};

#endif /* LLVM_FRONTEND_LLVM_CONVERT_H_ */
