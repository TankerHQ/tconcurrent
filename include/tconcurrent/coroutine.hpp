#ifndef TCONCURRENT_COROUTINE_HPP
#define TCONCURRENT_COROUTINE_HPP

#include <functional>

#include <boost/scope_exit.hpp>
#include <boost/context/execution_context_v2.hpp>

#include <tconcurrent/async.hpp>
#include <tconcurrent/packaged_task.hpp>

#include <tconcurrent/detail/export.hpp>

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
  std::string name;

  boost::context::fixedsize_stack salloc;
  boost::context::stack_context stack;

  coroutine_t ctx;
  coroutine_t* argctx;

  cancelation_token& token;

  template <typename F>
  coroutine_control(std::string name, F&& f, cancelation_token& token)
    : name(std::move(name)),
      salloc(boost::context::stack_traits::default_size() * 2),
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

TCONCURRENT_EXPORT
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

/** Unschedule the coroutine while \p awaitable is not ready
 *
 * If \p awaitable is already ready, no context-switch occur. This call is a
 * cancelation point, it can throw operation_canceled if a cancelation is
 * requested, even if \p awaitable finished with a value.
 *
 * \param awaitable what should be awaited, usually a future
 *
 * \return the value contained in \p awaitable if there is one
 *
 * \throw the exception contained in \p awaitable if there is one
 */
template <typename Awaitable>
typename std::decay_t<Awaitable>::value_type coroutine_control::operator()(
    Awaitable&& awaitable)
{
  std::decay_t<Awaitable> finished_awaitable;
  if (awaitable.is_ready())
    finished_awaitable = std::move(awaitable);
  else
  {
    auto canceler =
        token.make_scope_canceler(awaitable.make_canceler());

    TC_SANITIZER_OPEN_RETURN_CONTEXT()
    *argctx = std::get<0>(
        (*argctx)([&finished_awaitable, &awaitable](coroutine_control* ctrl) {
          std::move(awaitable).update_chain_name(ctrl->name)
              .then([&finished_awaitable, ctrl](std::decay_t<Awaitable> f) {
                finished_awaitable = std::move(f);
                run_coroutine(ctrl);
              });
        }));
    TC_SANITIZER_CLOSE_SWITCH_CONTEXT()
  }
  if (token.is_cancel_requested())
    throw operation_canceled{};
  return finished_awaitable.get();
}

/** Unschedule the coroutine immediately and put it in the task queue.
 *
 * This is a cancelation point, if a cancelation is requested before or after
 * the yield actually occurs, operation_canceled will be thrown.
 */
inline void coroutine_control::yield()
{
  if (token.is_cancel_requested())
    throw operation_canceled{};
  TC_SANITIZER_OPEN_RETURN_CONTEXT()
  *argctx = std::get<0>((*argctx)([](coroutine_control* ctrl) {
    tc::async(ctrl->name, [ctrl] { run_coroutine(ctrl); });
  }));
  TC_SANITIZER_CLOSE_SWITCH_CONTEXT()
  if (token.is_cancel_requested())
    throw operation_canceled{};
}

}

using awaiter = detail::coroutine_control;

/** Create a resumable function and schedule it
 *
 * \param name (optional) the name of the coroutine, only used for debugging
 * purposes
 * \param cb the callback to run. Its signature should be:
 *
 *     T func(awaiter& await);
 *
 * \return the future corresponding to the result of the callback
 */
template <typename F>
auto async_resumable(std::string const& name, F&& cb)
{
  using return_type =
      std::decay_t<decltype(cb(std::declval<detail::coroutine_control&>()))>;

  return async(name, [cb = std::forward<F>(cb), name](
          cancelation_token& token) mutable {
    auto pack = package<return_type(detail::coroutine_control&)>(std::move(cb));

    detail::coroutine_control* cs =
        new detail::coroutine_control(
            name + " (" + typeid(F).name() + ")",
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

    return std::move(pack.second);
  }).unwrap();
}

template <typename F>
auto async_resumable(F&& cb)
{
  return async_resumable({}, std::forward<F>(cb));
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
