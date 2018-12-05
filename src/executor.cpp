#include <tconcurrent/executor.hpp>

#include <tconcurrent/thread_pool.hpp>

namespace tconcurrent
{
namespace
{
thread_pool& get_global_single_thread()
{
  static thread_pool tp;
  return tp;
}

thread_pool& get_global_thread_pool()
{
  static thread_pool tp;
  return tp;
}

void start_thread_pool(unsigned int thread_count)
{
  auto& tp = get_global_thread_pool();
  if (!tp.is_running())
    tp.start(thread_count);
}

void start_single_thread()
{
  auto& tp = get_global_single_thread();
  if (!tp.is_running())
    tp.start(1);
}
}

executor get_default_executor()
{
  start_single_thread();
  return get_global_single_thread();
}

executor get_background_executor()
{
  start_thread_pool(std::thread::hardware_concurrency());
  return get_global_thread_pool();
}
}
