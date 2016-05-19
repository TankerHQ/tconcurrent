#ifndef TCONCURRENT_DETAIL_SHARED_BASE_HPP
#define TCONCURRENT_DETAIL_SHARED_BASE_HPP

#include <stdexcept>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <functional>

#include <boost/variant.hpp>

#include <tconcurrent/thread_pool.hpp>
#include <tconcurrent/cancelation_token.hpp>

namespace tconcurrent
{

struct broken_promise : std::runtime_error
{
  broken_promise() : runtime_error("promise is broken")
  {}
};

struct tvoid
{
};

namespace detail
{

template <typename S>
class promise_ptr
{
public:
  promise_ptr() = default;

  template <typename P>
  promise_ptr(P&& p) : _ptr(std::forward<P>(p))
  {
    _ptr->increment_promise();
  }

  promise_ptr(promise_ptr const& r) : _ptr(r._ptr)
  {
    _ptr->increment_promise();
  }
  promise_ptr& operator=(promise_ptr const& r)
  {
    if (_ptr != r._ptr)
    {
      finish();
      _ptr = r._ptr;
      _ptr->increment_promise();
    }
    return *this;
  }

  promise_ptr(promise_ptr&& r) : _ptr(std::move(r._ptr))
  {
  }
  promise_ptr& operator=(promise_ptr&& r)
  {
    if (_ptr != r._ptr)
    {
      finish();
      _ptr = std::move(r._ptr);
    }
    return *this;
  }

  ~promise_ptr()
  {
    finish();
  }

  decltype(auto) operator->() const
  {
    return _ptr.operator->();
  }
  decltype(auto) operator*() const
  {
    return _ptr.operator*();
  }

  operator std::shared_ptr<S> const&() const
  {
    return _ptr;
  }

private:
  std::shared_ptr<S> _ptr;

  void finish()
  {
    if (_ptr)
      _ptr->decrement_promise();
  }
};

template <typename R>
class shared_base
{
public:
  struct v_none {};
  struct v_value { R value; };
  struct v_exception { std::exception_ptr exc; };

  boost::variant<v_none, v_value, v_exception> _r;

  shared_base(
      cancelation_token_ptr token = std::make_shared<cancelation_token>())
    : _cancelation_token(std::move(token))
  {
    assert(_cancelation_token);
  }

  virtual ~shared_base()
  {
    assert(_promise_count.load() == 0);
    assert(_r.which() != 0);
  }

  void set(R const& r)
  {
    finish([&] { _r = v_value{r}; });
  }
  void set(R&& r)
  {
    finish([&] { _r = v_value{std::move(r)}; });
  }

  void set_exception(std::exception_ptr exc)
  {
    finish([&] { _r = v_exception{exc}; });
  }

  template <typename E, typename F>
  void then(E&& e, F&& f)
  {
    bool resolved{false};

    {
      std::lock_guard<std::mutex> lock{_mutex};
      if (_r.which() == 0)
        _then.emplace_back(
            [&e, f = std::forward<F>(f)]() mutable { e.post(std::move(f)); });
      else
        resolved = true;
    }

    if (resolved)
      e.post(std::move(f));
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

  std::shared_ptr<cancelation_token> get_cancelation_token()
  {
    return _cancelation_token;
  }

private:
  mutable std::mutex _mutex;
  mutable std::condition_variable _ready;
  std::vector<std::function<void()>> _then;

  cancelation_token_ptr _cancelation_token;

  /** Counts the number of promises (or anything that can set the shared state)
   *
   * This count is used to set an error state when all promises are destroyed.
   */
  std::atomic<unsigned int> _promise_count{0};

  void increment_promise()
  {
    ++_promise_count;
  }

  void decrement_promise()
  {
    assert(_promise_count.load() > 0);
    if (--_promise_count == 0 && _r.which() == 0)
      set_exception(std::make_exception_ptr(broken_promise{}));
  }

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
      f();
  }

  template <typename S>
  friend class promise_ptr;
};

template <typename T>
struct shared_base_type_
{
  using type = T;
};
template <>
struct shared_base_type_<void>
{
  using type = tvoid;
};

template <typename T>
using shared_base_type = typename shared_base_type_<T>::type;

}
}

#endif
