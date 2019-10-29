#ifndef TCONCURRENT_LAZY_THEN_HPP
#define TCONCURRENT_LAZY_THEN_HPP

#include <tconcurrent/lazy/cancelation_token.hpp>

namespace tconcurrent
{
namespace lazy
{
namespace detail
{
template <typename R>
struct then_caller
{
  template <typename Receiver, typename F, typename... Args>
  static void do_call(Receiver&& receiver, F&& func, Args&&... args)
  {
    receiver.set_value(func(std::forward<Args>(args)...));
  }
};

template <>
struct then_caller<void>
{
  template <typename Receiver, typename F, typename... Args>
  static void do_call(Receiver&& receiver, F&& func, Args&&... args)
  {
    func(std::forward<Args>(args)...);
    receiver.set_value();
  }
};

template <typename F, typename Tuple>
struct invoke_result;

template <typename F, template <typename...> class Tuple, typename... Args>
struct invoke_result<F, Tuple<Args...>>
{
  using type = std::invoke_result_t<F, Args...>;
};

template <typename Receiver, typename F>
struct then_receiver
{
  Receiver _receiver;
  F _fun;
  auto get_cancelation_token()
  {
    return _receiver.get_cancelation_token();
  }
  template <typename... V>
  void set_value(V... vs)
  {
    if (_receiver.get_cancelation_token()->is_cancel_requested())
      return set_done();

    _receiver.get_cancelation_token()->reset();
    try
    {
      then_caller<decltype(_fun(std::forward<V>(vs)...))>::do_call(
          _receiver, _fun, std::forward<V>(vs)...);
    }
    catch (...)
    {
      _receiver.set_error(std::current_exception());
    }
  }
  template <typename E>
  void set_error(E&& e)
  {
    _receiver.get_cancelation_token()->reset();
    _receiver.set_error(std::forward<E>(e));
  }
  void set_done()
  {
    _receiver.get_cancelation_token()->reset();
    _receiver.set_done();
  }
};

template <typename Sender, typename F>
struct then_sender
{
  template <template <typename...> class Tuple>
  using value_types = Tuple<typename invoke_result<
      F,
      typename Sender::template value_types<Tuple>>::type>;

  Sender sender;
  F fun;

  template <typename R>
  void submit(R&& receiver)
  {
    sender.submit(then_receiver<std::decay_t<R>, decltype(fun)>{
        std::forward<R>(receiver), std::move(fun)});
  };
};

template <typename Receiver, typename F>
struct async_then_receiver
{
  Receiver _receiver;
  F _fun;
  auto get_cancelation_token()
  {
    return _receiver.get_cancelation_token();
  }
  template <typename... V>
  void set_value(V... vs)
  {
    if (_receiver.get_cancelation_token()->is_cancel_requested())
      return set_done();

    _receiver.get_cancelation_token()->reset();
    try
    {
      _fun(_receiver, std::forward<V>(vs)...);
    }
    catch (...)
    {
      _receiver.set_error(std::current_exception());
    }
  }
  template <typename E>
  void set_error(E&& e)
  {
    _receiver.get_cancelation_token()->reset();
    _receiver.set_error(std::forward<E>(e));
  }
  void set_done()
  {
    _receiver.get_cancelation_token()->reset();
    _receiver.set_done();
  }
};

template <typename Sender, typename F, typename... Result>
struct async_then_sender
{
  template <template <typename...> class Tuple>
  using value_types = Tuple<Result...>;

  Sender sender;
  F fun;

  template <typename R>
  void submit(R&& receiver)
  {
    sender.submit(async_then_receiver<std::decay_t<R>, decltype(fun)>{
        std::forward<R>(receiver), std::move(fun)});
  };
};

template <typename T>
struct connect_impl;

template <typename... R>
struct connect_impl<std::tuple<R...>>
{
  template <typename Sender1, typename Sender2>
  static auto connect(Sender1&& sender1, Sender2&& sender2)
  {
    auto lambda = [sender2 =
                       std::forward<Sender2>(sender2)](auto... args) mutable {
      sender2.submit(std::forward<decltype(args)>(args)...);
    };
    return detail::
        async_then_sender<std::decay_t<Sender1>, decltype(lambda), R...>{
            std::forward<Sender1>(sender1), std::move(lambda)};
  }
};
}

template <typename Sender, typename F>
auto then(Sender&& sender, F&& fun)
{
  return detail::then_sender<std::decay_t<Sender>, std::decay_t<F>>{
      std::forward<Sender>(sender), std::forward<F>(fun)};
}

template <typename... R, typename Sender, typename F>
auto async_then(Sender&& sender, F&& fun)
{
  return detail::async_then_sender<std::decay_t<Sender>, std::decay_t<F>, R...>{
      std::forward<Sender>(sender), std::forward<F>(fun)};
}

template <typename Sender1, typename Sender2>
auto connect(Sender1&& sender1, Sender2&& sender2)
{
  return detail::
      connect_impl<typename Sender2::template value_types<std::tuple>>::connect(
          std::forward<Sender1>(sender1), std::forward<Sender2>(sender2));
}
}
}

#endif
