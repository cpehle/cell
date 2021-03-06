#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/JIT.h>
#include "sim/simulation_engine.h"
#include "sim/stream_instrumenter.h"
#include "sim/vcd_instrumenter.h"
#include "logging/logger.h"

#include <gtest/gtest.h>

class Simulator_test : public ::testing::Test {
  protected:
    virtual void SetUp() {
      init_logging();
    }
};


TEST_F(Simulator_test, basic_process) {
  sim::Simulation_engine engine("../lib/test/basic_process.cell",
      "test::basic_process");

  engine.setup();
  auto intro = engine.inspect_module("");

  EXPECT_EQ(1, intro.get<int64_t>("a"));

  engine.simulate(ir::Time(10, ir::Time::ns));

  EXPECT_EQ(1, intro.get<int64_t>("a"));
  EXPECT_EQ(2, intro.get<int64_t>("b"));
  EXPECT_EQ(3, intro.get<int64_t>("c"));

  std::cout << "Bits for a: "
    << intro.get_bits(0)
    << std::endl;

  engine.teardown();
}


TEST_F(Simulator_test, basic_periodic) {
  sim::Simulation_engine engine("../lib/test/basic_periodic.cell", "test::basic_periodic");

  engine.setup();
  engine.simulate(ir::Time(10, ir::Time::ns));

  auto intro = engine.inspect_module("");
  auto counter = intro.get<int64_t>("counter");
  auto acc = intro.get<int64_t>("acc");

  EXPECT_EQ(acc, counter);

  engine.teardown();
}


TEST_F(Simulator_test, basic_fsm) {
  sim::Simulation_engine engine("../lib/test/basic_fsm.cell", "test");

  engine.setup();
  auto intro = engine.inspect_module("");

  auto ctr = intro.get<int64_t>("ctr");
  auto state = intro.get<int64_t>("state");

  std::cout << "Module data members:\n";
  for(auto obj : intro.module()->objects) {
    std::cout << "   " << obj.first << '\n';
  }
  std::cout << std::endl;

  auto module_bits = intro.get_bits();
  std::cout << "module: " << module_bits << std::endl;
  auto reset_bits = intro.get_bits("reset");
  std::cout << "reset: " << reset_bits << std::endl;
  auto clk_bits = intro.get_bits("clk");
  std::cout << "clk: " << clk_bits << std::endl;

  EXPECT_TRUE(reset_bits[0]);
  EXPECT_TRUE(intro.get<bool>("reset"));
  EXPECT_FALSE(intro.get<bool>("clk"));
  EXPECT_EQ(0, ctr);
  EXPECT_EQ(1, state);

  engine.simulate(ir::Time(100, ir::Time::ns));

  ctr = intro.get<int64_t>("ctr");
  state = intro.get<int64_t>("state");

  EXPECT_EQ(11, ctr);
  EXPECT_EQ(3, state);

  engine.teardown();
}


TEST_F(Simulator_test, empty_module) {
  sim::Simulation_engine engine("../lib/test/empty_module.cell", "test::empty_module");

  engine.setup();
  engine.simulate(ir::Time(10, ir::Time::ns));
  engine.teardown();
}


TEST_F(Simulator_test, functions) {
  using namespace std::placeholders;

  sim::Simulation_engine engine("../lib/test/functions.cell", "m");

  engine.setup();

  int tmp;
  bool tmpb;
  auto insp = engine.inspect_module("");

  auto addf = std::bind(insp.get_function_ptr<int(char*,char*,char*,char*,int,int)>("add"),
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      _1,
      _2);

  ASSERT_EQ(addf(1, 2), 3);
  ASSERT_EQ(addf(0, 0), 0);
  ASSERT_EQ(addf(-1, 15), 14);

  insp.call(tmp, "add", 1, 2);
  ASSERT_EQ(3, tmp);


  auto testf = std::bind(insp.get_function_ptr<bool(char*,char*,char*,char*,int,int)>("test"),
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      _1,
      _2);

  ASSERT_EQ(testf(0, 0), true);
  ASSERT_EQ(testf(0, 1), false);
  ASSERT_EQ(testf(-500, 2), false);

  insp.call(tmpb, "test", 0, 0);
  ASSERT_EQ(tmpb, true);
  insp.call(tmpb, "test", -500, 2);
  ASSERT_EQ(tmpb, false);


  auto condf = std::bind(insp.get_function_ptr<int(char*,char*,char*,char*,bool,int,int)>("cond"),
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      _1,
      _2,
      _3);

  ASSERT_EQ(condf(true, 1, 2), 1);
  ASSERT_EQ(condf(false, 1, 2), 2);

  insp.call(tmp, "cond", true, 1, 2);
  ASSERT_EQ(tmp, 1);


  auto cond2f = std::bind(insp.get_function_ptr<int(char*,char*,char*,char*,bool,int)>("cond2"),
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      _1,
      _2);

  ASSERT_EQ(cond2f(true, 3), 6);
  ASSERT_EQ(cond2f(false, 3), 3);

  auto facf = std::bind(insp.get_function_ptr<int(char*,char*,char*,char*,int)>("fac"),
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      _1);

  {
    int f = 1;
    for(int i=0; i<10; i++) {
      ASSERT_EQ(facf(i), f);

      insp.call(tmp, "fac", i);
      ASSERT_EQ(f, tmp);

      f *= i+1;
    }
  }

  engine.teardown();
}


TEST_F(Simulator_test, module_access) {
  using namespace std::placeholders;

  sim::Simulation_engine engine("../lib/test/function_in_module.cell",
    "m");

  engine.setup();
  auto insp = engine.inspect_module("");

  insp.call("set_a", 5);
  int a;
  insp.call(a, "get_a");
  ASSERT_EQ(5, a);

  engine.teardown();
}

TEST_F(Simulator_test, basic_logging) {
  sim::Instrumented_simulation_engine engine("../lib/test/basic_periodic.cell", "test::basic_periodic");

  std::stringstream strm;
  sim::Stream_instrumenter instr(strm);
  engine.instrument(instr);

  engine.setup();
  engine.simulate(ir::Time(10, ir::Time::ns));
  engine.teardown();

  std::cout << "Loggin output:\n"
    << strm.str() << std::endl;
}

TEST_F(Simulator_test, vcd_logging) {
  sim::Instrumented_simulation_engine engine("../lib/test/basic_periodic.cell", "test::basic_periodic");

  sim::Vcd_instrumenter instr("simulator_test__vcd_logging.vcd");
  engine.instrument(instr);

  engine.setup();
  engine.simulate(ir::Time(10, ir::Time::ns));
  engine.teardown();
}


TEST_F(Simulator_test, vcd_logging_fsm) {
  sim::Instrumented_simulation_engine engine("../lib/test/basic_fsm.cell",
      "test");

  sim::Vcd_instrumenter instr("simulator_test__vcd_logging_fsm.vcd");
  engine.instrument(instr);

  engine.setup();
  engine.simulate(ir::Time(100, ir::Time::ns));
  engine.teardown();
}

TEST_F(Simulator_test, constants) {
  sim::Simulation_engine engine("../lib/test/constants.cell", "test::m");

  engine.setup();
  auto insp = engine.inspect_module("");
  engine.simulate(ir::Time(10, ir::Time::ns));
  EXPECT_EQ(55, insp.get<int64_t>("x"));
  EXPECT_EQ(42, insp.get<int64_t>("x_ref"));
  EXPECT_EQ(4.2, insp.get<double>("x_float"));
  EXPECT_EQ(true, insp.get<bool>("x_bool"));
  EXPECT_EQ(false, insp.get<bool>("y_bool"));
  EXPECT_EQ(55, insp.get<int64_t>("y"));
  EXPECT_EQ(15, insp.get<int64_t>("x_auto_int"));
  engine.teardown();
}


//TEST_F(Simulator_test, basic_array) {
  //sim::Simulation_engine engine("../lib/test/basic_array.cell", "test.basic_array");

  //engine.setup();
  //engine.simulate(ir::Time(10, ir::Time::ns));
  //engine.teardown();
//}


TEST_F(Simulator_test, tables) {
  sim::Simulation_engine engine("../lib/test/table.cell", "n::m");

  engine.setup();
  auto insp = engine.inspect_module("");
  engine.simulate(ir::Time(10, ir::Time::ns));
  EXPECT_EQ(1, insp.get<int64_t>("x"));
  EXPECT_EQ(3, insp.get<int64_t>("y"));
  engine.teardown();
}


TEST_F(Simulator_test, loops) {
  sim::Simulation_engine engine("../lib/test/for.cell", "m");

  engine.setup();
  engine.simulate(ir::Time(10, ir::Time::ns));
  auto insp = engine.inspect_module("");
  EXPECT_EQ(10, insp.get<int64_t>("s"));
  EXPECT_EQ(10, insp.get<int64_t>("s2"));
  engine.teardown();
}


TEST_F(Simulator_test, overloaded_functions) {
  sim::Simulation_engine engine("../lib/test/overloaded_function.cell", "m");

  engine.setup();
  engine.simulate(ir::Time(10, ir::Time::ns));
  auto insp = engine.inspect_module("");
  EXPECT_EQ(1, insp.get<int64_t>("res"));
  EXPECT_EQ(2, insp.get<int64_t>("res2"));
  engine.teardown();
}


TEST_F(Simulator_test, imports) {
  sim::Simulation_engine engine("../lib/test/imports.cell", "m");

  engine.setup();
  engine.simulate(ir::Time(10, ir::Time::ns));
  auto insp = engine.inspect_module("");
  EXPECT_EQ(12, insp.get<int64_t>("a"));
  EXPECT_EQ(6, insp.get<int64_t>("b"));
  EXPECT_EQ(30, insp.get<int64_t>("c"));
  EXPECT_EQ(15, insp.get<int64_t>("d"));
  engine.teardown();
}

