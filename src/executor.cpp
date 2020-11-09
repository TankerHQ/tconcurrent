#include <tconcurrent/executor.hpp>

#include <tconcurrent/thread_pool.hpp>

namespace tconcurrent
{
namespace
{
thread_pool* global_single_thread = nullptr;
thread_pool* global_thread_pool = nullptr;
}

thread_pool& get_global_single_thread()
{
  static auto _ = [] { return global_single_thread = new thread_pool; }();
  return *global_single_thread;
}

thread_pool& get_global_thread_pool()
{
  static auto _ = [] { return global_thread_pool = new thread_pool; }();
  return *global_thread_pool;
}

void shutdown()
{
  delete global_single_thread;
  delete global_thread_pool;
}

namespace
{
thread_pool& start_thread_pool(unsigned int thread_count)
{
  auto& tp = get_global_thread_pool();
  if (!tp.is_running())
    tp.start(thread_count);
  return tp;
}

thread_pool& start_single_thread()
{
  auto& tp = get_global_single_thread();
  if (!tp.is_running())
    tp.start(1);
  return tp;
}
}

executor get_default_executor()
{
  static auto& tp = start_single_thread();
  return tp;
}

executor get_background_executor()
{
  static auto& tp = start_thread_pool(std::thread::hardware_concurrency());
  return tp;
}
}
