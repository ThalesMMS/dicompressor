#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include "core/types.hpp"

namespace htj2k::core {

class JobScheduler {
public:
  explicit JobScheduler(std::size_t workers);

  [[nodiscard]] std::vector<JobResult> run(const std::vector<std::filesystem::path>& jobs,
                                           RuntimeStats& stats,
                                           const std::function<JobResult(const std::filesystem::path&)>& fn) const;

private:
  std::size_t workers_ = 1;
};

}  // namespace htj2k::core
