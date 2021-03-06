#include "sim/simulation_engine.h"

#include <iomanip>
#include <algorithm>
#include <iterator>
#include <list>
#include <cstdlib>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/IR/Verifier.h>
//#include <llvm/Assembly/PrintModulePass.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Support/raw_os_ostream.h>
#include <boost/filesystem.hpp>

#include "parse_driver.h"
#include "sim/llvm_namespace_scanner.h"
#include "ast/ast_printer.h"
#include "sim/llvm_builtins.h"
#include "ir/find_hierarchy.h"
#include "sim/runtime.h"

namespace sim {



  Simulation_engine::Simulation_engine(std::string const& filename,
      std::string const& toplevel) {
    init(filename, std::vector<std::string>());
    set_toplevel(toplevel);

    LOG4CXX_INFO(m_logger, "initialized simulation using file '"
        << filename
        << "' with top module '"
        << toplevel
        << "'");
  }


  Simulation_engine::Simulation_engine(std::string const& filename,
      std::string const& toplevel,
      std::vector<std::string> const& lookup_path) {
    init(filename, lookup_path);
    set_toplevel(toplevel);

    LOG4CXX_INFO(m_logger, "initialized simulation using file '"
        << filename
        << "' with top module '"
        << toplevel
        << "'");
  }


  Simulation_engine::Simulation_engine(std::string const& filename) {
    init(filename, std::vector<std::string>());

    LOG4CXX_INFO(m_logger, "initialized simulation using file '"
        << filename
        << "'");
  }


  Simulation_engine::Simulation_engine(std::string const& filename,
      std::vector<std::string> const& lookup_path) {
    init(filename, lookup_path);

    LOG4CXX_INFO(m_logger, "initialized simulation using file '"
        << filename
        << "'");
  }


  Simulation_engine::~Simulation_engine() {
    teardown();
  }


  void
  Simulation_engine::init(std::string const& filename,
      std::vector<std::string> const& lookup_path) {
    using namespace llvm;
    using namespace std;
    namespace bf = boost::filesystem;

    m_logger = log4cxx::Logger::getLogger("cell.sim");

    Parse_driver driver;
    if( driver.parse(filename) )
      throw std::runtime_error("parse failed");

    m_lib = std::make_shared<ir::Library<sim::Llvm_impl>>();

    m_lib->name = "main";
    m_lib->ns = std::make_shared<sim::Llvm_namespace>();
    m_lib->ns->enclosing_library = m_lib;
    m_lib->impl = sim::create_library_impl(m_lib->name);

    // set-up lookup path
    bf::path file_path(filename);
    m_lib->lookup_path.push_back(file_path.parent_path().string());
    std::copy(lookup_path.begin(),
        lookup_path.end(),
        std::back_inserter(m_lib->lookup_path));


    // LLVM initialization
    llvm::InitializeNativeTarget();

    // init builtins
    init_builtins(m_lib);

    // print AST
    std::stringstream strm_ast;
    ast::Ast_printer printer(strm_ast);
    driver.ast_root().accept(printer);
    LOG4CXX_DEBUG(m_logger, strm_ast.str());

    // generate code
    sim::Llvm_namespace_scanner scanner(*(m_lib->ns));
    driver.ast_root().accept(scanner);

    //verifyModule(*(m_lib->impl.module));

    // create JIT execution engine
    EngineBuilder exe_bld(m_lib->impl.module.get());
    string err_str;
    exe_bld.setErrorStr(&err_str);
    exe_bld.setEngineKind(EngineKind::JIT);

    m_exe = exe_bld.create();
    if( !m_exe ) {
      stringstream strm;
      strm << "Failed to create execution engine!: " << err_str;
      throw std::runtime_error(strm.str());
    }
    // no lookup using dlsym
    m_exe->DisableSymbolSearching(true);


    m_layout = m_exe->getDataLayout();
    m_runset.layout(m_layout);

    // optimize
    optimize();

    // show generated code
    std::stringstream strm_ir;
    //cout << "Generated code:\n=====\n";
    llvm::raw_os_ostream strm_ir_os(strm_ir);
    m_lib->impl.module->print(strm_ir_os, nullptr);
    LOG4CXX_DEBUG(m_logger, strm_ir.str());
    //m_lib->impl.module->dump();
    //cout << "\n====="
      //<< endl;

    m_time.v = 0;
    m_time.magnitude = ir::Time::ps;

  }


  void
  Simulation_engine::setup() {
    using namespace std;

    LOG4CXX_DEBUG(m_logger, "setup for simulation...");


    // add mappings for runtime functions
    if( ir::Builtins<Llvm_impl>::functions.count("print") != 1 )
      throw std::runtime_error("There can only be one builtin print function!");
    {
      auto f = ir::Builtins<Llvm_impl>::functions.find("print")->second;
      m_exe->addGlobalMapping(f->impl.code, (void*)(&print));
    }

    if( ir::Builtins<Llvm_impl>::functions.count("rand") != 1 )
      throw std::runtime_error("There can only be one builtin rand function!");
    {
      auto f = ir::Builtins<Llvm_impl>::functions.find("rand")->second;
      m_exe->addGlobalMapping(f->impl.code, (void*)(&rand));
    }

/*
    // generate wrapper function to setup simulation
    m_code->create_setup(m_top_mod);
    //m_code->emit();

    auto setup_func = m_code->module()->getFunction("setup");
    if( !setup_func ) {
      throw std::runtime_error("failed to find function setup()");
    }

    void* ptr = m_exe->getPointerToFunction(setup_func);
    void(*func)() = (void(*)())(ptr);
    func();

    // Init process list
    // TODO find all processes in hierarchy
    auto root_ptr = m_exe->getPointerToGlobal(m_code->root());
    void* root_b_ptr = static_cast<void*>(static_cast<char*>(root_ptr)
        + m_layout->getTypeAllocSize(m_code->get_module_type(m_top_mod.get())));
*/

    m_runset.add_module(m_exe, m_top_mod);
    m_runset.setup_hierarchy();
    m_runset.call_init(m_exe);

    m_setup_complete = true;

  }


  void
  Simulation_engine::simulate(ir::Time const& duration) {
    LOG4CXX_INFO(m_logger, "simulating " << duration);
    for(ir::Time t=m_time; t<(m_time + duration); ) {
      t = simulate_step(t, duration);
    }
  }


  void
  Simulation_engine::teardown() {
    m_setup_complete = false;
  }


  void
  Simulation_engine::optimize() {
    m_mpm = std::make_shared<llvm::PassManager>();
    //m_mpm->add(llvm::createPrintFunctionPass("function optimization in:",
          //new llvm::raw_os_ostream(std::cout)));
    m_mpm->add(new llvm::DataLayoutPass(*m_layout));
    m_mpm->add(llvm::createBasicAliasAnalysisPass());
    m_mpm->add(llvm::createPromoteMemoryToRegisterPass());
    m_mpm->add(llvm::createInstructionCombiningPass());
    m_mpm->add(llvm::createReassociatePass());
    m_mpm->add(llvm::createGVNPass());
    m_mpm->add(llvm::createCFGSimplificationPass());
    m_mpm->add(llvm::createConstantPropagationPass());
    //m_mpm->add(llvm::createDCEPass());
    m_mpm->add(llvm::createDeadInstEliminationPass());
    //m_mpm->add(llvm::createPrintFunctionPass("function optimization out:",
          //new llvm::raw_os_ostream(std::cout)));

    m_mpm->run(*m_lib->impl.module.get());
  }


  void
  Simulation_engine::set_toplevel(std::string const& toplevel) {
    m_top_mod = find_by_path(*(m_lib->ns), &ir::Namespace<Llvm_impl>::modules, toplevel);
    if( !m_top_mod ) {
      std::stringstream strm;
      strm << "Can not find top level module '"
        << toplevel
        << "'\n";
      strm << "The following modules were found in toplevel namespace '"
        << m_lib->ns->name
        << "':\n";
      for(auto m : m_lib->ns->modules) {
        strm << "    " << m.first << '\n';
      }

      throw std::runtime_error(strm.str());

      return;
    }
  }


  Module_inspector
  Simulation_engine::inspect_module(ir::Label const& name) {
    if( !m_setup_complete )
      throw std::runtime_error("Call Simulation_engine::setup() before Simulation_engine::inspect_module()");

    auto mod = ir::find_instance(m_top_mod, name);
    if( !mod )
      throw std::runtime_error("Could not find requested module");

    auto layout = m_layout->getStructLayout(mod->impl.mod_type);
    auto num_elements = mod->impl.mod_type->getNumElements();

    Module_inspector rv(mod,
        layout,
        num_elements,
        m_exe,
        m_runset);

    return rv;
  }


  ir::Time
  Simulation_engine::simulate_step(ir::Time const& t, ir::Time const& duration) {
    ir::Time next_t = m_time + duration.to_unit(ir::Time::ps);

    LOG4CXX_DEBUG(m_logger, "===== time: " << t << " =====");

    // add timed processes to the run list
    for(auto& mod : m_runset.modules) {
      std::list<Runset::Process_schedule::value_type> new_schedules;
      std::list<Runset::Time_process_map::value_type> new_schedules_recurrent;

      auto timed_procs_range = mod.schedule.equal_range(t);
      for(auto it=timed_procs_range.first;
          it != timed_procs_range.second;
          ++it) {
        auto period = std::get<0>(it->second);
        auto proc = std::get<1>(it->second);
        mod.run_list.insert(proc);

        if( period.v > 0 )
          new_schedules.push_back(std::make_pair(t + period, it->second));
      }

      // execute recurrent processes
      auto recurrent_range = mod.recurrent_schedule.equal_range(t);
      for(auto it=recurrent_range.first;
          it != recurrent_range.second;
          ++it) {
        auto exe_ptr = reinterpret_cast<int64_t(*)(char*, char*, char*,char*,int64_t)>(it->second.exe_ptr);
        LOG4CXX_TRACE(m_logger, "Calling recurrent process ...");
        auto next_t_tmp = exe_ptr(mod.this_out->data(),
            mod.this_in->data(),
            mod.this_prev->data(),
            mod.read_mask->data(),
            t.value(ir::Time::ps));

        ir::Time next_t;
        next_t.v = next_t_tmp;
        next_t.magnitude = ir::Time::ps;
        LOG4CXX_TRACE(m_logger, " next_t = " << next_t);
        new_schedules_recurrent.push_back(std::make_pair(next_t, it->second));
      }

      mod.schedule.erase(timed_procs_range.first, timed_procs_range.second);
      std::move(new_schedules.begin(),
          new_schedules.end(),
          std::inserter(mod.schedule, mod.schedule.begin()));

      mod.recurrent_schedule.erase(recurrent_range.first,
        recurrent_range.second);
      std::move(new_schedules_recurrent.begin(),
        new_schedules_recurrent.end(),
        std::inserter(mod.recurrent_schedule, mod.recurrent_schedule.begin()));

      // select next point in time for simulation
      auto nextit = mod.schedule.upper_bound(m_time);
      if( nextit != mod.schedule.end() )
        next_t = std::min(next_t, nextit->first);

      auto nextit_rec = mod.recurrent_schedule.upper_bound(m_time);
      if( nextit_rec != mod.recurrent_schedule.end() )
        next_t = std::min(next_t, nextit_rec->first);
    }

    // simulate cycles until all signals are stable
    unsigned int cycle = 0;
    bool rerun;

    do {
      rerun = simulate_cycle(t);
    } while( (cycle++ < max_cycles) && rerun );

    if( cycle >= max_cycles )
      LOG4CXX_ERROR(m_logger, "Exceeded max number of cycles. Probably a loop.");

    return next_t;
  }

  bool
  Simulation_engine::simulate_cycle(ir::Time const& t) {
    using namespace std;

    LOG4CXX_DEBUG(m_logger, "----- simulate cycle -----");

    for(auto& mod : m_runset.modules) {
      LOG4CXX_DEBUG(m_logger, "running "
          << mod.run_list.size()
          << " processes in module "
          << mod.mod->name);

      // call observer/checker code to observe ptr_out
      for(auto& drv : mod.drivers) {
        if( drv )
          drv(t, mod.this_in, mod.this_out, mod.this_prev);
      }

      for(auto const& proc : mod.run_list) {
        LOG4CXX_TRACE(m_logger, "calling process...");
        if( proc.sensitive )
          std::fill(mod.read_mask->begin(), mod.read_mask->end(), 0);
        auto exe_ptr = reinterpret_cast<void (*)(char*, char*, char*,char*)>(proc.exe_ptr);
        exe_ptr(mod.this_out->data(),
            mod.this_in->data(),
            mod.this_prev->data(),
            mod.read_mask->data());

        if( proc.sensitive ) {
          std::stringstream strm;
          strm << "read_mask: " << std::hex;
          for(size_t j=0; j<mod.read_mask->size(); j++)
            strm << setw(2) << setfill('0')
              << static_cast<int>((*(mod.read_mask))[j]) << " ";
          LOG4CXX_DEBUG(m_logger, strm.str());

          // add to sensitivity list
          for(size_t j=0; j<mod.read_mask->size(); j++) {
            if( (*(mod.read_mask))[j] )
              mod.sensitivity[j].insert(proc);
            else
              mod.sensitivity[j].erase(proc);
          }
        }
      }
    }

    // find modified signals
    bool rerun = false;
    std::set<std::shared_ptr<Llvm_module>> port_event;

    for(auto& mod : m_runset.modules) {
      char* ptr_in = mod.this_in->data();
      char* ptr_out = mod.this_out->data();
      auto size = mod.layout->getSizeInBytes();
      mod.run_list.clear();

      bool mod_modified = false;
      for(size_t i=0; i<size; i++) {
        if( ptr_out[i] != ptr_in[i] ) {
          auto elem = mod.layout->getElementContainingOffset(i);
          LOG4CXX_TRACE(m_logger, "found mismatch at offset " << i
              << " belongs to element "
              << elem
              << " of "
              << mod.mod->name);
          mod_modified = true;

          if( elem == 0 ) {
            // port modified
            port_event.insert(mod.mod);
          }

          // add dependant processes to run list
          for(auto const& dep : mod.sensitivity[elem]) {
            mod.run_list.insert(dep);
          }
        }
      }

      //{
        //std::cout << "memory contents after cycle:\n"
          //<< "this_in: " << std::hex;
        //std::cout << reinterpret_cast<int64_t>(mod.this_in->data()) << ": ";
        //for(auto const& c : *(mod.this_in))
          //std::cout << +c << ' ';
        //std::cout << "\nthis_out: " << std::hex;
        //std::cout << reinterpret_cast<int64_t>(mod.this_out->data()) << ": ";
        //for(auto const& c : *(mod.this_out))
          //std::cout << +c << ' ';
        //std::cout << std::endl;
      //}

      // safe this_in to this_prev frame
      std::copy(mod.this_in->begin(),
          mod.this_in->end(),
          mod.this_prev->begin());

      if( mod_modified )
        copy(ptr_out, ptr_out + size, ptr_in);


      if( !mod.run_list.empty() )
        rerun = true;

      //{
        //std::cout << "memory contents after copy:\n"
          //<< "this_in: " << std::hex;
        //std::cout << reinterpret_cast<int64_t>(mod.this_in->data()) << ": ";
        //for(auto const& c : *(mod.this_in))
          //std::cout << +c << ' ';
        //std::cout << "\nthis_out: " << std::hex;
        //std::cout << reinterpret_cast<int64_t>(mod.this_out->data()) << ": ";
        //for(auto const& c : *(mod.this_out))
          //std::cout << +c << ' ';
        //std::cout << std::endl;
      //}
    }

    for(auto& mod : m_runset.modules) {
      for(auto inst : mod.mod->instantiations) {
        if( port_event.count( inst.second->module ) ) {
          auto index = mod.mod->objects.at(inst.first)->impl.struct_index;

          LOG4CXX_TRACE(m_logger, "port event for module '"
              << inst.second->module->name
              << "' in '"
              << mod.mod->name
              << "' inserting "
              << mod.sensitivity[index].size()
              << " processes to runlist");
          // add dependant processes to run list
          for(auto const& dep : mod.sensitivity[index]) {
            mod.run_list.insert(dep);
          }
        }
      }

      if( !mod.run_list.empty() )
        rerun = true;
    }


    /*auto root_ptr = m_exe->getPointerToGlobal(m_code->root());
    cout << "top A:\n" << hex
      << "0x" << setw(16) << setfill('0')
      << static_cast<uint64_t*>(root_ptr)[0]
      << " "
      << "0x" << setw(16) << setfill('0')
      << static_cast<uint64_t*>(root_ptr)[1]
      << endl;

    auto off = m_layout->getTypeAllocSize(m_code->get_module_type(m_top_mod.get()));
    cout << "top B:\n" << hex
      << "0x" << setw(16) << setfill('0')
      << static_cast<uint64_t*>(root_ptr)[off/sizeof(uint64_t)]
      << " "
      << "0x" << setw(16) << setfill('0')
      << static_cast<uint64_t*>(root_ptr)[1 + off/sizeof(uint64_t)]
      << endl;*/

    return rerun;
  }



  //--------------------------------------------------------------------------
  //--------------------------------------------------------------------------


  void
  Instrumented_simulation_engine::setup() {
    Simulation_engine::setup();

    if( m_instrumenter ) {
      setup_module(m_top_mod);

      m_instrumenter->initial(ir::Time(0, ir::Time::ps));
    }
  }


  void
  Instrumented_simulation_engine::setup_module(std::shared_ptr<Llvm_module> mod) {
    auto num_elements = mod->impl.mod_type->getNumElements();
    auto layout = m_layout->getStructLayout(mod->impl.mod_type);
    auto insp = std::make_shared<Module_inspector>(mod,
        layout,
        num_elements,
        m_exe,
        m_runset);
    m_instrumenter->push_hierarchy();
    m_instrumenter->register_module(insp);

    for(auto i : mod->instantiations) {
      ir::Label inst_name;
      std::shared_ptr<Llvm_instantiation> inst;

      std::tie(inst_name, inst) = i;

      setup_module(inst->module);
    }
    m_instrumenter->pop_hierarchy();
  }


  void
  Instrumented_simulation_engine::simulate(ir::Time const& duration) {
    LOG4CXX_INFO(m_logger, "simulating " << duration);

    for(ir::Time t=m_time; t<(m_time + duration); ) {
      ir::Time next_t = simulate_step(t, duration);

      if( m_instrumenter )
        m_instrumenter->step(t);

      t = next_t;
    }
  }

}
