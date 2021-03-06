#pragma once

#include "ast/scanner_base.h"

#include "llvm_namespace.h"
#include "ast/ast.h"
#include "ir/find.hpp"
#include "ir/find_hierarchy.h"
#include "logging/logger.h"


namespace sim {

  class Llvm_function_scanner : public ast::Scanner_base<Llvm_function_scanner> {
    public:
      Llvm_function_scanner(Llvm_namespace& ns, Llvm_function& function);
      Llvm_function_scanner(Llvm_module& ns, Llvm_function& function);


    private:
      enum class Lookup_source { in, out, prev };

      typedef std::unordered_map<ast::Node_if const*, llvm::Value*> Node_value_map;
      //typedef std::unordered_map<ir::Label, llvm::AllocaInst*> Name_value_map;
      typedef std::unordered_map<ir::Label, llvm::Value*> Name_value_map;
      typedef std::unordered_map<ast::Node_if const*, std::shared_ptr<Llvm_type>> Node_type_map;
      typedef std::unordered_map<ir::Label, std::shared_ptr<Llvm_type>> Name_type_map;
      typedef std::vector<std::shared_ptr<Llvm_type>> Type_stack;
      typedef std::vector<Lookup_source> Lookup_source_stack;


      Llvm_namespace& m_ns;
      Llvm_module* m_mod = nullptr;
      Llvm_function& m_function;
      llvm::IRBuilder<> m_builder;
      Node_value_map m_values;
      Name_value_map m_named_values;
      Node_type_map m_types;
      Name_type_map m_named_types;
      Type_stack m_type_targets;
      Lookup_source_stack m_lookups;
      log4cxx::LoggerPtr m_logger;


      void init_function();
      void init_scanner();
      llvm::FunctionType* get_function_type(Llvm_function const& function) const;
      llvm::ArrayType* read_mask_type() const;


      // scanner callbacks
      virtual bool insert_return(ast::Return_statement const& node);
      virtual bool enter_variable_ref(ast::Variable_ref const& node);
      virtual bool insert_variable_ref(ast::Variable_ref const& node);
      virtual bool leave_op_element(ast::Op_element const& node);
      virtual bool leave_op_index(ast::Op_index const& node);
      virtual bool enter_name_lookup(ast::Name_lookup const& node);
      virtual bool leave_variable_def(ast::Variable_def const& node);
      virtual bool insert_literal_int(ast::Literal<int> const& node);
      virtual bool insert_literal_double(ast::Literal<double> const& node);
      virtual bool insert_literal_bool(ast::Literal<bool> const& node);
      virtual bool insert_literal_string(ast::Literal<std::string> const& node);
      virtual bool insert_phys_literal(ast::Phys_literal const& node);
      virtual bool enter_op_at(ast::Op_at const& node);
      virtual bool insert_op_at(ast::Op_at const& node);
      virtual bool insert_op_not(ast::Op_not const& node);
      virtual bool insert_op_equal(ast::Op_equal const& node);
      virtual bool insert_op_plus(ast::Op_plus const& node);
      virtual bool insert_op_minus(ast::Op_minus const& node);
      virtual bool insert_op_mult(ast::Op_mult const& node);
      virtual bool insert_op_div(ast::Op_div const& node);
      virtual bool insert_op_mod(ast::Op_mod const& node);
      virtual bool insert_op_and(ast::Op_and const& node);
      virtual bool insert_op_or(ast::Op_or const& node);
      virtual bool insert_op_gt(ast::Op_greater_then const& node);
      virtual bool insert_op_lt(ast::Op_lesser_then const& node);
      virtual bool insert_op_ge(ast::Op_greater_or_equal_then const& node);
      virtual bool insert_op_le(ast::Op_lesser_or_equal_then const& node);
      virtual bool insert_op_bidir(ast::Op_bidir const& node);
      virtual bool enter_assignment(ast::Assignment const& node);
      virtual bool leave_assignment(ast::Assignment const& node);
      virtual bool leave_compound(ast::Compound const& node);
      virtual bool enter_if_statement(ast::If_statement const& node);
      virtual bool leave_function_call(ast::Function_call const& node);
      virtual bool leave_function_def(ast::Function_def const& node);
      virtual bool leave_process(ast::Process const& node);
      virtual bool leave_periodic(ast::Periodic const& node);
      virtual bool leave_once(ast::Once const& node);
      virtual bool leave_recurrent(ast::Recurrent const& node);
      virtual bool enter_while(ast::While_expression const& node);
      virtual bool enter_for(ast::For_expression const& node);


      template<typename Node>
      void insert_bin_op(Node const& node, std::string const& opname) {
        // get types and values
        auto ty_left = m_types.at(&(node.left()));
        auto ty_right = m_types.at(&(node.right()));
        auto v_left = m_values.at(&(node.left()));
        auto v_right = m_values.at(&(node.right()));

        //if( m_type_targets.empty() )
          //throw std::runtime_error("Don't know return type for binary operator");

        //auto ret_ty = m_type_targets.back();

        // check with expected target type
        /*if( !m_type_targets.empty() ) {
          auto ty_target = m_types.at(m_type_targets.back());
          if( ty_target != ret_ty ) {
            std::stringstream strm;
            strm << node.location() << ": expected type '"
              << ty_target->name
              << "' but operator '" << opname << "' returns 'bool' instead";
            throw std::runtime_error(strm.str());
          }
        }*/

        //cout << "insert_op_equal: ["
          //<< ty_left->name
          //<< "] == ["
          //<< ty_right->name
          //<< "] -> [bool]" << endl;

        // select an operator
        std::shared_ptr<Llvm_operator> op = ir::find_operator(m_ns, opname, ty_left, ty_right);
        if( op ) {
          auto v_cmp = op->impl.insert_func(m_builder, v_left, v_right);
          m_values[&node] = v_cmp;
          //m_types[&node] = ret_ty;
          m_types[&node] = op->return_type;
        } else {
          std::stringstream strm;
          strm << node.location() << ": failed to find operator '"
            << opname
            << "' with signature: ["
            << ty_left->name
            << "] " << opname << " ["
            << ty_right->name
            << "] -> ["
            //<< ret_ty->name
            << "]";
          throw std::runtime_error(strm.str());
        }
      }
  };

}

/* vim: set et ff=unix sts=0 sw=2 ts=2 : */
