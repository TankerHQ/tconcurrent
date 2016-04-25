#ifndef TCONCURRENT_PERIODIC_TASK_HPP
#define TCONCURRENT_PERIODIC_TASK_HPP

#include <chrono>
#include <mutex>
#include <type_traits>

#include <tconcurrent/future.hpp>

namespace tconcurrent
{

class periodic_task
{
public:
  using duration_type = std::chrono::steady_clock::duration;

  enum StartOption
  {
    no_option,
    start_immediately,
  };

  ~periodic_task();

  void set_period(duration_type period)
  {
    std::lock_guard<std::mutex> l(_mutex);
    _period = period;
  }

  template <typename C>
  auto set_callback(C&& cb) -> typename std::enable_if<
      std::is_same<decltype(cb()), future<void>>::value>::type
  {
    std::lock_guard<std::mutex> l(_mutex);
    _callback = std::forward<C>(cb);
  }

  template <typename C>
  auto set_callback(C&& cb) -> typename std::enable_if<
      !std::is_same<decltype(cb()), future<void>>::value>::type
  {
    std::lock_guard<std::mutex> l(_mutex);
    // TODO cpp14 capture by forward
    _callback = [cb]
    {
      cb();
      return make_ready_future();
    };
  }

  void set_executor(thread_pool* executor)
  {
    _executor = executor;
  }

  void start(StartOption opt = no_option);
  future<void> stop();

  bool is_running() const
  {
    return _state == State::Running;
  }

private:
  using scope_lock = std::lock_guard<std::mutex>;

  enum class State
  {
    Stopped,
    Running,
    Stopping,
  };

  std::mutex _mutex;

  State _state{State::Stopped};

  duration_type _period;
  std::function<future<void>()> _callback;

  future<void> _future;
  std::function<void()> _cancel;

  // TODO very ugly design
  thread_pool* _executor{&get_default_executor()};

  void reschedule();
  future<void> do_call();
};

}

#endif
