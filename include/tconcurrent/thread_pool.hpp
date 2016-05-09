#ifndef TCONCURRENT_THREAD_POOL_H
#define TCONCURRENT_THREAD_POOL_H

#include <atomic>
#include <memory>
#include <thread>
#include <functional>

#include <boost/asio/io_service.hpp>

namespace tconcurrent
{
namespace detail
{
void default_error_cb(std::exception_ptr const&);
}

class thread_pool
{
public:
  using error_handler_cb = std::function<void(std::exception_ptr const&)>;

  ~thread_pool();

  void start(unsigned int thread_count);
  void stop();

  bool is_running() const
  {
    return _work != nullptr;
  }

  bool is_in_this_context() const;
  bool is_single_threaded() const;

  boost::asio::io_service& get_io_service()
  {
    return _io;
  }

  /** Call this function to become a worker of this threadpool
   * This call will not return until the threadpool is destroyed.
   */
  void run_thread();

  template <typename F>
  void post(F&& work)
  {
    assert(!_dead.load());
    _io.post(std::forward<F>(work));
  }

  void set_error_handler(error_handler_cb cb)
  {
    assert(cb);
    _error_cb = std::move(cb);
  }
  void signal_error(std::exception_ptr const& e)
  {
    _error_cb(e);
  }

private:
  boost::asio::io_service _io;
  std::unique_ptr<boost::asio::io_service::work> _work;
  std::vector<std::thread> _threads;
  std::atomic<bool> _dead{false};

  error_handler_cb _error_cb{detail::default_error_cb};
};

thread_pool& get_default_executor();
void start_thread_pool(unsigned int thread_count);
thread_pool& get_background_executor();

/// Executor that runs its work in-place
class synchronous_executor
{
public:
  template <typename F>
  void post(F&& work)
  {
    work();
  }
};

// FIXME do an abstraction so that executors can be passed by value
synchronous_executor& get_synchronous_executor();

}

#endif
