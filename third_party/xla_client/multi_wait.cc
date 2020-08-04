#include "tensorflow/compiler/xla/xla_client/multi_wait.h"

#include <chrono>
#include <exception>

namespace xla {
namespace util {

void MultiWait::Done() {
  bool notify = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    completed_count_ += 1;
    notify = completed_count_ == count_;
  }
  if (notify) {
    cv_.notify_all();
  }
}

void MultiWait::Wait() {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this] { return completed_count_ >= count_; });
  if (exptr_ != nullptr) {
    std::rethrow_exception(exptr_);
  }
}

void MultiWait::Wait(double wait_seconds) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (!cv_.wait_for(lock, std::chrono::duration<double>(wait_seconds),
                    [this] { return completed_count_ >= count_; })) {
    throw std::runtime_error("Timeout");
  }
  if (exptr_ != nullptr) {
    std::rethrow_exception(exptr_);
  }
}

void MultiWait::Reset(size_t count) {
  std::lock_guard<std::mutex> lock(mutex_);
  count_ = count;
  completed_count_ = 0;
  exptr_ = nullptr;
}

std::function<void()> MultiWait::Completer(std::function<void()> func) {
  auto completer = [this, func = std::move(func)]() {
    try {
      func();
    } catch (...) {
      std::lock_guard<std::mutex> lock(mutex_);
      exptr_ = std::current_exception();
    }
    Done();
  };
  return completer;
}

std::function<void()> MultiWait::Completer(std::shared_ptr<MultiWait> mwait,
                                           std::function<void()> func) {
  auto completer = [mwait = std::move(mwait), func = std::move(func)]() {
    try {
      func();
    } catch (...) {
      std::lock_guard<std::mutex> lock(mwait->mutex_);
      mwait->exptr_ = std::current_exception();
    }
    mwait->Done();
  };
  return completer;
}

}  // namespace util
}  // namespace xla
