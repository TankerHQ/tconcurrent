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
  /** Unwrap a nested future
   *
   * Transform a future<future<T>> to a future<T>.
   *
   * This is equivalent to the monadic join.
   */
  future<R> unwrap();
};

template <template <typename> class F, typename R>
class future_base
{
public:
  using this_type = F<R>;
  using value_type = typename detail::future_value_type<R>::type;

  /** Prevent cancelation requests to propagate from this future
   *
   * This means that cancelation requests that may be triggered on this future
   * before this call, or on previous futures, will not be propagated to then
   * and and_then callbacks.
   *
   * This function discards the cancelation token of the previous task and
   * creates a new one, effectively preventing cancelation requests from going
   * from this future to the previous task and from the previous future to the
   * following tasks.
   */
  this_type break_cancelation_chain() &&
  {
    _cancelation_token = _p->reset_cancelation_token();
    return std::move(*this_());
  }

  /** Request a cancelation of the future
   *
   * This does not guarantee that the future will be canceled. After this call,
   * the future may still not be ready, it may finish with a value or with an
   * error.
   *
   * If the cancelation request succeeds, the future will finish with an error
   * of type operation_canceled.
   *
   * Requesting a cancelation on a future that is already ready has no effect.
   */
  void request_cancel()
  {
    auto const& token = _p->get_cancelation_token();
    if (token)
      token->request_cancel();
  }

  /** Get a callable that will cancel this future
   */
  auto make_canceler()
  {
    return [_p = this->_p] {
      auto const& token = _p->get_cancelation_token();
      if (token)
        token->request_cancel();
    };
  }

  /// Wait indefinitely for the future to get ready
  void wait() const
  {
    _p->wait();
  }

  /// Wait for the future to get ready and timeout after \p dur
  template <typename Rep, typename Period>
  void wait_for(std::chrono::duration<Rep, Period> const& dur) const
  {
    _p->wait_for(dur);
  }

  /// Return true if the future has a result value or an exception
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
  /// Return false if the future has been default constructed or moved-from
  bool is_valid() const noexcept
  {
    return bool(_p);
  }

  /** Get the contained exception
   *
   * If the future is not ready, this call will block. If the future does not
   * contain an exception, this call will throw.
   */
  std::exception_ptr const& get_exception() const
  {
    return _p->get_exception();
  }

  std::string const& get_chain_name() const
  {
    return _chain_name;
  }

  /** Return a new future with a different name that will be propagated to then
   * and and_then
   */
  this_type update_chain_name(std::string name) &&
  {
    this_type ret{std::move(*this_())};
    ret._chain_name = std::move(name);
    return ret;
  }

protected:
  using shared_type = detail::shared_base<value_type>;
  using shared_pointer = std::shared_ptr<shared_type>;

  shared_pointer _p;
  // the cancelation_token in _p will be lost when the promise is set, keep a
  // copy here so that continuations can still use it
  cancelation_token_ptr _cancelation_token;

  std::string _chain_name;

  future_base() = default;
  future_base(future_base const&) = default;
  future_base& operator=(future_base const&) = default;
  future_base(future_base&&) = default;
  future_base& operator=(future_base&&) = default;

  future_base(shared_pointer p)
    : _p(std::move(p)), _cancelation_token(_p->get_cancelation_token())
  {
  }

  this_type* this_()
  {
    return static_cast<this_type*>(this);
  }
};

}

template <typename R>
class future;

namespace detail
{
// see above
template <typename R>
using base_for_future = detail::future_base<future, R>;
}

template <typename R>
class future : public detail::base_for_future<R>,
               public detail::future_unwrap<R>
{
public:
  using base_type = detail::base_for_future<R>;
  using typename base_type::this_type;
  using typename base_type::value_type;

  future(future const&) = delete;
  future& operator=(future const&) = delete;
  future(future&&) = default;
  future& operator=(future&&) = default;

  /// Construct a future in an invalid state
  future() = default;

  /// Same as then(E&& e, F&& f) with the default executor as `e`
  template <typename F>
  auto then(F&& f)
  {
    return then(get_default_executor(), std::forward<F>(f));
  }

  /** Register a callback to run when the future gets ready
   *
   * \param e an executor to run the callback on
   * \param f a function to call when the future completes. Its signature must
   * be one of:
   *
   *     U func(future<T> const&);
   *     U func(cancelation_token&, future<T> const&);
   *
   * It will receive *this as an argument and optionally the cancelation_token.
   *
   * \return a future<U> containing the result of the callback
   */
  // TODO replace this result_of and the others by decltype the day microsoft
  // makes a real compiler
  template <typename E, typename F>
  auto then(E&& e, F&& f)
      -> future<std::decay_t<std::result_of_t<F(future<R>)>>>
  {
    return then_impl(std::forward<E>(e), [
      p = this->_p,
      token = this->_cancelation_token,
      f = std::forward<F>(f)
    ]() mutable {
      future fut(p);
      fut._cancelation_token = token;
      return f(std::move(fut));
    });
  }

  template <typename E, typename F>
  auto then(E&& e, F&& f) -> future<
      std::decay_t<std::result_of_t<F(cancelation_token&, future<R>)>>>
  {
    return then_impl(std::forward<E>(e), [
      p = this->_p,
      token = this->_cancelation_token,
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

  /** Register a callback to run when the future gets ready with a result value
   *
   * This function is similar to then(E&& e, F&& f) but the callback will be
   * called only if the future finishes with a value. If the future finishes
   * with an exception, the callback will not be called and the resulting future
   * will get the same exception.
   *
   * This function represents the Haskell Functor.fmap function.
   *
   * \param e an executor to run the callback on
   * \param f a function to call when the future completes with a value. Its
   * signature must be one of:
   *
   *     U func(T const&);
   *     U func(cancelation_token&, T const&);
   *
   * It will receive `this->get()` as an argument and optionally the
   * cancelation_token.
   *
   * \return a future<U> containing the result of the callback
   */
  template <typename E, typename F>
  auto and_then(E&& e, F&& f)
      -> future<std::decay_t<std::result_of_t<F(value_type)>>>
  {
    return then_impl(std::forward<E>(e), [
      p = this->_p,
      token = this->_cancelation_token,
      f = std::forward<F>(f)
    ]() mutable {
      return this_type::do_and_then_callback(
          *p, token.get(), [&] { return f(p->template get<value_type>()); });
    });
  }
  template <typename E, typename F>
  auto and_then(E&& e, F&& f) -> future<
      std::decay_t<std::result_of_t<F(cancelation_token&, value_type)>>>
  {
    return then_impl(std::forward<E>(e), [
      p = this->_p,
      token = this->_cancelation_token,
      f = std::forward<F>(f)
    ]() mutable {
      return this_type::do_and_then_callback(*p, token.get(), [&] {
        return f(*token, p->template get<value_type>());
      });
    });
  }

  /** Get the contained result value
   *
   * If the future is not ready, this call will block. If the future contains an
   * exception, it will be rethrown here.
   */
  value_type get()
  {
    return this->_p->template get<value_type>();
  }

  /// Get a future equivalent to this one but discarding the result value
  tc::future<void> to_void()
  {
    return and_then(get_synchronous_executor(), [](value_type const&){});
  }

private:
  using typename base_type::shared_type;
  using typename base_type::shared_pointer;

  template <typename T>
  friend class future;
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
    : base_type(std::move(p))
  {
  }

  template <typename E, typename F>
  auto then_impl(E&& e, F&& f)
      -> future<typename std::decay<decltype(f())>::type>
  {
    using result_type = typename std::decay<decltype(f())>::type;

    auto pack =
        package<result_type()>(std::forward<F>(f), this->_cancelation_token);
    this->_p->then(
        this->_chain_name + " (" + typeid(F).name() + ")",
        std::forward<E>(e),
        std::move(pack.first));
    pack.second._chain_name = this->_chain_name;
    return std::move(pack.second);
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
      p.template get<value_type const&>(); // rethrow to set the future to error
      assert(false && "unreachable code");
      std::terminate();
    }
  }
};

template <typename R>
future<R> detail::future_unwrap<future<R>>::unwrap()
{
  auto& fut = static_cast<future<future<R>>&>(*this);
  auto sb =
      std::make_shared<typename future<R>::shared_type>(fut._cancelation_token);
  fut.then(get_synchronous_executor(),
           [sb](future<future<R>> fut) {
             if (fut.has_exception())
               sb->set_exception(fut.get_exception());
             else
             {
               auto nested = fut.get();
               if (sb->get_cancelation_token() != nested._cancelation_token)
                 sb->get_cancelation_token()->push_last_cancelation_callback(
                     nested.make_canceler());
               nested.then(get_synchronous_executor(),
                           [sb](future<R> nested) {
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

/// Create a future in a ready state with value \p val
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

/// Create a void future in a ready state
inline auto make_ready_future() -> future<void>
{
  using shared_base_type = future<void>::shared_type;

  auto sb = std::make_shared<shared_base_type>(detail::nocancel_tag{});
  sb->_r = shared_base_type::v_value{};
  future<void> fut(std::move(sb));
  fut._cancelation_token = std::make_shared<cancelation_token>();
  return fut;
}

/// Create a future in an exceptional state with the exception err
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
