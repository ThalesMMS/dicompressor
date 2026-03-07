#include "util/thread_pool.hpp"

#include <stdexcept>

namespace htj2k::util {

ThreadPool::ThreadPool(const std::size_t workers)
{
  if (workers == 0) {
    throw std::invalid_argument("ThreadPool requires at least one worker");
  }

  workers_.reserve(workers);
  for (std::size_t i = 0; i < workers; ++i) {
    workers_.emplace_back([this] { worker_loop(); });
  }
}

ThreadPool::~ThreadPool()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

void ThreadPool::enqueue(std::function<void()> job)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.push(std::move(job));
  }
  cv_.notify_one();
}

void ThreadPool::wait_idle()
{
  std::unique_lock<std::mutex> lock(mutex_);
  idle_cv_.wait(lock, [this] { return jobs_.empty() && active_jobs_ == 0; });
}

void ThreadPool::worker_loop()
{
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
      if (stopping_ && jobs_.empty()) {
        return;
      }

      job = std::move(jobs_.front());
      jobs_.pop();
      ++active_jobs_;
    }

    try {
      job();
    } catch (...) {
      // Swallow exceptions here. Callers propagate through their own channels.
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      --active_jobs_;
      if (jobs_.empty() && active_jobs_ == 0) {
        idle_cv_.notify_all();
      }
    }
  }
}

}  // namespace htj2k::util
