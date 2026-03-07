#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace htj2k::util {

class ThreadPool {
public:
  explicit ThreadPool(std::size_t workers);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  void enqueue(std::function<void()> job);
  void wait_idle();
  [[nodiscard]] std::size_t size() const { return workers_.size(); }

private:
  void worker_loop();

  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> jobs_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::condition_variable idle_cv_;
  bool stopping_ = false;
  std::size_t active_jobs_ = 0;
};

}  // namespace htj2k::util
