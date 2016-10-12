#ifndef TCONCURRENT_COROUTINE_HPP
#define TCONCURRENT_COROUTINE_HPP

#include <functional>

#include <boost/scope_exit.hpp>
#include <boost/context/execution_context_v2.hpp>

#include <tconcurrent/async.hpp>
#include <tconcurrent/packaged_task.hpp>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TCONCURRENT_SANITIZER
#endif
#endif

#ifdef TCONCURRENT_SANITIZER
#include <sanitizer/common_interface_defs.h>
#endif

namespace tconcurrent
{
namespace detail
{

#ifdef TCONCURRENT_SANITIZER

struct stack_bounds
{
  void* stack;
  size_t size;
};

stack_bounds get_stack_bounds();

#define TC_SANITIZER_OPEN_SWITCH_CONTEXT(stack, stacksize) \
  {                                                        \
    void* fsholder;                                        \
    __sanitizer_start_switch_fiber(&fsholder, stack, stacksize);

#define TC_SANITIZER_OPEN_RETURN_CONTEXT()                \
  {                                                       \
    void* fsholder;                                       \
    auto const stack_bounds = detail::get_stack_bounds(); \
    __sanitizer_start_switch_fiber(                       \
        &fsholder, stack_bounds.stack, stack_bounds.size);

#define TC_SANITIZER_CLOSE_SWITCH_CONTEXT()    \
    __sanitizer_finish_switch_fiber(fsholder); \
  }

#define TC_SANITIZER_ENTER_NEW_CONTEXT() \
  __sanitizer_finish_switch_fiber(nullptr)

#define TC_SANITIZER_EXIT_CONTEXT()                     \
  auto const stack_bounds = detail::get_stack_bounds(); \
  __sanitizer_start_switch_fiber(                       \
      nullptr, stack_bounds.stack, stack_bounds.size);

#else

#define TC_SANITIZER_OPEN_SWITCH_CONTEXT(stack, stacksize)
#define TC_SANITIZER_OPEN_RETURN_CONTEXT()
#define TC_SANITIZER_CLOSE_SWITCH_CONTEXT()
#define TC_SANITIZER_ENTER_NEW_CONTEXT()
#define TC_SANITIZER_EXIT_CONTEXT()

#endif

using coroutine_controller = std::function<void(struct coroutine_control*)>;
using coroutine_t = boost::context::execution_context<coroutine_controller>;

struct coroutine_control
{
  boost::context::fixedsize_stack salloc;
  boost::context::stack_context stack;

  coroutine_t ctx;
  coroutine_t* argctx;

  cancelation_token& token;

  template <typename F>
  coroutine_control(F&& f, cancelation_token& token)
    : salloc(boost::context::stack_traits::default_size() * 2),
      stack(salloc.allocate()),
      ctx(std::allocator_arg,
          boost::context::preallocated(stack.sp, stack.size, stack),
          salloc,
          std::forward<F>(f)),
      argctx(nullptr),
      token(token)
  {}

  coroutine_control(coroutine_control const&) = delete;
  coroutine_control(coroutine_control&&) = delete;
  coroutine_control& operator=(coroutine_control const&) = delete;
  coroutine_control& operator=(coroutine_control&&) = delete;

  template <typename Awaitable>
  typename std::decay_t<Awaitable>::value_type operator()(
      Awaitable&& awaitable);

  void yield();
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

  TC_SANITIZER_OPEN_SWITCH_CONTEXT(
      reinterpret_cast<char const*>(ctrl->stack.sp) - ctrl->stack.size,
      ctrl->stack.size)
  std::tie(ctrl->ctx, f) = ctrl->ctx({});
  TC_SANITIZER_CLOSE_SWITCH_CONTEXT()

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
  if (!awaitable.is_ready())
  {
    auto canceler =
        token.make_scope_canceler([&] { awaitable.request_cancel(); });

    TC_SANITIZER_OPEN_RETURN_CONTEXT()
    *argctx = std::get<0>((*argctx)([&awaitable](coroutine_control* ctrl) {
      awaitable.then([ctrl](auto const& f) { run_coroutine(ctrl); });
    }));
    TC_SANITIZER_CLOSE_SWITCH_CONTEXT()
  }
  if (token.is_cancel_requested())
    throw operation_canceled{};
  return awaitable.get();
}

inline void coroutine_control::yield()
{
  if (token.is_cancel_requested())
    throw operation_canceled{};
  TC_SANITIZER_OPEN_RETURN_CONTEXT()
  *argctx = std::get<0>((*argctx)([](coroutine_control* ctrl) {
    tc::async([ctrl] { run_coroutine(ctrl); });
  }));
  TC_SANITIZER_CLOSE_SWITCH_CONTEXT()
  if (token.is_cancel_requested())
    throw operation_canceled{};
}

}

using awaiter = detail::coroutine_control;

template <typename F>
auto async_resumable(F&& cb)
{
  using return_type =
      std::decay_t<decltype(cb(std::declval<detail::coroutine_control&>()))>;

  return async([cb = std::forward<F>(cb)](cancelation_token& token) mutable {
    auto pack = package<return_type(detail::coroutine_control&)>(std::move(cb));

    detail::coroutine_control* cs =
        new detail::coroutine_control(
            [cb = std::move(pack.first), &cs](
                detail::coroutine_t argctx,
                detail::coroutine_controller const&) {
              TC_SANITIZER_ENTER_NEW_CONTEXT();
              auto mycs = cs;
              mycs->argctx = &argctx;

              TC_SANITIZER_OPEN_RETURN_CONTEXT();
              *mycs->argctx = std::move(std::get<0>(argctx([](
                  detail::coroutine_control* ctrl) { run_coroutine(ctrl); })));
              TC_SANITIZER_CLOSE_SWITCH_CONTEXT();

              cb(*mycs);

              TC_SANITIZER_EXIT_CONTEXT()
              return argctx;
            },
            token);

    detail::coroutine_controller f;
    {
      TC_SANITIZER_OPEN_SWITCH_CONTEXT(
          reinterpret_cast<char const*>(cs->stack.sp) - cs->stack.size,
          cs->stack.size)
      std::tie(cs->ctx, f) = cs->ctx({});
      TC_SANITIZER_CLOSE_SWITCH_CONTEXT()
    }

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
