#ifndef TCONCURRENT_THREAD_POOL_H
#define TCONCURRENT_THREAD_POOL_H

#include <atomic>
#include <memory>
#include <thread>

#include <function2/function2.hpp>

#include <tconcurrent/detail/boost_fwd.hpp>
#include <tconcurrent/detail/export.hpp>
#include <tconcurrent/future.hpp>

#ifdef _MSC_VER
#pragma warning(push)
// remove dll-interface warning
#pragma warning(disable : 4251)
#endif

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

  using canceler = std::function<void()>;

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

  boost::asio::io_context& get_io_service();

  /** Call this function to become a worker of this threadpool
   * This call will not return until the threadpool is destroyed.
   */
  void run_thread();

  void post(fu2::unique_function<void()> work, std::string name = {});

  void set_error_handler(error_handler_cb cb);
  void signal_error(std::exception_ptr const& e);
  void set_task_trace_handler(task_trace_handler_cb cb);

private:
  struct impl;
  std::unique_ptr<impl> _p;
};
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
