#include "tests/test_registry.hpp"

namespace htj2k::tests {

std::vector<TestCase>& registry()
{
  static auto* tests = new std::vector<TestCase>();
  return *tests;
}

void register_test(const std::string& name, TestFunction fn)
{
  registry().push_back(TestCase{name, std::move(fn)});
}

}  // namespace htj2k::tests
