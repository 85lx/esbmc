/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_ANSI_C_LANGUAGE_H
#define CPROVER_ANSI_C_LANGUAGE_H

#include <language.h>

#include "ansi_c_parse_tree.h"

// Elsewhere:
void init_scanner_pool();
void clear_scanner_pool();

class ansi_c_languaget:public languaget
{
public:
  virtual bool preprocess(
    std::istream &instream,
    const std::string &path,
    std::ostream &outstream,
    message_handlert &message_handler);

  virtual bool parse(
    std::istream &instream,
    const std::string &path,
    message_handlert &message_handler);
             
  virtual bool typecheck(
    contextt &context,
    const std::string &module,
    message_handlert &message_handler);

  virtual bool final(
    contextt &context,
    message_handlert &message_handler);

  virtual bool merge_context( 
    contextt &dest,
    contextt &src, 
    message_handlert &message_handler,
    const std::string &module) const;

  virtual void show_parse(std::ostream &out);
  
  virtual ~ansi_c_languaget();
  ansi_c_languaget() { }
  
  virtual bool from_expr(
    const exprt &expr,
    std::string &code,
    const namespacet &ns);

  virtual bool from_type(
    const typet &type,
    std::string &code,
    const namespacet &ns);

  virtual bool to_expr(
    const std::string &code,
    const std::string &module,
    exprt &expr,
    message_handlert &message_handler,
    const namespacet &ns);
                       
  virtual languaget *new_language()
  { return new ansi_c_languaget; }
   
  virtual std::string id() { return "C"; }
  virtual std::string description() { return "ANSI-C 99"; }

  virtual void modules_provided(std::set<std::string> &modules);  
  
protected:
  ansi_c_parse_treet parse_tree;
  std::string parse_path;
};
 
languaget *new_ansi_c_language();
 
#endif
