#include "llvm_module_scanner.h"

#include "sim/llvm_function_scanner.h"
#include "sim/llvm_constexpr_scanner.h"
#include "ir/find.hpp"
#include "ir/find_hierarchy.h"
#include "ir/builtins.h"


#include <iostream>


namespace sim {

  using Module_scanner = ir::Module_scanner<Llvm_impl>;


  Llvm_module_scanner::Llvm_module_scanner(Llvm_module& mod)
    : Module_scanner(mod) {
    using namespace llvm;

    // prepare module type
    auto lib = find_library(mod);
    auto hier_name = hierarchical_name(mod, "mod");
    mod.impl.mod_type = llvm::StructType::create(lib->impl.context, hier_name);

    // create ctor
    //mod.impl.ctor = Function::Create(
        //FunctionType::get(mod.impl.mod_type, false),
        //Function::ExternalLinkage,
        //hierarchical_name(mod, "ctor"),
        //lib->impl.module.get());

    // register scanner callback functions
    this->template on_leave_if_type<ast::Module_def>(&Llvm_module_scanner::leave_module);

    // reserve slot 0 for the 'port' object
    m_member_types.resize(1);
  }


  bool
  Llvm_module_scanner::insert_function(ast::Function_def const& node) {
    LOG4CXX_TRACE(m_logger, "inserting function");

    std::shared_ptr<Llvm_function> func = create_function(node);

    m_ns.functions.insert(std::make_pair(func->name, func));

    // code generation at leave of module node
    m_todo_functions.push_back(std::make_tuple(func, &node));

    return false;
  }


  bool
  Llvm_module_scanner::insert_object(ast::Variable_def const& node) {
    auto obj = create_object(node);

    LOG4CXX_TRACE(m_logger, "inserting object '" << obj->name << "'");

    m_mod.objects[obj->name] = obj;

    obj->impl.struct_index = m_member_types.size();

    m_member_types.push_back(obj->type->impl.type);

    return false;
  }


  bool
  Llvm_module_scanner::insert_instantiation(ast::Module_instantiation const& node) {
    auto inst = create_instantiation(node);

    LOG4CXX_TRACE(m_logger, "instantiating module '"
        << inst->module->name
        << "'");

    m_mod.objects.at(inst->name)->impl.struct_index = m_member_types.size();
    m_member_types.push_back(
        llvm::PointerType::getUnqual(inst->module->impl.mod_type)
      );
    //m_todo_insts.push_back(inst);

    return false;
  }


  bool
  Llvm_module_scanner::insert_process(ast::Process const& node) {
    auto proc = create_process(node);
    m_mod.processes.push_back(proc);

    auto func = std::make_shared<Llvm_function>();
    func->name = "__process__";
    func->return_type = ir::Builtins<Llvm_impl>::types.at("unit");
    func->within_module = true;
    m_todo_functions.push_back(std::make_tuple(func, &node));
    proc->function = func;

    return false;
  }


  bool
  Llvm_module_scanner::insert_periodic(ast::Periodic const& node) {
    auto period = dynamic_cast<ast::Phys_literal const&>(node.period());
    auto value = period.value();
    auto unit = dynamic_cast<ast::Identifier const&>(period.unit()).identifier();
    ir::Time::Unit tu;

    if( unit == "ps" ) tu = ir::Time::ps;
    else if( unit == "ns" ) tu = ir::Time::ns;
    else if( unit == "us" ) tu = ir::Time::us;
    else if( unit == "ms" ) tu = ir::Time::ms;
    else if( unit == "s" ) tu = ir::Time::s;
    else
      throw std::runtime_error("Unknown time unit");

    auto per = std::make_shared<ir::Periodic<Llvm_impl>>();
    per->period = ir::Time(value, tu);

    m_mod.periodicals.push_back(per);

    auto func = std::make_shared<Llvm_function>();
    func->name = "__periodic__";
    func->return_type = ir::Builtins<Llvm_impl>::types.at("unit");
    func->within_module = true;
    m_todo_functions.push_back(std::make_tuple(func, &node));
    per->function = func;

    return false;
  }


  bool
  Llvm_module_scanner::insert_once(ast::Once const& node) {
    auto period = dynamic_cast<ast::Phys_literal const&>(node.time());
    auto value = period.value();
    auto unit = dynamic_cast<ast::Identifier const&>(period.unit()).identifier();
    ir::Time::Unit tu;

    if( unit == "ps" ) tu = ir::Time::ps;
    else if( unit == "ns" ) tu = ir::Time::ns;
    else if( unit == "us" ) tu = ir::Time::us;
    else if( unit == "ms" ) tu = ir::Time::ms;
    else if( unit == "s" ) tu = ir::Time::s;
    else
      throw std::runtime_error("Unknown time unit");

    auto once = std::make_shared<ir::Once<Llvm_impl>>();
    once->time = ir::Time(value, tu);
    m_mod.onces.push_back(once);

    auto func = std::make_shared<Llvm_function>();
    func->name = "__once__";
    func->return_type = ir::Builtins<Llvm_impl>::types.at("unit");
    func->within_module = true;
    m_todo_functions.push_back(std::make_tuple(func, &node));
    once->function = func;

    return false;
  }


  bool
  Llvm_module_scanner::insert_recurrent(ast::Recurrent const& node) {
    auto rec = std::make_shared<Llvm_recurrent>();
    rec->time_id = dynamic_cast<ast::Identifier const&>(node.time_id()).identifier();
    m_mod.recurrents.push_back(rec);

    auto param = std::make_shared<Llvm_object>();
    param->name = rec->time_id;
    param->type = ir::Builtins<Llvm_impl>::types.at("int");

    auto func = std::make_shared<Llvm_function>();
    func->name = "__recurrent__";
    func->return_type = ir::Builtins<Llvm_impl>::types.at("int");
    func->parameters.push_back(param);
    func->within_module = true;
    m_todo_functions.push_back(std::make_tuple(func, &node));
    rec->function = func;

    return false;
  }


  bool
  Llvm_module_scanner::leave_module(ast::Module_def const& node) {
    // add socket type
    m_mod.objects.at("port")->impl.struct_index = 0;
    m_member_types[0] = m_mod.socket->impl.type;

    m_mod.impl.mod_type->setBody(m_member_types);

    for(auto f : m_todo_functions) {
      Llvm_function_scanner scanner(m_mod, *std::get<0>(f));
      std::get<1>(f)->accept(scanner);
    }

    m_todo_functions.clear();

    return true;
  }


  bool
  Llvm_module_scanner::insert_socket(ast::Socket_def const& node) {
    auto sock = create_socket(node);
    auto lib = find_library(m_ns);
    auto hier_name = hierarchical_name(m_ns, sock->name);
    auto ty = llvm::StructType::create(lib->impl.context, hier_name);

    std::size_t i = 0;
    std::vector<llvm::Type*> port_tys;
    port_tys.reserve(sock->elements.size());
    for(auto p : sock->elements) {
      p.second->impl.struct_index = i++;
      port_tys.push_back(p.second->type->impl.type);
    }

    ty->setBody(port_tys);
    sock->Type::impl.type = ty;
    LOG4CXX_DEBUG(m_logger, "created socket definition for '" << sock->name << "'");

    m_mod.socket = sock;

    return false;
  }


  bool
  Llvm_module_scanner::insert_constant(ast::Constant_def const& node) {
    LOG4CXX_TRACE(m_logger, "Llvm_module_scanner::insert_constant");

    auto c = create_constant(node);
    m_mod.constants[c->name] = c;

    Llvm_constexpr_scanner scanner(c, m_ns);
    node.accept(scanner);

    //auto cnst_int = dynamic_cast<llvm::ConstantInt*>(c->impl.expr);
    //if( cnst_int )
      //LOG4CXX_TRACE(m_logger, "constant evaluates to "
          //<< cnst_int->getSExtValue());
    if( c->type == ir::Builtins<Llvm_impl>::types["int"] )
      LOG4CXX_TRACE(m_logger, "constant integer: "
          << c->impl.expr->getUniqueInteger().getLimitedValue());

    return false;
  }


  bool
  Llvm_module_scanner::insert_table(ast::Table_def const& node) {
    auto ty = create_table_type(node);
    m_ns.types[ty.first->name] = ty.first;
    m_ns.namespaces[ty.second->name] = ty.second;

    auto cnst_it = ty.first->allowed_values.begin();
    for(size_t i=0; i<ty.first->allowed_values.size(); ++i) {
      auto cnst = (cnst_it++)->second;
      auto item = node.items().at(i);

      LOG4CXX_TRACE(m_logger, "creating constant for "
          << ty.first->name << "::" << cnst->name);

      Llvm_constexpr_scanner scanner(cnst, m_ns);
      item.second->accept(scanner);
    }

    return false;
  }


  bool
  Llvm_module_scanner::insert_module(ast::Module_def const& mod) {
    auto m = create_module(mod);
    LOG4CXX_TRACE(m_logger, "Llvm_module_scanner::insert_module for '"
        << m->name << "'");

    Llvm_module_scanner scanner(*m);
    mod.accept(scanner);
    m_mod.modules[m->name] = m;

    return false;
  }


  std::shared_ptr<Llvm_module>
  Llvm_module_scanner::instantiate_module_template(ir::Label inst_name,
      ast::Module_def const* node,
      std::map<ir::Label,std::shared_ptr<Llvm_type>> const& args) {
    LOG4CXX_TRACE(this->m_logger, "creating module '"
        << inst_name << "' from template");

    auto existing = m_mod.modules.find(inst_name);
    if( existing != m_mod.modules.end() )
      return existing->second;

    auto m = std::make_shared<Llvm_module>(inst_name);
    m->enclosing_ns = &m_mod;
    m->enclosing_library = m_mod.enclosing_library;


    // inject type arguments
    for(auto ty : args) {
      LOG4CXX_TRACE(this->m_logger, "overriding type parameter '"
          << ty.first << "' with type '"
          << ty.second->name << "'");
      m->types[ty.first] = ty.second;
    }

    Llvm_module_scanner scanner(*m);
    node->accept(scanner);
    m_mod.modules[m->name] = m;

    return m;
  }


  std::shared_ptr<Llvm_type>
  Llvm_module_scanner::create_array_type(ast::Array_type const& node) {
    // process constant expression to determine size
    auto sz_cnst = std::make_shared<Llvm_constant>();
    Llvm_constexpr_scanner scanner(sz_cnst, m_ns);
    node.size_expr().accept(scanner);

    sz_cnst = scanner.constant_of_node(node.size_expr());

    if( !sz_cnst->type ) {
      std::stringstream strm;
      strm << node.location()
        << ": No inferred type for array size expression.";
      throw std::runtime_error(strm.str());
    }

    // get size from constant expression
    if( sz_cnst->type != ir::Builtins<Llvm_impl>::types["int"] ) {
      std::stringstream strm;
      strm << node.location()
        << ": Constant for array size is not of type 'int'"
        << " (is of type '"
        << sz_cnst->type->name
        << "')";
      throw std::runtime_error(strm.str());
    }

    auto sz = sz_cnst->impl.expr->getUniqueInteger().getLimitedValue();


    if( typeid(node.base_type()) == typeid(ast::Array_type) ) {
      auto& base_type = dynamic_cast<ast::Array_type const&>(node.base_type());
      auto bt = this->create_array_type(base_type);

      std::stringstream strm;
      strm << bt->name << "[" << sz << "]";
      auto new_type = std::make_shared<Llvm_type>();
      new_type->name = strm.str();
      new_type->array_size = sz;
      new_type->array_base_type = bt;
      new_type->impl.type = llvm::ArrayType::get(bt->impl.type,
          new_type->array_size);

      return new_type;
    } else if( typeid(node.base_type()) == typeid(ast::Qualified_name) ) {
      auto const& base_type = dynamic_cast<ast::Qualified_name const&>(node.base_type());
      auto type_name = base_type.name();
      std::shared_ptr<Llvm_type> ir_type;
      if( type_name.size() > 1 ) {
        ir_type = find_by_path(m_ns,
            &Llvm_namespace::types,
            type_name);
      } else {
        ir_type = find_type(m_ns, type_name[0]);
      }

      if( !ir_type ) {
        std::stringstream strm;
        auto type_name_join = boost::algorithm::join(type_name, "::");
        strm << node.location()
          << ": typename '" << type_name_join << "' not found.";
        throw std::runtime_error(strm.str());
      }

      std::stringstream strm;
      strm << ir_type->name << "[" << sz << "]";
      auto new_type = std::make_shared<Llvm_type>();
      new_type->name = strm.str();
      new_type->array_base_type = ir_type;
      new_type->array_size = sz;
      new_type->impl.type = llvm::ArrayType::get(ir_type->impl.type,
          new_type->array_size);

      return new_type;
    } else {
      throw std::runtime_error("Array base type is neither array nor identifier.");
    }
  }

}

/* vim: set et ff=unix sts=0 sw=2 ts=2 : */
