#ifndef TCONCURRENT_FUTURE_HPP
#define TCONCURRENT_FUTURE_HPP

#include <tconcurrent/detail/shared_base.hpp>
#include <tconcurrent/detail/util.hpp>
#include <tconcurrent/executor.hpp>
#include <tconcurrent/lazy/cancelation_token.hpp>

#include <utility>

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

template <typename S, typename F>
auto package(F&& f, cancelation_token_ptr token, bool cancelable)
    -> std::pair<packaged_task<S>, future<detail::result_of_t_<S>>>;
}

template <typename T>
auto make_ready_future(T&& val) -> future<typename std::decay<T>::type>;
auto make_ready_future() -> future<void>;
template <typename T, typename E>
auto make_exceptional_future(E&& err) -> future<T>;

template <typename T, typename Sender>
future<T> submit_to_future(Sender&& task);

namespace detail
{

template <typename R>
struct future_unwrap
{
};

template <template <typename> class Fut1,
          template <typename>
          class Fut2,
          typename R>
struct future_unwrap<Fut1<Fut2<R>>>
{
  /** Unwrap a nested future
   *
   * Transform a future<future<T>> to a future<T>.
   *
   * This is equivalent to the monadic join.
   */
  Fut2<R> unwrap();
};

template <template <typename> class F, typename R, bool Ref>
class future_base : public detail::future_unwrap<F<R>>
{
public:
  using this_type = F<R>;
  using result_type = R;
  using value_type = detail::void_to_tvoid_t<R>;
  using get_type = std::conditional_t<Ref, value_type const&, value_type>;

  /// Same as then(E&& e, Func&& func) with the default executor as `e`
  template <typename Func>
  auto then(Func&& func)
  {
    return then(get_default_executor(), std::forward<Func>(func));
  }

  /** Register a callback to run when the future gets ready
   *
   * \param e an executor to run the callback on
   * \param func a function to call when the future completes. Its signature
   * must be one of:
   *
   *     U func(future<T> const&);
   *     U func(cancelation_token&, future<T> const&);
   *
   * It will receive *this as an argument and optionally the cancelation_token.
   *
   * \return a future<U> containing the result of the callback
   */
  template <typename E, typename Func>
  auto then(E&& e, Func&& func) -> future<
      std::decay_t<decltype(std::declval<Func&&>()(std::declval<this_type>()))>>
  {
    return then_impl(std::forward<E>(e),
                     [p = _p,
                      token = _cancelation_token,
                      chain_name = _chain_name,
                      func = std::forward<Func>(func)]() mutable {
                       this_type fut(p);
                       fut._cancelation_token = token;
                       fut._chain_name = chain_name;
                       return func(std::move(fut));
                     });
  }

  template <typename E, typename Func>
  auto then(E&& e, Func&& func)
      -> future<std::decay_t<decltype(std::declval<Func&&>()(
          std::declval<cancelation_token&>(), std::declval<this_type>()))>>
  {
    return then_impl(std::forward<E>(e),
                     [p = _p,
                      token = _cancelation_token,
                      chain_name = _chain_name,
                      func = std::forward<Func>(func)]() mutable {
                       this_type fut(p);
                       fut._cancelation_token = token;
                       fut._chain_name = chain_name;
                       return func(*token, std::move(fut));
                     });
  }

  template <typename Func>
  auto and_then(Func&& func)
  {
    return and_then(get_default_executor(), std::forward<Func>(func));
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
  template <typename E, typename Func>
  auto and_then(E&& e, Func&& func) -> future<
      std::decay_t<decltype(std::declval<Func&&>()(std::declval<get_type>()))>>
  {
    return then_impl(std::forward<E>(e),
                     [p = _p,
                      token = _cancelation_token,
                      func = std::forward<Func>(func)]() mutable {
                       return do_and_then_callback(*p, token.get(), [&] {
                         return func(p->template get<get_type>());
                       });
                     });
  }
  template <typename E, typename Func>
  auto and_then(E&& e, Func&& func)
      -> future<std::decay_t<decltype(std::declval<Func&&>()(
          std::declval<cancelation_token&>(), std::declval<get_type>()))>>

  {
    return then_impl(std::forward<E>(e),
                     [p = _p,
                      token = _cancelation_token,
                      func = std::forward<Func>(func)]() mutable {
                       return do_and_then_callback(*p, token.get(), [&] {
                         return func(*token, p->template get<get_type>());
                       });
                     });
  }

  /// Get a future equivalent to this one but discarding the result value
  tc::future<void> to_void();

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
    auto const token = _p->get_cancelation_token();
    if (token)
      token->request_cancel();
  }

  /** Get a callable that will cancel this future
   */
  auto make_canceler()
  {
    return [_p = this->_p] {
      auto const token = _p->get_cancelation_token();
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
    return _p && _p->_r.index() != 0;
  }
  bool has_value() const noexcept
  {
    return _p && _p->_r.index() == 1;
  }
  bool has_exception() const noexcept
  {
    return _p && _p->_r.index() == 2;
  }
  /// Return false if the future has been default constructed or moved-from
  bool is_valid() const noexcept
  {
    return bool(_p);
  }

  /** Get the contained result value
   *
   * If the future is not ready, this call will block. If the future contains an
   * exception, it will be rethrown here.
   */
  get_type get()
  {
    return _p->template get<get_type>();
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

  /** Update the name that will be propagated to then and and_then
   */
  this_type& update_chain_name(std::string name) &
  {
    _chain_name = std::move(name);
    return *this;
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
  ~future_base() = default;

  future_base(shared_pointer p)
    : _p(std::move(p)), _cancelation_token(_p->get_cancelation_token())
  {
  }

  future_base(shared_pointer p,
              cancelation_token_ptr cancelation_token,
              std::string chain_name)
    : _p(std::move(p))
    , _cancelation_token(std::move(cancelation_token))
    , _chain_name(std::move(chain_name))
  {
  }

private:
  this_type* this_()
  {
    return static_cast<this_type*>(this);
  }

  template <typename E, typename Func>
  auto then_impl(E&& e, Func&& func)
      -> future<typename std::decay<decltype(func())>::type>
  {
    using result_type = typename std::decay<decltype(func())>::type;

    auto pack =
        package<result_type()>(std::forward<Func>(func), _cancelation_token);
    _p->then(_chain_name + " (" + typeid(Func).name() + ")",
             std::forward<E>(e),
             std::move(pack.first));
    pack.second._chain_name = _chain_name;
    return std::move(pack.second);
  }

  template <typename Func>
  static auto do_and_then_callback(shared_type& p,
                                   cancelation_token* token,
                                   Func&& cb)
  {
    assert(p._r.index() != 0);
    if (p._r.index() == 1)
    {
      if (token && token->is_cancel_requested())
        throw operation_canceled();
      else
        return cb();
    }
    else
    {
      assert(p._r.index() == 2);
      p.template get<value_type const&>(); // rethrow to set the future to error
      assert(false && "unreachable code");
      std::terminate();
    }
  }
};
}

template <typename R>
class shared_future;

namespace detail
{
// workaround for VS2015, we can't use CRTP with a class template inside the
// class
template <typename R>
using base_for_shared_future = detail::future_base<shared_future, R, true>;
}

template <typename R>
class shared_future : public detail::base_for_shared_future<R>
{
public:
  using base_type = detail::base_for_shared_future<R>;
  using typename base_type::this_type;
  using typename base_type::value_type;

  shared_future(shared_future const&) = default;
  shared_future& operator=(shared_future const&) = default;
  shared_future(shared_future&&) = default;
  shared_future& operator=(shared_future&&) = default;

  /// Construct a shared_future in an invalid state
  shared_future() = default;

  /// Convert a future to a shared_future
  shared_future(future<R>&& fut);

private:
  using typename base_type::shared_pointer;
  using typename base_type::shared_type;

  template <template <typename> class, typename, bool>
  friend class detail::future_base;
  template <typename T>
  friend struct detail::future_unwrap;

  explicit shared_future(std::shared_ptr<detail::shared_base<value_type>> p)
    : base_type(std::move(p))
  {
  }
};

template <typename R>
class future;

namespace detail
{
// see above
template <typename R>
using base_for_future = detail::future_base<future, R, false>;
}

template <typename R>
class future : public detail::base_for_future<R>
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

  shared_future<R> to_shared()
  {
    return shared_future<R>(std::move(*this));
  }

private:
  using typename base_type::shared_pointer;
  using typename base_type::shared_type;

  template <template <typename> class, typename, bool>
  friend class detail::future_base;
  template <typename T>
  friend class future;
  friend class shared_future<R>;
  template <typename T>
  friend struct detail::future_unwrap;
  template <typename S, typename F>
  friend auto detail::package(F&& f,
                              cancelation_token_ptr token,
                              bool cancelable)
      -> std::pair<packaged_task<S>, future<detail::result_of_t_<S>>>;
  template <typename T>
  friend class promise;
  template <typename T>
  friend auto make_ready_future(T&& f) -> future<typename std::decay<T>::type>;
  friend auto make_ready_future() -> future<void>;
  template <typename T, typename E>
  friend auto make_exceptional_future(E&& err) -> future<T>;
  template <typename T, typename Sender>
  friend future<T> submit_to_future(Sender&& task);

  explicit future(std::shared_ptr<detail::shared_base<value_type>> p)
    : base_type(std::move(p))
  {
  }
};

template <typename R>
shared_future<R>::shared_future(future<R>&& fut)
  : base_type(std::move(fut._p),
              std::move(fut._cancelation_token),
              std::move(fut._chain_name))
{
}

template <template <typename> class F, typename R, bool Ref>
tc::future<void> detail::future_base<F, R, Ref>::to_void()
{
  return and_then(get_synchronous_executor(), [](value_type const&) {});
}

template <template <typename> class Fut1,
          template <typename>
          class Fut2,
          typename R>
Fut2<R> detail::future_unwrap<Fut1<Fut2<R>>>::unwrap()
{
  auto& fut1 = static_cast<Fut1<Fut2<R>>&>(*this);
  auto sb = std::make_shared<typename future<R>::shared_type>(
      fut1._cancelation_token);
  fut1.then(get_synchronous_executor(), [sb](Fut1<Fut2<R>> fut1) {
    if (fut1.has_exception())
      sb->set_exception(fut1.get_exception());
    else
    {
      auto fut2 = fut1.get();
      cancelation_token_ptr token;
      if (sb->get_cancelation_token() != fut2._cancelation_token)
      {
        token = sb->get_cancelation_token();
        token->push_cancelation_callback(fut2.make_canceler());
      }
      fut2.then(get_synchronous_executor(), [sb, token](Fut2<R> fut2) {
        if (token)
          token->pop_cancelation_callback();
        if (fut2.has_exception())
          sb->set_exception(fut2.get_exception());
        else
          sb->set(fut2.get());
      });
    }
  });

  auto ret = Fut2<R>(sb);
  ret._chain_name = fut1._chain_name;
  return std::move(ret);
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

template <class T>
struct shared_receiver_priv : public detail::shared_base<T>
{
  lazy::cancelation_token lazy_cancelation_token;
  cancelation_token::scope_canceler canceler;

  shared_receiver_priv()
  {
    canceler = this->get_cancelation_token()->make_scope_canceler(
        [this] { lazy_cancelation_token.request_cancel(); });
  }
};

template <class T>
struct shared_receiver
{
  detail::promise_ptr<shared_receiver_priv<T>> shared =
      detail::promise_ptr<shared_receiver_priv<T>>::make_shared();

  auto get_cancelation_token()
  {
    return &shared->lazy_cancelation_token;
  }
  void set_value()
  {
    get_cancelation_token()->reset();
    shared->set(tvoid{});
  }
  template <typename V>
  void set_value(V&& val)
  {
    get_cancelation_token()->reset();
    shared->set(std::forward<V>(val));
  }
  template <typename E>
  void set_error(E&& e)
  {
    get_cancelation_token()->reset();
    shared->set_exception(std::forward<E>(e));
  }
  void set_done()
  {
    get_cancelation_token()->reset();
    shared->set_exception(std::make_exception_ptr(operation_canceled()));
  }
};

template <typename T, typename Sender>
future<T> submit_to_future(Sender&& sender)
{
  shared_receiver<detail::void_to_tvoid_t<T>> receiver;
  // get the shared state now as we will move out receiver
  auto const shared_state = receiver.shared.as_shared();
  try
  {
    sender.submit(std::move(receiver));
  }
  catch (...)
  {
    shared_state->lazy_cancelation_token.reset();
    shared_state->set_exception(std::current_exception());
  }
  return future<T>(shared_state);
}
}

#include <tconcurrent/packaged_task.hpp>

#endif
