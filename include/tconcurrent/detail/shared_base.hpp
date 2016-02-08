#ifndef TCONCURRENT_DETAIL_SHARED_BASE_HPP
#define TCONCURRENT_DETAIL_SHARED_BASE_HPP

#include <mutex>
#include <condition_variable>
#include <vector>
#include <functional>

#include <boost/variant.hpp>

#include <tconcurrent/thread_pool.hpp>

namespace tconcurrent
{
namespace detail
{

template <typename R>
class shared_base
{
public:
  struct v_none {};
  struct v_value { R value; };
  struct v_exception { std::exception_ptr exc; };

  boost::variant<v_none, v_value, v_exception> _r;

  virtual ~shared_base() = default;

  void set(R&& r)
  {
    finish([&]
           {
             _r = v_value{std::move(r)};
           });
  }

  void set_exception(std::exception_ptr exc)
  {
    finish([&]
           {
             _r = v_exception{exc};
           });
  }

  template <typename F>
  void then(F&& f)
  {
    bool resolved{false};

    {
      std::lock_guard<std::mutex> lock{_mutex};
      if (_r.which() == 0)
        _then.emplace_back(std::forward<F>(f));
      else
        resolved = true;
    }

    if (resolved)
      get_default_executor().post(std::move(f));
  }

  R const& get()
  {
    std::unique_lock<std::mutex> lock{_mutex};
    while (_r.which() == 0)
      _ready.wait(lock);
    if (_r.which() == 2)
      std::rethrow_exception(std::move(boost::get<v_exception>(_r).exc));
    return boost::get<v_value>(_r).value;
  }

  std::exception_ptr const& get_exception()
  {
    std::unique_lock<std::mutex> lock{_mutex};
    while (_r.which() == 0)
      _ready.wait(lock);
    if (_r.which() == 1)
      throw std::logic_error("this future has a value");
    return boost::get<v_exception>(_r).exc;
  }

  void wait() const
  {
    std::unique_lock<std::mutex> lock{_mutex};
    while (_r.which() == 0)
      _ready.wait(lock);
  }

private:
  mutable std::mutex _mutex;
  mutable std::condition_variable _ready;
  std::vector<std::function<void()>> _then;

  template <typename F>
  void finish(F&& setval)
  {
    assert(_r.which() == 0 && "state already set");

    std::vector<std::function<void()>> then;
    {
      std::lock_guard<std::mutex> lock{_mutex};
      setval();
      std::swap(_then, then);
    }

    _ready.notify_all();
    for (auto& f : then)
      get_default_executor().post(std::move(f));
  }
};

}
}

#endif
