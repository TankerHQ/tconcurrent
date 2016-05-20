#ifndef TCONCURRENT_FUTURE_HPP
#define TCONCURRENT_FUTURE_HPP

#include <utility>
#include <tconcurrent/detail/util.hpp>
#include <tconcurrent/detail/shared_base.hpp>

namespace tconcurrent
{

template <typename R>
class future;

template <typename R>
class promise;

template <typename>
class packaged_task;

template <typename S, typename F>
auto package(F&& f);
template <typename S, typename F>
auto package(F&& f, cancelation_token_ptr token)
    -> std::pair<packaged_task<S>, future<detail::result_of_t_<S>>>;

namespace detail
{

template <typename T>
struct future_value_type
{
  using type = T;
};
template <>
struct future_value_type<void>
{
  using type = tvoid;
};

}

template <typename T>
auto make_ready_future(T&& val) -> future<typename std::decay<T>::type>;
auto make_ready_future() -> future<void>;
template <typename T, typename E>
auto make_exceptional_future(E&& err) -> future<T>;

namespace detail
{

template <typename R>
struct future_unwrap
{
};

template <typename R>
struct future_unwrap<future<R>>
{
  future<R> unwrap();
};

}

template <typename R>
class future : public detail::future_unwrap<R>
{
public:
  using this_type = future<R>;
  using value_type = typename detail::future_value_type<R>::type;

  future() = default;

  template <typename F>
  auto then(F&& f)
  {
    return then(get_default_executor(), std::forward<F>(f));
  }

  template <typename E, typename F>
  auto then(E&& e, F&& f) -> future<
      typename std::decay<decltype(f(std::declval<future<R>>()))>::type>
  {
    return then_impl(std::forward<E>(e), [
      p = _p,
      token = _cancelation_token,
      f = std::forward<F>(f)
    ]() mutable {
      future fut(p);
      fut._cancelation_token = token;
      return f(std::move(fut));
    });
  }

  template <typename E, typename F>
  auto then(E&& e, F&& f) -> future<typename std::decay<decltype(
      f(std::declval<cancelation_token&>(), std::declval<future<R>>()))>::type>
  {
    return then_impl(std::forward<E>(e), [
      p = _p,
      token = _cancelation_token,
      f = std::forward<F>(f)
    ]() mutable {
      future fut(p);
      fut._cancelation_token = token;
      return f(*token, std::move(fut));
    });
  }

  template <typename F>
  auto and_then(F&& f)
  {
    return and_then(get_default_executor(), std::forward<F>(f));
  }

  template <typename E, typename F>
  auto and_then(E&& e, F&& f) -> future<
      typename std::decay<decltype(f(std::declval<value_type>()))>::type>
  {
    return then_impl(std::forward<E>(e), [
      p = _p,
      token = _cancelation_token,
      f = std::forward<F>(f)
    ]() {
      return this_type::do_and_then_callback(
          *p, token.get(), [&] { return f(p->get()); });
    });
  }
  template <typename E, typename F>
  auto and_then(E&& e, F&& f) -> future<typename std::decay<decltype(
      f(std::declval<cancelation_token&>(), std::declval<value_type>()))>::type>
  {
    return then_impl(std::forward<E>(e), [
      p = _p,
      token = _cancelation_token,
      f = std::forward<F>(f)
    ]() {
      return this_type::do_and_then_callback(
          *p, token.get(), [&] { return f(*token, p->get()); });
    });
  }

  void request_cancel()
  {
    auto const& token = _p->get_cancelation_token();
    if (token)
      token->request_cancel();
  }

  value_type const& get() const
  {
    return _p->get();
  }

  std::exception_ptr const& get_exception() const
  {
    return _p->get_exception();
  }

  void wait() const
  {
    _p->wait();
  }

  bool is_ready() const noexcept
  {
    return _p && _p->_r.which() != 0;
  }
  bool has_value() const noexcept
  {
    return _p && _p->_r.which() == 1;
  }
  bool has_exception() const noexcept
  {
    return _p && _p->_r.which() == 2;
  }
  bool is_valid() const noexcept
  {
    return bool(_p);
  }

private:
  using shared_type = detail::shared_base<value_type>;
  using shared_pointer = std::shared_ptr<shared_type>;

  shared_pointer _p;
  // the cancelation_token in _p will be lost when the promise is set, keep a
  // copy here so that continuations can still use it
  cancelation_token_ptr _cancelation_token;

  template <typename T>
  friend struct detail::future_unwrap;
  template <typename S, typename F>
  friend auto package(F&& f, cancelation_token_ptr token)
      -> std::pair<packaged_task<S>, future<detail::result_of_t_<S>>>;
  template <typename T>
  friend class promise;
  template <typename T>
  friend auto make_ready_future(T&& f) -> future<typename std::decay<T>::type>;
  friend auto make_ready_future() -> future<void>;
  template <typename T, typename E>
  friend auto make_exceptional_future(E&& err) -> future<T>;

  explicit future(std::shared_ptr<detail::shared_base<value_type>> p)
    : _p(std::move(p)),
      _cancelation_token(_p->get_cancelation_token())
  {
  }

  template <typename E, typename F>
  auto then_impl(E&& e, F&& f) -> future<typename std::decay<decltype(f())>::type>
  {
    using result_type = typename std::decay<decltype(f())>::type;

    auto pack = package<result_type()>(std::forward<F>(f), _cancelation_token);
    _p->then(std::forward<E>(e), std::move(pack.first));
    return pack.second;
  }

  template <typename F>
  static auto do_and_then_callback(shared_type& p,
                                   cancelation_token* token,
                                   F&& cb)
  {
    assert(p._r.which() != 0);
    if (p._r.which() == 1)
    {
      if (token && token->is_cancel_requested())
        throw operation_canceled();
      else
        return cb();
    }
    else
    {
      assert(p._r.which() == 2);
      p.get(); // rethrow to set the future to error
      assert(false && "unreachable code");
      std::terminate();
    }
  }
};

template <typename R>
future<R> detail::future_unwrap<future<R>>::unwrap()
{
  auto& fut = static_cast<future<future<R>>&>(*this);
  auto sb = std::make_shared<typename future<R>::shared_type>(
      fut._p->get_cancelation_token());
  fut.then(get_synchronous_executor(),
           [sb](future<future<R>> const& fut) {
             if (fut.has_exception())
               sb->set_exception(fut.get_exception());
             else
             {
               auto nested = fut.get();
               if (sb->get_cancelation_token() !=
                   nested._p->get_cancelation_token())
                 sb->get_cancelation_token()->push_last_cancelation_callback(
                     [nested]() mutable { nested.request_cancel(); });
               nested.then(get_synchronous_executor(),
                           [sb](future<R> const& nested) {
                             if (nested.has_exception())
                               sb->set_exception(nested.get_exception());
                             else
                               sb->set(
// Lol, msvc does not want a typename here
#ifndef _WIN32
                                   typename
#endif
                                   future<R>::value_type(nested.get()));
                           });
             }
           });
  return future<R>(sb);
}

template <typename T>
auto make_ready_future(T&& val) -> future<typename std::decay<T>::type>
{
  using result_type = typename std::decay<T>::type;
  using shared_base_type = detail::shared_base<result_type>;

  auto sb = std::make_shared<shared_base_type>(detail::nocancel_tag{});
  sb->_r = typename shared_base_type::v_value{std::forward<T>(val)};
  future<result_type> fut(std::move(sb));
  fut._cancelation_token = std::make_shared<cancelation_token>();
  return fut;
}

inline auto make_ready_future() -> future<void>
{
  using shared_base_type = future<void>::shared_type;

  auto sb = std::make_shared<shared_base_type>(detail::nocancel_tag{});
  sb->_r = shared_base_type::v_value{};
  future<void> fut(std::move(sb));
  fut._cancelation_token = std::make_shared<cancelation_token>();
  return fut;
}

template <typename T, typename E>
auto make_exceptional_future(E&& err) -> future<T>
{
  using result_type = typename future<T>::value_type;
  using shared_base_type = detail::shared_base<result_type>;

  auto sb = std::make_shared<shared_base_type>(detail::nocancel_tag{});
  sb->_r = typename shared_base_type::v_exception{
      std::make_exception_ptr(std::forward<E>(err))};
  future<T> fut(std::move(sb));
  fut._cancelation_token = std::make_shared<cancelation_token>();
  return fut;
}

}

#include <tconcurrent/packaged_task.hpp>

#endif
