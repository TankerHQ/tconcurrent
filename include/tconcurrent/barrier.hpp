#pragma once

#include <tconcurrent/detail/stdmutex.hpp>

namespace tconcurrent
{
class barrier
{
public:
  barrier(barrier const&) = delete;
  barrier(barrier&&) = delete;
  barrier& operator=(barrier const&) = delete;
  barrier& operator=(barrier&&) = delete;

  barrier(unsigned int target);
  void operator()();

private:
  using mutex = std::mutex;
  using scope_lock = std::unique_lock<mutex>;

  mutex _mutex;
  std::condition_variable _cond;

  const unsigned int _target;
  unsigned int _waiting = 0;
};
}
