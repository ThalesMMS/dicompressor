#pragma once

#include <stdexcept>
#include <string>

#include "tests/test_registry.hpp"

#define HTJ2K_TEST(name)                                                                        \
  static void name();                                                                           \
  namespace {                                                                                   \
  struct name##_registrar {                                                                     \
    name##_registrar() { ::htj2k::tests::register_test(#name, &name); }                        \
  } name##_registrar_instance;                                                                  \
  }                                                                                             \
  static void name()

#define HTJ2K_ASSERT(cond)                                                                      \
  do {                                                                                          \
    if (!(cond)) {                                                                              \
      throw std::runtime_error(std::string("assertion failed: ") + #cond);                     \
    }                                                                                           \
  } while (false)

#define HTJ2K_ASSERT_EQ(lhs, rhs)                                                               \
  do {                                                                                          \
    if (!((lhs) == (rhs))) {                                                                    \
      throw std::runtime_error(std::string("assertion failed: ") + #lhs + " == " + #rhs);     \
    }                                                                                           \
  } while (false)
