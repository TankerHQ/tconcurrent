#ifndef TCONCURRENT_DETAIL_SHARED_BASE_HPP
#define TCONCURRENT_DETAIL_SHARED_BASE_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <mpark/variant.hpp>

#include <tconcurrent/cancelation_token.hpp>

namespace tconcurrent
{

struct broken_promise : std::runtime_error
{
  broken_promise() : runtime_error("promise is broken")
  {
  }
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
  using shared_ptr_type = std::shared_ptr<S>;

  promise_ptr() = default;

  promise_ptr(promise_ptr const& r) : _ptr(r._ptr)
  {
    auto const ok = _ptr->increment_promise();
    (void)ok;
    assert(ok);
  }
  promise_ptr& operator=(promise_ptr const& r)
  {
    if (_ptr != r._ptr)
    {
      finish();
      _ptr = r._ptr;
      auto const ok = _ptr->increment_promise();
      (void)ok;
      assert(ok);
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

  shared_ptr_type as_shared() const
  {
    return _ptr;
  }

  decltype(auto) operator-> () const
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
  explicit operator bool() const
  {
    return !!_ptr;
  }

  template <typename P>
  static promise_ptr try_lock(P&& p)
  {
    auto const ok = p->increment_promise();
    if (!ok)
      return {};
    return promise_ptr(std::forward<P>(p));
  }

  template <typename... Args>
  static promise_ptr make_shared(Args&&... args)
  {
    auto p = std::make_shared<S>(std::forward<Args>(args)...);
    ++p->_promise_count;
    return promise_ptr(std::move(p));
  }

private:
  std::shared_ptr<S> _ptr;

  promise_ptr(shared_ptr_type const& p) : _ptr(p)
  {
  }

  promise_ptr(shared_ptr_type&& p) : _ptr(std::move(p))
  {
  }

  void finish()
  {
    if (_ptr)
      _ptr->decrement_promise();
  }
};

struct nocancel_tag
{
};

template <typename R>
class shared_base
{
public:
  struct v_none
  {
  };
  struct v_value
  {
    R value;
  };
  struct v_exception
  {
    std::exception_ptr exc;
  };

  mpark::variant<v_none, v_value, v_exception> _r;

  shared_base(nocancel_tag)
  {
  }

  shared_base(cancelation_token_ptr token = nullptr)
    : _cancelation_token(token ? std::move(token)
                               : std::make_shared<cancelation_token>())
  {
  }

  virtual ~shared_base()
  {
    assert(_promise_count.load() == 0);
    assert(_r.index() != 0);
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
  void then(std::string name, E&& e, F&& f)
  {
    bool resolved{false};

    {
      std::lock_guard<std::mutex> lock{_mutex};
      if (_r.index() == 0)
        _then.emplace_back([name = std::move(name),
                            e = std::forward<E>(e),
                            f = std::forward<F>(f)]() mutable {
          e.post(std::move(f), std::move(name));
        });
      else
        resolved = true;
    }

    if (resolved)
      e.post(std::forward<F>(f), std::move(name));
  }

  template <typename Rcv>
  Rcv get()
  {
    static_assert(std::is_same<R, std::decay_t<Rcv>>::value,
                  "Rcv must be a R or R const&");

    std::unique_lock<std::mutex> lock{_mutex};
    while (_r.index() == 0)
      _ready.wait(lock);
    if (_r.index() == 2)
      std::rethrow_exception(mpark::get<v_exception>(_r).exc);
    // this may or may not move depending on Rcv being a reference or not
    return std::move(mpark::get<v_value>(_r).value);
  }

  std::exception_ptr const& get_exception()
  {
    std::unique_lock<std::mutex> lock{_mutex};
    while (_r.index() == 0)
      _ready.wait(lock);
    if (_r.index() == 1)
      throw std::logic_error("this future has a value");
    return mpark::get<v_exception>(_r).exc;
  }

  void wait() const
  {
    std::unique_lock<std::mutex> lock{_mutex};
    _ready.wait(lock, [&] { return _r.index() != 0; });
  }

  template <class Rep, class Period>
  void wait_for(std::chrono::duration<Rep, Period> const& dur) const
  {
    std::unique_lock<std::mutex> lock{_mutex};
    _ready.wait_for(lock, dur, [&] { return _r.index() != 0; });
  }

  std::shared_ptr<cancelation_token> reset_cancelation_token()
  {
    std::unique_lock<std::mutex> lock{_mutex};
    _cancelation_token = std::make_shared<cancelation_token>();
    return _cancelation_token;
  }

  std::shared_ptr<cancelation_token> get_cancelation_token()
  {
    std::unique_lock<std::mutex> lock{_mutex};
    return _cancelation_token;
  }

private:
  mutable std::mutex _mutex;
  mutable std::condition_variable _ready;
  std::vector<std::function<void()>> _then;

protected:
  cancelation_token_ptr _cancelation_token;

private:
  /** Counts the number of promises (or anything that can set the shared state)
   *
   * This count is used to set an error state when all promises are destroyed.
   */
  std::atomic<unsigned int> _promise_count{0};

  bool increment_promise()
  {
    auto count = _promise_count.load();
    while (true)
    {
      if (count == 0)
        return false;
      if (_promise_count.compare_exchange_weak(count, count + 1))
        return true;
    }
  }

  void decrement_promise()
  {
    assert(_promise_count.load() > 0);
    if (--_promise_count == 0 && _r.index() == 0)
      set_exception(std::make_exception_ptr(broken_promise{}));
  }

  template <typename F>
  void finish(F&& setval)
  {
    assert(_r.index() == 0 && "state already set");

    std::vector<std::function<void()>> then;
    {
      std::lock_guard<std::mutex> lock{_mutex};
      setval();
      std::swap(_then, then);
      _cancelation_token = nullptr;
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
