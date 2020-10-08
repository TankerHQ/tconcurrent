#ifndef TCONCURRENT_TEST_UTILS_HPP
#define TCONCURRENT_TEST_UTILS_HPP

#include <type_traits>
#include <utility>

namespace
{
template <typename F, typename Ret>
struct lambda_sender
{
  template <template <typename...> class Tuple>
  using value_types = Tuple<Ret>;

  F f;

  template <typename R>
  void submit(R&& receiver) const
  {
    f(std::forward<R>(receiver));
  }
};

template <typename F>
struct lambda_sender<F, void> : lambda_sender<F, int>
{
  template <template <typename...> class Tuple>
  using value_types = Tuple<>;
};

template <typename R, typename F>
auto make_sender(F&& f)
{
  return lambda_sender<std::decay_t<F>, R>{std::forward<F>(f)};
}
}

#endif
