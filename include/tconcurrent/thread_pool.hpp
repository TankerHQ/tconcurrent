#ifndef TCONCURRENT_THREAD_POOL_H
#define TCONCURRENT_THREAD_POOL_H

#include <atomic>
#include <memory>
#include <thread>
#include <functional>

#include <tconcurrent/detail/export.hpp>

#ifdef _MSC_VER
#pragma warning(push)
// remove dll-interface warning
#pragma warning(disable:4251)
#endif

namespace boost
{
namespace asio
{
class io_service;
}
}

namespace tconcurrent
{
namespace detail
{
TCONCURRENT_EXPORT
void default_error_cb(std::exception_ptr const&);
}

class TCONCURRENT_EXPORT thread_pool
{
public:
  using error_handler_cb = std::function<void(std::exception_ptr const&)>;
  using task_trace_handler_cb = std::function<void(
      std::string const& name, std::chrono::steady_clock::duration dur)>;

  thread_pool(thread_pool const&) = delete;
  thread_pool(thread_pool&&) = delete;
  thread_pool& operator=(thread_pool const&) = delete;
  thread_pool& operator=(thread_pool&&) = delete;

  thread_pool();
  ~thread_pool();

  void start(unsigned int thread_count);
  void stop();

  bool is_running() const;

  bool is_in_this_context() const;
  bool is_single_threaded() const;

  boost::asio::io_service& get_io_service();

  /** Call this function to become a worker of this threadpool
   * This call will not return until the threadpool is destroyed.
   */
  void run_thread();

  void post(std::function<void()> work, std::string name = {});

  void set_error_handler(error_handler_cb cb);
  void signal_error(std::exception_ptr const& e);
  void set_task_trace_handler(task_trace_handler_cb cb);

private:
  struct impl;
  std::unique_ptr<impl> _p;
};

TCONCURRENT_EXPORT thread_pool& get_default_executor();
TCONCURRENT_EXPORT void start_thread_pool(unsigned int thread_count);
TCONCURRENT_EXPORT thread_pool& get_background_executor();

/// Executor that runs its work in-place
class synchronous_executor
{
public:
  template <typename F>
  void post(F&& work, std::string const& = {})
  {
    work();
  }
};

// FIXME do an abstraction so that executors can be passed by value
TCONCURRENT_EXPORT synchronous_executor& get_synchronous_executor();

}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
