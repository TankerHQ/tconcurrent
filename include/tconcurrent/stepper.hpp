#pragma once

#ifdef __MINGW32__
#include <mingw-threads/mingw.thread.h>
#include <mingw-threads/mingw.condition_variable.h>
#else
#include <condition_variable>
#include <thread>
#endif

namespace tconcurrent
{
class stepper
{
public:
  stepper(stepper const&) = delete;
  stepper(stepper&&) = delete;
  stepper& operator=(stepper const&) = delete;
  stepper& operator=(stepper&&) = delete;

  stepper() = default;

  void operator()(unsigned int step);

private:
  using mutex = std::mutex;
  using scope_lock = std::unique_lock<mutex>;

  mutex _mutex;
  std::condition_variable _cond;

  unsigned int _next_step = 1;
};
}
