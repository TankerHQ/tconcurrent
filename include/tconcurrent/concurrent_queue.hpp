#ifndef TCONCURRENT_CHANNEL_HPP
#define TCONCURRENT_CHANNEL_HPP

#include <cassert>
#include <queue>
#include <vector>

#ifdef __MINGW32__
#include <mingw-threads/mingw.thread.h>
#else
#include <mutex>
#endif

#include <tconcurrent/promise.hpp>

namespace tconcurrent
{

template <typename T>
class concurrent_queue
{
public:
  void push(T val)
  {
    scope_lock l(_mutex);
    if (!_waiters.empty())
    {
      assert(_queue.empty());
      _waiters.front().set_value(std::move(val));
      _waiters.pop();
    }
    else
    {
      _queue.emplace(std::move(val));
    }
  }
  future<T> pop()
  {
    scope_lock l(_mutex);
    if (!_queue.empty())
    {
      assert(_waiters.empty());
      auto ret = make_ready_future(std::move(_queue.front()));
      _queue.pop();
      return ret;
    }
    else
    {
      promise<T> prom;
      _waiters.emplace(prom);
      return prom.get_future();
    }
  }

  std::size_t size() const
  {
    scope_lock l(_mutex);
    return _queue.size();
  }

private:
  using mutex_t = std::mutex;
  using scope_lock = std::lock_guard<mutex_t>;
  mutable mutex_t _mutex;
  std::queue<promise<T>> _waiters;
  std::queue<T> _queue;
};
}

#endif
