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
  auto set_callback(C&& cb)
      ->typename std::enable_if<
          std::is_same<decltype(cb()), future<void>>::value>::type
  {
    std::lock_guard<std::mutex> l(_mutex);
    _callback = std::forward<C>(cb);
  }

  template <typename C>
  auto set_callback(C&& cb)
      ->typename std::enable_if<
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

  void start(StartOption opt = no_option);
  future<void> stop();

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
  future<void> _future;
  duration_type _period;
  std::function<future<void>()> _callback;
  std::function<void()> _cancel;

  void reschedule();
  future<void> do_call();
};

}

#endif
