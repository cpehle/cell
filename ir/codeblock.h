#pragma once

#include "namespace.h"
#include "ast/node_if.h"

namespace ir {

  class Codeblock_if {
    public:
      /** Process the AST to generate the code for this codeblock.
       *
       * @param enclosing_ns used for resolution of types and functions.
       * */
      virtual void scan_ast(Namespace& enclosing_ns, 
          ast::Node_if const& tree) = 0;

      /** Append to the list of predefined objects accessible in the codeblock */
      virtual void append_predefined_objects(std::map<Label, std::shared_ptr<Object>> objects) = 0;
  };


  class Codeblock_base : public Codeblock_if {
    public:
      virtual ~Codeblock_base();
  };


  class Null_codeblock : public Codeblock_base {
    public:
      virtual void scan_ast(Namespace& enclosing_ns,
          ast::Node_if const& tree);

      virtual void append_predefined_objects(std::map<Label, std::shared_ptr<Object>> objects);
  };

}

/* vim: set et fenc= ff=unix sts=0 sw=2 ts=2 : */
