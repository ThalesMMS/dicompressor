#pragma once

#include <functional>
#include <string>
#include <vector>

namespace htj2k::tests {

using TestFunction = std::function<void()>;

struct TestCase {
  std::string name;
  TestFunction fn;
};

std::vector<TestCase>& registry();
void register_test(const std::string& name, TestFunction fn);

}  // namespace htj2k::tests
