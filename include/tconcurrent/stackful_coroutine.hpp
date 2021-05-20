#ifndef TCONCURRENT_STACKFUL_COROUTINE_HPP
#define TCONCURRENT_STACKFUL_COROUTINE_HPP

#include <function2/function2.hpp>

#include <boost/context/fiber.hpp>
#include <boost/scope_exit.hpp>

#include <tconcurrent/async.hpp>
#include <tconcurrent/detail/tvoid.hpp>
#include <tconcurrent/lazy/async.hpp>
#include <tconcurrent/lazy/detail.hpp>
#include <tconcurrent/lazy/sink_receiver.hpp>
#include <tconcurrent/lazy/then.hpp>
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
namespace lazy
{
namespace detail
{
template <typename E, typename F>
struct run_resumable_sender;
}
}
template <typename F>
auto dispatch_on_thread_context(F&& f);

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

#define TC_SANITIZER_OPEN_RETURN_CONTEXT(ctrl)                               \
  {                                                                          \
    void* fsholder;                                                          \
    auto const stack_bounds = ::tconcurrent::detail::get_stack_bounds(ctrl); \
    __sanitizer_start_switch_fiber(                                          \
        &fsholder, stack_bounds.stack, stack_bounds.size);

#define TC_SANITIZER_CLOSE_SWITCH_CONTEXT()                    \
  __sanitizer_finish_switch_fiber(fsholder, nullptr, nullptr); \
  }

#define TC_SANITIZER_ENTER_NEW_CONTEXT() \
  __sanitizer_finish_switch_fiber(nullptr, nullptr, nullptr)

#define TC_SANITIZER_EXIT_CONTEXT(ctrl)                                    \
  auto const stack_bounds = ::tconcurrent::detail::get_stack_bounds(ctrl); \
  __sanitizer_start_switch_fiber(                                          \
      nullptr, stack_bounds.stack, stack_bounds.size);

#else

#define TC_SANITIZER_OPEN_SWITCH_CONTEXT(stack, stacksize)
#define TC_SANITIZER_OPEN_RETURN_CONTEXT(ctrl)
#define TC_SANITIZER_CLOSE_SWITCH_CONTEXT()
#define TC_SANITIZER_ENTER_NEW_CONTEXT()
#define TC_SANITIZER_EXIT_CONTEXT(ctrl)

#endif

void assert_not_in_catch(char const* reason);

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
  auto operator()(Awaitable&& awaitable);

  void yield();

private:
  std::string name;

  executor executor_;

  boost::context::fixedsize_stack salloc;
  boost::context::stack_context stack;

  coroutine_t ctx;
  coroutine_t* argctx;

  fu2::unique_function<void()> work_for_thread_context;
  fu2::unique_function<void(coroutine_control*)> coroutine_exit_post_setup;

  lazy::cancelation_token* token;

  bool aborted = false;

  coroutine_control* previous_coroutine = nullptr;

  template <typename E, typename F>
  coroutine_control(std::string name, E&& e, F&& f)
    : name(std::move(name))
    , executor_(std::forward<E>(e))
    , salloc(boost::context::stack_traits::default_size() * 2)
    , stack(salloc.allocate())
    , ctx(std::allocator_arg,
          boost::context::preallocated(stack.sp, stack.size, stack),
          salloc,
          std::forward<F>(f))
    , argctx(nullptr)
  {
  }

  template <typename Sender>
  auto await(Sender sender,
             bool early_return,
             detail::void_t<typename std::decay_t<Sender>::template value_types<
                 std::tuple>>** = nullptr);
  template <typename Awaitable>
  typename std::decay_t<Awaitable>::value_type await(
      Awaitable awaitable,
      bool early_return,
      // SFINAE to check if it looks like a future
      // I originally wrote something to test if then() existed, but I didn't
      // manage to make it work
      detail::void_t<typename std::decay_t<Awaitable>::value_type>** = nullptr);

  template <typename F>
  auto dispatch_on_thread_context(F&& work);

  template <typename F>
  friend auto ::tconcurrent::dispatch_on_thread_context(F&& f);

  template <typename E, typename F>
  friend struct lazy::detail::run_resumable_sender;

#if TCONCURRENT_SANITIZER
  friend stack_bounds get_stack_bounds(coroutine_control* ctrl);
#endif
  friend coroutine_status run_coroutine(coroutine_control* ctrl);
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

  while (true)
  {
    TC_SANITIZER_OPEN_SWITCH_CONTEXT(
        reinterpret_cast<char const*>(ctrl->stack.sp) - ctrl->stack.size,
        ctrl->stack.size)
    ctrl->ctx = std::move(ctrl->ctx).resume();
    TC_SANITIZER_CLOSE_SWITCH_CONTEXT()
    if (ctrl->work_for_thread_context)
    {
      ptr = nullptr;
      ctrl->work_for_thread_context();
      ptr = ctrl;
    }
    else
      break;
  }

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
auto coroutine_control::operator()(Awaitable&& awaitable)
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
  if (token->is_cancel_requested())
    throw operation_canceled{};

  await(tc::make_ready_future(), false);
}

template <typename T>
struct await_receiver_state
{
  struct v_none
  {
  };
  struct v_value
  {
    T value;
  };
  struct v_exception
  {
    std::exception_ptr exc;
  };

  coroutine_control* ctrl;
  lazy::cancelation_token cancelation_token;
  boost::variant2::variant<v_none, v_exception, v_value> result;
};

template <class T>
struct await_receiver_base
{
  using state_type = await_receiver_state<T>;

  state_type* _state;
  executor _executor;

  // atomic because we don't want the compiler to reorder instructions
  std::shared_ptr<std::atomic<bool>> _aborted =
      std::make_shared<std::atomic<bool>>(false);

  auto get_cancelation_token()
  {
    return &_state->cancelation_token;
  }
  template <typename V>
  void _set(V&& v)
  {
    _executor.post(
        [state = _state, aborted = _aborted, v = std::forward<V>(v)] {
          // cancel was called, the coroutine is already dead and the memory
          // free
          if (*aborted)
            return;

          state->cancelation_token.reset();
          state->result.template emplace<std::decay_t<V>>(std::move(v));

          run_coroutine(state->ctrl);
        });
  }
  template <typename E>
  void set_error(E&& e)
  {
    _set(typename state_type::v_exception{std::forward<E>(e)});
  }
  void set_done()
  {
    _set(typename state_type::v_exception{
        std::make_exception_ptr(operation_canceled())});
  }
};

template <class T>
struct await_receiver : await_receiver_base<T>
{
  await_receiver(await_receiver_state<T>* state, executor e)
    : await_receiver_base<T>{state, std::move(e)}
  {
  }

  void set_value(T&& v)
  {
    this->_set(typename await_receiver_state<T>::v_value{std::forward<T>(v)});
  }
};

template <>
struct await_receiver<void> : await_receiver_base<tvoid>
{
  await_receiver(state_type* state, executor e)
    : await_receiver_base<tvoid>{state, std::move(e)}
  {
  }

  void set_value()
  {
    this->_set(await_receiver_state<tvoid>::v_value{{}});
  }
};

template <typename Sender>
auto coroutine_control::await(
    Sender sender,
    bool early_return,
    detail::void_t<
        typename std::decay_t<Sender>::template value_types<std::tuple>>**)
{
  assert_not_in_catch("awaiting a sender");

  using return_type = lazy::detail::extract_single_value_type_t<Sender>;
  using state_type = typename await_receiver<return_type>::state_type;

  state_type state{this};
  await_receiver<return_type> receiver(&state, executor_);

  auto canceler = token->make_scope_canceler(
      [this, &state, aborted = receiver._aborted]() mutable {
        assert_not_in_catch("canceling a coroutine awaiting on a sender");
        assert(this->executor_.is_in_this_context());

        state.cancelation_token.request_cancel();

        *aborted = true;
        // run the coroutine one last time so that it can abort
        auto const status = run_coroutine(this);
        (void)status;
        assert(status == coroutine_status::aborted &&
               "tc::detail::abort_coroutine must never be caught");
      });

  coroutine_exit_post_setup = [sender = std::move(sender),
                               &receiver](coroutine_control* ctrl) mutable {
    sender.submit(receiver);
  };

  TC_SANITIZER_OPEN_RETURN_CONTEXT(this)
  *argctx = std::move(*argctx).resume();
  TC_SANITIZER_CLOSE_SWITCH_CONTEXT()
  if (*receiver._aborted)
    throw abort_coroutine{};
  if (token->is_cancel_requested())
    throw operation_canceled{};
  if (auto const exc =
          boost::variant2::get_if<typename state_type::v_exception>(
              &state.result))
    std::rethrow_exception(exc->exc);
  return std::move(
      boost::variant2::get<typename state_type::v_value>(state.result).value);
}

template <typename Awaitable>
typename std::decay_t<Awaitable>::value_type coroutine_control::await(
    Awaitable awaitable,
    bool early_return,
    detail::void_t<typename std::decay_t<Awaitable>::value_type>**)
{
  assert_not_in_catch("awaiting a future");

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
        token->make_scope_canceler([this, aborted, &progressing_awaitable] {
          assert_not_in_catch("canceling a coroutine awaiting a future");
          assert(this->executor_.is_in_this_context());

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
  if (token->is_cancel_requested())
    throw operation_canceled{};
  return finished_awaitable.get();
}

template <typename F>
auto coroutine_control::dispatch_on_thread_context(F&& work)
{
  using return_type = decltype(work());

  assert(!work_for_thread_context);
  auto pack = ::tconcurrent::package<return_type()>(std::forward<F>(work));
  work_for_thread_context = [f = std::move(pack.first)]() { f(); };

  TC_SANITIZER_OPEN_RETURN_CONTEXT(this)
  *argctx = std::move(*argctx).resume();
  TC_SANITIZER_CLOSE_SWITCH_CONTEXT()

  work_for_thread_context = nullptr;

  assert(pack.second.is_ready());
  return pack.second.get();
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
struct runner
{
  template <typename P, typename F>
  static auto run(P&& p, F&& cb)
  {
    p.set_value(cb().get());
  }
};

template <>
struct runner<void>
{
  template <typename P, typename F>
  static auto run(P&& p, F&& cb)
  {
    cb();
    p.set_value();
  }
};
}

namespace lazy
{
namespace detail
{
template <typename R, template <typename...> class Tuple>
struct value_types_of
{
  using types = Tuple<R>;
};

template <template <typename...> class Tuple>
struct value_types_of<void, Tuple>
{
  using types = Tuple<>;
};

template <typename E, typename F>
struct run_resumable_sender
{
  using return_task_type = std::decay_t<decltype(std::declval<F>()())>;
  using return_type =
      typename tconcurrent::detail::task_return_type<return_task_type>::type;

  template <template <typename...> class Tuple>
  using value_types = typename value_types_of<return_type, Tuple>::types;

  E executor;
  F cb;
  std::string name;

  template <typename R>
  void submit(R&& r)
  {
    tconcurrent::detail::coroutine_control* cs =
        new tconcurrent::detail::coroutine_control(
            name,
            executor,
            [cb = std::move(cb), &cs, r = std::forward<R>(r)](
                tconcurrent::detail::coroutine_t&& argctx) mutable {
              TC_SANITIZER_ENTER_NEW_CONTEXT();

              // we jump to the coroutine and back a first time before the user
              // code is run to get mycs here, and initialize argctx and token
              // here, and ctx outside
              auto mycs = cs;
              mycs->argctx = &argctx;
              mycs->token = r.get_cancelation_token();

              TC_SANITIZER_OPEN_RETURN_CONTEXT(mycs);
              *mycs->argctx = std::move(*mycs->argctx).resume();
              TC_SANITIZER_CLOSE_SWITCH_CONTEXT();

              try
              {
                tconcurrent::detail::runner<return_type>::run(r, std::move(cb));
              }
              catch (tconcurrent::detail::abort_coroutine)
              {
                mycs->aborted = true;
                r.set_done();
              }
              catch (...)
              {
                r.set_error(std::current_exception());
              }

              TC_SANITIZER_EXIT_CONTEXT(mycs)
              return std::move(argctx);
            });

    {
      TC_SANITIZER_OPEN_SWITCH_CONTEXT(
          reinterpret_cast<char const*>(cs->stack.sp) - cs->stack.size,
          cs->stack.size)
      cs->ctx = std::move(cs->ctx).resume();
      TC_SANITIZER_CLOSE_SWITCH_CONTEXT()
    }

    tconcurrent::detail::run_coroutine(cs);
  };
};
}

template <typename E, typename F, typename... Args>
auto run_resumable(E&& executor, std::string name, F&& cb, Args&&... args)
{
  auto wrap = [cb = std::forward<F>(cb),
               args = std::make_tuple(std::forward<Args>(args)...)]() mutable
      -> decltype(auto) { return std::apply(std::move(cb), std::move(args)); };
  return detail::run_resumable_sender<std::decay_t<E>, decltype(wrap)>{
      std::forward<E>(executor), std::move(wrap), std::move(name)};
}

template <typename E, typename F>
auto async_resumable(E&& executor, std::string const& name, F&& cb)
{
  auto fullName = name + " (" + typeid(F).name() + ")";

  return lazy::connect(
      lazy::async(executor, fullName),
      lazy::run_resumable(executor, std::move(fullName), std::forward<F>(cb)));
}
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

template <typename F>
auto dispatch_on_thread_context(F&& f)
{
  auto const ctrl = detail::get_current_coroutine_ptr();
  if (ctrl)
  {
    if constexpr (std::is_same_v<decltype(f()), void>)
    {
      ctrl->dispatch_on_thread_context(std::forward<F>(f));
      return;
    }
    else
    {
      return ctrl->dispatch_on_thread_context(std::forward<F>(f));
    }
  }
  else
  {
    return f();
  }
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
