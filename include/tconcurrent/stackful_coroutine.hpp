#ifndef TCONCURRENT_STACKFUL_COROUTINE_HPP
#define TCONCURRENT_STACKFUL_COROUTINE_HPP

#include <functional>

#include <boost/context/fiber.hpp>
#include <boost/scope_exit.hpp>

#include <tconcurrent/async.hpp>
#include <tconcurrent/packaged_task.hpp>

#include <tconcurrent/detail/export.hpp>

#if TCONCURRENT_SANITIZER
#include <sanitizer/common_interface_defs.h>
#endif

#if __cplusplus >= 201703L
#define TCONCURRENT_NODISCARD [[nodiscard]]
#else
#define TCONCURRENT_NODISCARD
#endif

#if TCONCURRENT_ALLOW_CANCEL_IN_CATCH
#error TCONCURRENT_ALLOW_CANCEL_IN_CATCH is not allowed when compiling in the non-coroutines-TS mode.
#endif

namespace tconcurrent
{
template <typename E, typename F>
auto async_resumable(std::string const& name, E&& executor, F&& cb);

namespace detail
{

class coroutine_control;

#if TCONCURRENT_SANITIZER

struct stack_bounds
{
  void const* stack;
  size_t size;
};

stack_bounds get_stack_bounds(coroutine_control* ctrl);

#define TC_SANITIZER_OPEN_SWITCH_CONTEXT(stack, stacksize) \
  {                                                        \
    void* fsholder;                                        \
    __sanitizer_start_switch_fiber(&fsholder, stack, stacksize);

#define TC_SANITIZER_OPEN_RETURN_CONTEXT(ctrl)                \
  {                                                           \
    void* fsholder;                                           \
    auto const stack_bounds = detail::get_stack_bounds(ctrl); \
    __sanitizer_start_switch_fiber(                           \
        &fsholder, stack_bounds.stack, stack_bounds.size);

#define TC_SANITIZER_CLOSE_SWITCH_CONTEXT()                    \
  __sanitizer_finish_switch_fiber(fsholder, nullptr, nullptr); \
  }

#define TC_SANITIZER_ENTER_NEW_CONTEXT() \
  __sanitizer_finish_switch_fiber(nullptr, nullptr, nullptr)

#define TC_SANITIZER_EXIT_CONTEXT(ctrl)                     \
  auto const stack_bounds = detail::get_stack_bounds(ctrl); \
  __sanitizer_start_switch_fiber(                           \
      nullptr, stack_bounds.stack, stack_bounds.size);

#else

#define TC_SANITIZER_OPEN_SWITCH_CONTEXT(stack, stacksize)
#define TC_SANITIZER_OPEN_RETURN_CONTEXT(ctrl)
#define TC_SANITIZER_CLOSE_SWITCH_CONTEXT()
#define TC_SANITIZER_ENTER_NEW_CONTEXT()
#define TC_SANITIZER_EXIT_CONTEXT(ctrl)

#endif

void assert_not_in_catch();

/// Thrown inside a coroutine to stop it
struct abort_coroutine
{
};

template <typename T>
struct coroutine_finish;

using coroutine_t = boost::context::fiber;

enum class coroutine_status
{
  waiting,
  finished,
  aborted,
};

coroutine_status run_coroutine(coroutine_control* ctrl);

class coroutine_control
{
public:
  coroutine_control(coroutine_control const&) = delete;
  coroutine_control(coroutine_control&&) = delete;
  coroutine_control& operator=(coroutine_control const&) = delete;
  coroutine_control& operator=(coroutine_control&&) = delete;

  template <typename Awaitable>
  typename std::decay_t<Awaitable>::value_type operator()(
      Awaitable&& awaitable);

  void yield();

private:
  std::string name;

  executor executor_;

  boost::context::fixedsize_stack salloc;
  boost::context::stack_context stack;

  coroutine_t ctx;
  coroutine_t* argctx;

  std::function<void(coroutine_control*)> coroutine_exit_post_setup;

  cancelation_token& token;

  bool aborted = false;

  coroutine_control* previous_coroutine = nullptr;

  template <typename E, typename F>
  coroutine_control(std::string name, E&& e, F&& f, cancelation_token& token)
    : name(std::move(name))
    , executor_(std::forward<E>(e))
    , salloc(boost::context::stack_traits::default_size() * 2)
    , stack(salloc.allocate())
    , ctx(std::allocator_arg,
          boost::context::preallocated(stack.sp, stack.size, stack),
          salloc,
          std::forward<F>(f))
    , argctx(nullptr)
    , token(token)
  {
  }

  template <typename Awaitable>
  typename std::decay_t<Awaitable>::value_type await(Awaitable awaitable,
                                                     bool early_return);

  template <typename E, typename F>
  friend auto ::tconcurrent::async_resumable(std::string const& name,
                                             E&& executor,
                                             F&& cb);
#if TCONCURRENT_SANITIZER
  friend stack_bounds get_stack_bounds(coroutine_control* ctrl);
#endif
  friend coroutine_status run_coroutine(coroutine_control* ctrl);
  template <typename T>
  friend struct coroutine_finish;
};

TCONCURRENT_EXPORT
detail::coroutine_control*& get_current_coroutine_ptr();

inline coroutine_status run_coroutine(coroutine_control* ctrl)
{
  auto& ptr = get_current_coroutine_ptr();
  assert(!ctrl->previous_coroutine);
  auto const previous_coroutine = ptr;
  ctrl->previous_coroutine = previous_coroutine;
  ptr = ctrl;
  BOOST_SCOPE_EXIT(&ptr, &ctrl, &previous_coroutine)
  {
    ptr = previous_coroutine;
    if (ctrl)
      ctrl->previous_coroutine = nullptr;
  }
  BOOST_SCOPE_EXIT_END

  TC_SANITIZER_OPEN_SWITCH_CONTEXT(
      reinterpret_cast<char const*>(ctrl->stack.sp) - ctrl->stack.size,
      ctrl->stack.size)
  ctrl->ctx = std::move(ctrl->ctx).resume();
  TC_SANITIZER_CLOSE_SWITCH_CONTEXT()

  // if coroutine just finished
  if (!ctrl->ctx)
  {
    auto const status =
        ctrl->aborted ? coroutine_status::aborted : coroutine_status::finished;
    delete ctrl;
    ctrl = nullptr;
    return status;
  }

  if (ctrl->coroutine_exit_post_setup)
  {
    ctrl->coroutine_exit_post_setup(ctrl);
    ctrl->coroutine_exit_post_setup = nullptr;
  }
  return coroutine_status::waiting;
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
  return await(std::forward<Awaitable>(awaitable), true);
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

  await(tc::make_ready_future(), false);
}

template <typename Awaitable>
typename std::decay_t<Awaitable>::value_type coroutine_control::await(
    Awaitable awaitable, bool early_return)
{
  assert_not_in_catch();

  using FutureType = std::decay_t<Awaitable>;

  FutureType finished_awaitable;
  // atomic because we don't want the compiler to reorder instructions
  auto const aborted = std::make_shared<std::atomic<bool>>(false);

  if (early_return && awaitable.is_ready())
    finished_awaitable = std::move(awaitable);
  else
  {
    auto progressing_awaitable =
        std::move(awaitable).update_chain_name(this->name);

    auto canceler =
        token.make_scope_canceler([this, aborted, &progressing_awaitable] {
          assert_not_in_catch();
          assert(get_default_executor().is_in_this_context());

          progressing_awaitable.request_cancel();

          *aborted = true;
          // run the coroutine one last time so that it can abort
          auto const status = run_coroutine(this);
          (void)status;
          assert(status == coroutine_status::aborted &&
                 "tc::detail::abort_coroutine must never be caught");
        });

    coroutine_exit_post_setup = [&aborted,
                                 &progressing_awaitable,
                                 &finished_awaitable](coroutine_control* ctrl) {
      progressing_awaitable.then(
          ctrl->executor_,
          [aborted, &finished_awaitable, ctrl](std::decay_t<Awaitable> f) {
            // cancel was called, the coroutine is already dead and the
            // memory free
            if (*aborted)
              return;

            finished_awaitable = std::move(f);
            run_coroutine(ctrl);
          });
    };

    TC_SANITIZER_OPEN_RETURN_CONTEXT(this)
    *argctx = std::move(*argctx).resume();
    TC_SANITIZER_CLOSE_SWITCH_CONTEXT()
  }
  if (*aborted)
    throw abort_coroutine{};
  if (token.is_cancel_requested())
    throw operation_canceled{};
  return finished_awaitable.get();
}
}

using awaiter = detail::coroutine_control;

inline awaiter& get_current_awaiter()
{
  auto ptr = detail::get_current_coroutine_ptr();
  if (!ptr)
    throw std::runtime_error("calling await from outside of a coroutine!");
  return *ptr;
}

inline void yield()
{
  get_current_awaiter().yield();
}

namespace detail
{
template <typename T>
struct cotask_value
{
  T val;
};

template <typename T>
class TCONCURRENT_NODISCARD cotask_impl
{
public:
  using value_type = T;

  cotask_impl(cotask_impl const&) = delete;
  cotask_impl& operator=(cotask_impl const&) = delete;
  cotask_impl(cotask_impl&&) = default;
  cotask_impl& operator=(cotask_impl&&) = default;

  template <typename U>
  cotask_impl(detail::cotask_value<U> value) : _value(static_cast<U>(value.val))
  {
  }

  decltype(auto) get() &&
  {
    return std::forward<T>(_value);
  }

private:
  T _value;
};

template <>
class TCONCURRENT_NODISCARD cotask_impl<void>
{
public:
  using value_type = void;

  void get() &&
  {
  }
};

template <typename T>
struct task_return_type;

template <typename T>
struct task_return_type<cotask_impl<T>>
{
  using type = typename cotask_impl<T>::value_type;
};

template <>
struct task_return_type<void>
{
  using type = void;
};

template <typename T>
struct cotask_alias
{
  using type = cotask_impl<T>;
};

template <>
struct cotask_alias<void>
{
  using type = void;
};

template <typename T>
struct coroutine_finish
{
  detail::coroutine_control* cs;

  auto operator()(future<cotask_impl<T>>&& fut)
  {
    try
    {
      return fut.get().get();
    }
    catch (detail::abort_coroutine)
    {
      cs->aborted = true;
      throw operation_canceled{};
    }
  }
};

template <>
struct coroutine_finish<void>
{
  detail::coroutine_control* cs;

  void operator()(future<void>&& fut)
  {
    try
    {
      fut.get();
    }
    catch (detail::abort_coroutine)
    {
      cs->aborted = true;
      throw operation_canceled{};
    }
  }
};
}

template <typename T>
using cotask = typename detail::cotask_alias<T>::type;

namespace detail
{
template <typename T>
auto wrap_task(T&& value)
{
  return detail::cotask_value<T&&>{std::forward<T>(value)};
}

inline cotask<void> wrap_task()
{
  return cotask<void>();
}
}

template <typename E, typename F>
auto async_resumable(std::string const& name, E&& executor, F&& cb)
{
  using return_task_type = std::decay_t<decltype(cb())>;
  using return_type = typename detail::task_return_type<return_task_type>::type;

  auto const fullName = name + " (" + typeid(F).name() + ")";

  auto token = std::make_shared<cancelation_token>();
  auto pack = package_cancelable<future<return_type>()>(
      [executor, cb = std::forward<F>(cb), fullName, token]() mutable {
        auto pack = package<return_task_type()>(std::move(cb), token);

        detail::coroutine_control* cs = new detail::coroutine_control(
            fullName,
            executor,
            [cb = std::move(pack.first), &cs](detail::coroutine_t&& argctx) {
              TC_SANITIZER_ENTER_NEW_CONTEXT();
              auto mycs = cs;
              mycs->argctx = &argctx;

              TC_SANITIZER_OPEN_RETURN_CONTEXT(mycs);
              *mycs->argctx = std::move(*mycs->argctx).resume();
              TC_SANITIZER_CLOSE_SWITCH_CONTEXT();

              cb();

              TC_SANITIZER_EXIT_CONTEXT(mycs)
              return std::move(argctx);
            },
            *token);

        {
          TC_SANITIZER_OPEN_SWITCH_CONTEXT(
              reinterpret_cast<char const*>(cs->stack.sp) - cs->stack.size,
              cs->stack.size)
          cs->ctx = std::move(cs->ctx).resume();
          TC_SANITIZER_CLOSE_SWITCH_CONTEXT()
        }

        detail::run_coroutine(cs);

        return pack.second.then(tc::get_synchronous_executor(),
                                detail::coroutine_finish<return_type>{cs});
      },
      token);

  executor.post(std::move(std::get<0>(pack)), fullName);

  return std::move(std::get<1>(pack)).update_chain_name(fullName).unwrap();
}

namespace detail
{
struct await_impl
{
  template <typename T>
  T&& operator,(cotask_impl<T>&& task)
  {
    return std::move(task).get();
  }
  template <typename Awaitable>
  auto operator,(Awaitable&& awaitable)
  {
    return get_current_awaiter()(std::forward<Awaitable>(awaitable));
  }
};
}
}

#define TC_AWAIT(future) (tc::detail::await_impl(), future)
#define TC_YIELD() ::tc::yield()
#define TC_RETURN(value)                   \
  do                                       \
  {                                        \
    return ::tc::detail::wrap_task(value); \
  } while (false)

#endif
