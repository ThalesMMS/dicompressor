#include "core/job_scheduler.hpp"

#include <atomic>
#include <mutex>

#include "util/thread_pool.hpp"

namespace htj2k::core {

JobScheduler::JobScheduler(const std::size_t workers) : workers_(workers == 0 ? 1 : workers) {}

std::vector<JobResult> JobScheduler::run(const std::vector<std::filesystem::path>& jobs,
                                         RuntimeStats& stats,
                                         const std::function<JobResult(const std::filesystem::path&)>& fn) const
{
  if (jobs.empty()) {
    return {};
  }

  util::ThreadPool pool(workers_);
  std::atomic<std::size_t> next_index{0};
  std::mutex results_mutex;
  std::vector<JobResult> results;
  results.reserve(jobs.size());

  for (std::size_t worker = 0; worker < workers_; ++worker) {
    pool.enqueue([&] {
      for (;;) {
        const auto index = next_index.fetch_add(1);
        if (index >= jobs.size()) {
          return;
        }
        auto result = fn(jobs[index]);
        stats.completed.fetch_add(1);
        if (result.status == "ok") {
          stats.ok.fetch_add(1);
        } else if (result.status == "copied") {
          stats.copied.fetch_add(1);
        } else if (result.status == "failed") {
          stats.failed.fetch_add(1);
        }
        stats.frames.fetch_add(result.frames);
        stats.pixels.fetch_add(result.pixels);
        stats.bytes_read.fetch_add(result.bytes_read);
        stats.bytes_written.fetch_add(result.bytes_written);

        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(std::move(result));
      }
    });
  }

  pool.wait_idle();
  return results;
}

}  // namespace htj2k::core
