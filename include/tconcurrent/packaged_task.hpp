#ifndef PACKAGED_TASK_HPP
#define PACKAGED_TASK_HPP

#include <memory>
#include <tconcurrent/future.hpp>

namespace tconcurrent
{

namespace detail
{

struct packaged_task_result_type_2
{
  template <typename F, typename... Args>
  static decltype(std::declval<F>()(std::declval<Args>()...)) f(long);
  template <typename F, typename... Args>
  static decltype(std::declval<F>()(std::declval<cancelation_token&>(),
                                    std::declval<Args>()...))
  f(int);
};

template <typename Call>
struct packaged_task_result_type_; // not defined

template <typename F, typename... Args>
struct packaged_task_result_type_<F(Args...)>
{
  using type = decltype(packaged_task_result_type_2::f<F, Args...>(0));
};
}

template <typename Call>
using packaged_task_result_type =
    typename detail::packaged_task_result_type_<Call>::type;

namespace detail
{

template <typename...>
using void_t = void;

template <typename R>
struct package_caller
{
  template <typename F, typename... Args>
  static void do_call(shared_base<R>& s, F&& func, Args&&... args)
  {
    s.set(func(std::forward<Args>(args)...));
  }
};

template <>
struct package_caller<void>
{
  template <typename F, typename... Args>
  static void do_call(shared_base<tvoid>& s, F&& func, Args&&... args)
  {
    func(std::forward<Args>(args)...);
    s.set({});
  }
};

template <typename>
struct shared; // not defined

template <typename R, typename... Args>
struct shared<R(Args...)> : shared_base<void_to_tvoid_t<R>>
{
  using base_type = shared_base<void_to_tvoid_t<R>>;

  std::atomic<bool> _done{false};
  fu2::unique_function<R(cancelation_token&, Args...)> _f;
  bool _cancelable;

  template <typename F>
  shared(
      bool cancelable,
      cancelation_token_ptr token,
      F&& f,
      void_t<decltype(std::declval<F>()(std::declval<Args>()...))>* = nullptr)
    : base_type(std::move(token))
    , _cancelable(cancelable)
    , _f([f = std::forward<F>(f)](cancelation_token&, auto&&... args) mutable {
      return f(std::forward<decltype(args)>(args)...);
    })
  {
  }

  template <typename F>
  shared(
      bool cancelable,
      cancelation_token_ptr token,
      F&& f,
      void_t<decltype(std::declval<F>()(std::declval<cancelation_token&>(),
                                        std::declval<Args>()...))>** = nullptr)
    : base_type(std::move(token))
    , _cancelable(cancelable)
    , _f(std::forward<F>(f))
  {
    assert(_f);
  }

  template <typename... A>
  void operator()(A&&... args)
  {
    if (_done.exchange(true))
      return;
    try
    {
      if (_cancelable)
        this->_cancelation_token->pop_cancelation_callback();
      package_caller<R>::do_call(
          *this, _f, *this->_cancelation_token, std::forward<A>(args)...);
    }
    catch (...)
    {
      this->set_exception(std::current_exception());
    }
    _f = nullptr;
  }
};

template <typename S>
class packaged_task_canceler
{
public:
  packaged_task_canceler(std::shared_ptr<shared<S>> p,
                         cancelation_token* cancelation_token)
    : _p(std::move(p)), _token(cancelation_token)
  {
  }

  void operator()()
  {
    // If we got the _done lock just below, the callback may die
    // asynchronously, setting the promise state to broken_promise. That's why
    // we lock the promise_ptr before that line.
    auto const pp = promise_ptr<detail::shared<S>>::try_lock(std::move(_p));
    if (!pp)
    {
      // the promise is already dead, this means the callback has run
      assert(_p->_done.load());
      return;
    }
    if (pp->_done.exchange(true))
      return;

    _token->pop_cancelation_callback();
    pp->_f = nullptr;
    pp->set_exception(std::make_exception_ptr(operation_canceled{}));
  }

private:
  std::shared_ptr<shared<S>> _p;
  cancelation_token* _token;
};

template <typename S, typename F>
auto package(F&& f, cancelation_token_ptr token, bool cancelable)
    -> std::pair<packaged_task<S>, future<detail::result_of_t_<S>>>
{
  auto const p = promise_ptr<detail::shared<S>>::make_shared(
      cancelable, token, std::forward<F>(f));
  if (cancelable)
    token->push_cancelation_callback(
        packaged_task_canceler<S>{p.as_shared(), token.get()});
  return std::make_pair(packaged_task<S>(p),
                        future<detail::result_of_t_<S>>(p.as_shared()));
}
}

template <typename R, typename... Args>
class packaged_task<R(Args...)>
{
public:
  template <typename... A>
  void operator()(A&&... args) const
  {
    (*_p)(std::forward<A>(args)...);
  }

private:
  using shared_type = detail::shared<R(Args...)>;

  detail::promise_ptr<shared_type> _p;

  template <typename S, typename F>
  friend auto detail::package(F&& f,
                              cancelation_token_ptr token,
                              bool cancelable)
      -> std::pair<packaged_task<S>, future<detail::result_of_t_<S>>>;

  explicit packaged_task(detail::promise_ptr<shared_type> p) : _p(std::move(p))
  {
  }
};

template <typename S, typename F>
auto package(F&& f, cancelation_token_ptr token)
    -> std::pair<packaged_task<S>, future<detail::result_of_t_<S>>>
{
  return detail::package<S>(std::forward<F>(f), token, false);
}

template <typename S, typename F>
auto package(F&& f)
{
  return detail::package<S>(
      std::forward<F>(f), std::make_shared<cancelation_token>(), false);
}

template <typename S, typename F>
auto package_cancelable(F&& f)
{
  return detail::package<S>(
      std::forward<F>(f), std::make_shared<cancelation_token>(), true);
}

template <typename S, typename F>
auto package_cancelable(F&& f, cancelation_token_ptr token)
{
  return detail::package<S>(std::forward<F>(f), token, true);
}
}

#endif
