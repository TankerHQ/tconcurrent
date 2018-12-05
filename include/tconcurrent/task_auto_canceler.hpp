#ifndef TCONCURRENT_TASK_AUTO_CANCELER_HPP
#define TCONCURRENT_TASK_AUTO_CANCELER_HPP

#include <cassert>
#include <vector>

#include <tconcurrent/future.hpp>
#include <tconcurrent/when.hpp>

namespace tconcurrent
{
class task_auto_canceler
{
public:
  ~task_auto_canceler()
  {
    auto const fut = terminate();
    if (!fut.is_ready())
    {
      assert(false &&
             "destructing a task_auto_canceler that could not be canceled");
      return;
    }
  }

  template <typename Future>
  void add(Future&& future)
  {
    lock_guard _(_mutex);

    if (_terminating)
      throw std::runtime_error(
          "adding a future to terminating task_auto_canceler");

    if (future.is_ready())
      return;

    collect();
    _futures.emplace_back(future.to_void());
  }

private:
  using lock_guard = std::lock_guard<std::mutex>;
  std::mutex _mutex;
  std::vector<tc::future<void>> _futures;
  bool _terminating{false};

  /// Remove ready futures from vector
  void collect()
  {
    _futures.erase(
        std::remove_if(_futures.begin(),
                       _futures.end(),
                       [](auto const& fut) { return fut.is_ready(); }),
        _futures.end());
  }

  future<void> terminate()
  {
    lock_guard _(_mutex);

    _terminating = true;
    for (auto& fut : _futures)
      fut.request_cancel();
    auto ret = when_all(std::make_move_iterator(_futures.begin()),
                        std::make_move_iterator(_futures.end()))
                   .to_void();
    _futures.clear();
    // move ret, otherwise it doesn't compile
    // static_cast to silence a clang warning
    return static_cast<decltype(ret)&&>(ret);
  }
};
}

#endif
