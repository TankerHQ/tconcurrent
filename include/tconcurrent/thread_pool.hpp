#ifndef TCONCURRENT_THREAD_POOL_H
#define TCONCURRENT_THREAD_POOL_H

#include <memory>
#include <thread>
#include <functional>

#include <boost/asio/io_service.hpp>

namespace tconcurrent
{

class thread_pool
{
public:
  ~thread_pool();

  void start(unsigned int thread_count);
  void stop();

  bool is_running() const
  {
    return _work != nullptr;
  }

  bool is_in_this_context() const;

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
    _io.post(std::forward<F>(work));
  }

private:
  boost::asio::io_service _io;
  std::unique_ptr<boost::asio::io_service::work> _work;
  std::vector<std::thread> _threads;
};

void start_thread_pool(unsigned int thread_count);
thread_pool& get_default_executor();

}

#endif
