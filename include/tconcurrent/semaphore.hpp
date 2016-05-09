#ifndef TCONCURRENT_SEMAPHORE_HPP
#define TCONCURRENT_SEMAPHORE_HPP

#include <tconcurrent/future.hpp>
#include <tconcurrent/concurrent_queue.hpp>

namespace tconcurrent
{

class semaphore
{
public:
  class scope_lock
  {
  public:
    scope_lock(scope_lock const& r) = delete;
    scope_lock& operator=(scope_lock const& r) = delete;
    scope_lock(scope_lock&& r)
      : s(std::exchange(r.s, nullptr))
    {
    }
    scope_lock& operator=(scope_lock&& r)
    {
      s = std::exchange(r.s, nullptr);
      return *this;
    }
    ~scope_lock()
    {
      if (s)
        s->release();
    }

  private:
    semaphore* s;

    scope_lock(semaphore* s)
      : s(s)
    {
    }

    friend semaphore;
  };

  semaphore(unsigned int N)
  {
    for (unsigned int i = 0; i < N; ++i)
      _queue.push({});
  }

  void release()
  {
    _queue.push({});
  }

  future<void> acquire()
  {
    return _queue.pop().and_then(get_synchronous_executor(), [](auto const&){});
  }

  future<scope_lock> get_scope_lock()
  {
    return _queue.pop().and_then(
        get_synchronous_executor(),
        [this](auto const&) { return scope_lock(this); });
  }

  std::size_t count() const
  {
    return _queue.size();
  }

private:
  struct token {};

  concurrent_queue<token> _queue;
};

}

#endif
