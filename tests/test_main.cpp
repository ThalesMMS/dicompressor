#include <exception>
#include <iostream>

#include "tests/test_registry.hpp"

int main()
{
  int failures = 0;
  for (const auto& test : htj2k::tests::registry()) {
    try {
      test.fn();
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception& ex) {
      ++failures;
      std::cerr << "[FAIL] " << test.name << ": " << ex.what() << '\n';
    }
  }
  return failures == 0 ? 0 : 1;
}
