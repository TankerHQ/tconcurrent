#ifndef TCONCURRENT_FUTURE_GROUP_HPP
#define TCONCURRENT_FUTURE_GROUP_HPP

#include <vector>
#include <cassert>

#include <tconcurrent/future.hpp>
#include <tconcurrent/when.hpp>

namespace tconcurrent
{
class future_group
{
public:
  ~future_group()
  {
    if (!_terminating)
    {
      assert(false && "destructing a future_group that was not terminated");
      return;
    }
  }

  template <typename Future>
  void add(Future&& future)
  {
    if (_terminating)
      throw std::runtime_error("adding a future to terminating future_group");

    collect();
    _futures.emplace_back(future.to_void());
  }

  future<void> terminate()
  {
    _terminating = true;
    for (auto& fut : _futures)
      fut.request_cancel();
    return when_all(
               std::make_move_iterator(_futures.begin()),
               std::make_move_iterator(_futures.end()))
        .to_void();
  }

private:
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
};
}

#endif
