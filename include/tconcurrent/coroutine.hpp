#ifndef TCONCURRENT_COROUTINE_HPP
#define TCONCURRENT_COROUTINE_HPP

#include <functional>

#include <boost/scope_exit.hpp>
#include <boost/context/execution_context_v2.hpp>

#include <tconcurrent/async.hpp>
#include <tconcurrent/packaged_task.hpp>

namespace tconcurrent
{
namespace detail
{

using coroutine_controller = std::function<void(struct coroutine_control*)>;
using coroutine_t = boost::context::execution_context<coroutine_controller>;

struct coroutine_control
{
  coroutine_t ctx;
  coroutine_t* argctx;

  cancelation_token& token;

  coroutine_control(coroutine_t ctx, cancelation_token& token)
    : ctx(std::move(ctx)), argctx(nullptr), token(token)
  {}
  coroutine_control(coroutine_control const&) = delete;
  coroutine_control(coroutine_control&&) = delete;
  coroutine_control& operator=(coroutine_control const&) = delete;
  coroutine_control& operator=(coroutine_control&&) = delete;

  template <typename Awaitable>
  typename std::decay_t<Awaitable>::value_type operator()(
      Awaitable&& awaitable);
};

detail::coroutine_control*& get_current_coroutine_ptr();

inline void run_coroutine(coroutine_control* ctrl)
{
  auto& ptr = get_current_coroutine_ptr();
  assert(!ptr);
  ptr = ctrl;
  BOOST_SCOPE_EXIT(&ptr) {
    ptr = nullptr;
  } BOOST_SCOPE_EXIT_END

  coroutine_controller f;
  std::tie(ctrl->ctx, f) = ctrl->ctx({});
  // if coroutine just finished
  if (!ctrl->ctx)
  {
    delete ctrl;
    return;
  }
  f(ctrl);
}

template <typename Awaitable>
typename std::decay_t<Awaitable>::value_type coroutine_control::operator()(
    Awaitable&& awaitable)
{
  if (token.is_cancel_requested())
    throw operation_canceled{};
  if (!awaitable.is_ready())
  {
    auto canceler =
        token.make_scope_canceler([&] { awaitable.request_cancel(); });
    *argctx = std::get<0>((*argctx)([&awaitable](coroutine_control* ctrl) {
      awaitable.then([ctrl](auto const& f) { run_coroutine(ctrl); });
    }));
  }
  if (token.is_cancel_requested())
    throw operation_canceled{};
  return awaitable.get();
}

}

using awaiter = detail::coroutine_control;

template <typename F>
auto async_resumable(F&& cb)
{
  using return_type =
      std::decay_t<decltype(cb(std::declval<detail::coroutine_control&>()))>;

  return async([cb = std::forward<F>(cb)](cancelation_token& token) {
    auto pack = package<return_type(detail::coroutine_control&)>(std::move(cb));
    token.propagate_cancel_to(pack.second);

    detail::coroutine_control* cs = new detail::coroutine_control(
        detail::coroutine_t(
            std::allocator_arg,
            boost::context::fixedsize_stack(
                boost::context::stack_traits::default_size() * 2),
            [cb = std::move(pack.first), &cs](
                detail::coroutine_t argctx,
                detail::coroutine_controller const&) {
              auto mycs = cs;
              mycs->argctx = &argctx;

              *mycs->argctx = std::move(std::get<0>(argctx([](
                  detail::coroutine_control* ctrl) { run_coroutine(ctrl); })));

              cb(*mycs);

              return argctx;
            }),
        token);

    detail::coroutine_controller f;
    std::tie(cs->ctx, f) = cs->ctx({});
    f(cs);

    return pack.second;
  }).unwrap();
}

inline awaiter& get_current_awaiter()
{
  auto ptr = detail::get_current_coroutine_ptr();
  if (!ptr)
    throw std::runtime_error("calling await from outside of a coroutine!");
  return *ptr;
}

template <typename Awaitable>
inline auto await(Awaitable&& awaitable)
{
  return get_current_awaiter()(std::forward<Awaitable>(awaitable));
}

}

#endif
