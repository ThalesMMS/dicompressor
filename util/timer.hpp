#pragma once

#include <chrono>

namespace htj2k::util {

class ScopedTimer {
public:
  ScopedTimer() = default;

  [[nodiscard]] double elapsed_seconds() const
  {
    const auto now = clock::now();
    return std::chrono::duration<double>(now - started_at_).count();
  }

  void reset() { started_at_ = clock::now(); }

private:
  using clock = std::chrono::steady_clock;
  clock::time_point started_at_ = clock::now();
};

}  // namespace htj2k::util
