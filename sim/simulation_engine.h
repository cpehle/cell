#pragma once

#include <string>
#include <memory>
#include <vector>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>

#include "sim/llvm_codegen.h"
#include "ir/namespace.h"

namespace sim {

  class Simulation_engine {
    public:
      Simulation_engine(std::string const& filename,
          std::string const& toplevel);

      void setup();
      bool simulate_cycle();
      void teardown();


    private:
      struct Process {
        llvm::Function* function;
        void* exe_ptr;
      };

      typedef std::vector<Process> Process_list;

      struct Module {
        void* this_in;
        void* this_out;
        llvm::StructLayout const* layout;
        Process_list processes;
        unsigned int num_elements;
      };

      typedef std::vector<Module> Module_list;



      llvm::ExecutionEngine* m_exe = nullptr;
      llvm::DataLayout const* m_layout = nullptr;
      std::shared_ptr<sim::Llvm_codegen> m_code;
      ir::Namespace m_top_ns;
      std::shared_ptr<ir::Module> m_top_mod;
      Module_list m_modules;

      void init(std::string const& filename, std::string const& toplevel);
  };

}


/* vim: set et fenc= ff=unix sts=0 sw=2 ts=2 : */
