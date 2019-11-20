#ifndef TCONCURRENT_STACKLESS_COROUTINE_HPP
#define TCONCURRENT_STACKLESS_COROUTINE_HPP

#include <tconcurrent/async.hpp>
#include <tconcurrent/future.hpp>
#include <tconcurrent/lazy/async.hpp>
#include <tconcurrent/lazy/then.hpp>
#include <tconcurrent/promise.hpp>

#include <experimental/coroutine>

namespace tconcurrent
{
template <typename E, typename F>
auto async_resumable(std::string const& name, E&& executor, F&& cb);

template <typename T>
class cotask;

namespace detail
{
void assert_no_cancel_in_catch();
void assert_no_co_await_in_catch();

struct task_promise_base
{
  struct final_awaitable
  {
    bool await_ready() const noexcept
    {
      return false;
    }

    template <typename P>
    void await_suspend(std::experimental::coroutine_handle<P> coroutine)
    {
      task_promise_base& promise = coroutine.promise();
      if (!promise.cont)
        return;
      // continuation may delete the promise, be careful
      auto cont = std::move(promise.cont);
      cont();
    }

    void await_resume() noexcept
    {
    }
  };

  task_promise_base() = default;
  task_promise_base(task_promise_base const&) = delete;
  task_promise_base(task_promise_base&&) = delete;
  task_promise_base& operator=(task_promise_base const&) = delete;
  task_promise_base& operator=(task_promise_base&&) = delete;

  auto initial_suspend()
  {
    // suspend always so that we don't start the task until it's awaited
    return std::experimental::suspend_always{};
  }
  auto final_suspend()
  {
    // suspend always so that we can get the result of the coroutine when it is
    // done
    return final_awaitable{};
  }
  void unhandled_exception()
  {
    exc = std::current_exception();
  }
  void rethrow_if_needed()
  {
    if (exc)
      std::rethrow_exception(std::move(exc));
  }
  std::function<void()> cont;
  std::exception_ptr exc;
  executor executor;
};

template <typename T>
struct task_promise : task_promise_base
{
  ~task_promise()
  {
    if (constructed)
      get_val()->~T();
  }

  template <typename U>
  void return_value(U&& u)
  {
    new (&val) T(std::forward<U>(u));
    constructed = true;
  }
  T&& result()
  {
    rethrow_if_needed();
    return std::move(*get_val());
  }

  T* get_val()
  {
    return reinterpret_cast<T*>(&val);
  }

  cotask<T> get_return_object();
  bool constructed = false;
  typename std::aligned_storage<sizeof(T), alignof(T)>::type val;
};

template <typename T>
struct task_promise<T&> : task_promise_base
{
  void return_value(T& u)
  {
    val = &u;
  }
  T& result()
  {
    rethrow_if_needed();
    return *val;
  }

  cotask<T&> get_return_object();

  T* val;
};

template <>
struct task_promise<void> : task_promise_base
{
  void return_void()
  {
  }
  void result()
  {
    rethrow_if_needed();
  }

  cotask<void> get_return_object();
};
}

template <typename T>
class [[nodiscard]] cotask
{
public:
  using value_type = T;

  using promise_type = detail::task_promise<T>;

  cotask(cotask const&) = delete;
  cotask& operator=(cotask const&) = delete;

  cotask(cotask && o) : coro(o.coro), started(o.started)
  {
    o.coro = nullptr;
  }
  cotask& operator=(cotask&&) = delete;

  ~cotask()
  {
    if (coro)
    {
      if (started && !coro.done())
        assert(!"we are destroying a cotask that is not finished!");
      coro.destroy();
    }
  }

  auto operator co_await()&&
  {
    return typename cotask<T>::awaitable{coro};
  }

private:
  struct awaitable
  {
    std::experimental::coroutine_handle<promise_type> coroutine;

    bool await_ready()
    {
      assert(!coroutine.done() &&
             "trying to await a coroutine that has already run");
      return coroutine.done();
    }
    decltype(auto) await_resume()
    {
      return coroutine.promise().result();
    }
    template <typename P>
    bool await_suspend(std::experimental::coroutine_handle<P> caller_awaiter)
    {
      coroutine.promise().executor = caller_awaiter.promise().executor;
      coroutine.resume();
      if (coroutine.done())
        return false;
      coroutine.promise().cont = [caller_awaiter]() mutable {
        caller_awaiter.resume();
      };
      return true;
    }
  };

  std::experimental::coroutine_handle<promise_type> coro;
  bool started = false;

  cotask(std::experimental::coroutine_handle<promise_type> coroutine)
    : coro(coroutine)
  {
  }

  void run()
  {
    started = true;
    coro.resume();
  }

  template <typename E>
  void set_executor(E && executor)
  {
    coro.promise().executor = std::forward<E>(executor);
  }

  template <typename F>
  void set_continuation(F && cont)
  {
    coro.promise().cont = std::forward<F>(cont);
  }

  void cancel()
  {
#ifndef TCONCURRENT_ALLOW_CANCEL_IN_CATCH
    detail::assert_no_cancel_in_catch();
#endif

    auto const executor = coro.promise().executor;
    assert((!executor || executor.is_in_this_context()) &&
           "cancelation is not supported cross-executor");

    auto cont = coro.promise().cont;
    coro.destroy();
    coro = nullptr;
    cont();
  }

  T get()
  {
    if (!coro)
      throw operation_canceled{};
    return coro.promise().result();
  }

  template <typename E, typename F>
  friend auto async_resumable(std::string const& name, E&& executor, F&& cb);
  friend detail::task_promise<T>;
};

namespace detail
{
template <typename T>
cotask<T> task_promise<T>::get_return_object()
{
  return cotask<T>{
      std::experimental::coroutine_handle<task_promise<T>>::from_promise(
          *this)};
}

template <typename T>
cotask<T&> task_promise<T&>::get_return_object()
{
  return cotask<T&>{
      std::experimental::coroutine_handle<task_promise<T&>>::from_promise(
          *this)};
}

inline cotask<void> task_promise<void>::get_return_object()
{
  return cotask<void>{
      std::experimental::coroutine_handle<task_promise<void>>::from_promise(
          *this)};
}

template <template <typename> class F, typename R>
struct future_awaiter
{
  F<R>&& input;
  F<R> output;
  std::shared_ptr<bool> dead = std::make_shared<bool>(false);

  future_awaiter(F<R>&& o) : input(std::move(o))
  {
  }

  future_awaiter(future_awaiter&& o) = default;

  future_awaiter(future_awaiter const&) = delete;
  future_awaiter& operator=(future_awaiter&&) = delete;
  future_awaiter& operator=(future_awaiter const&) = delete;

  ~future_awaiter()
  {
    if (dead)
    {
      *dead = true;
      // we need this check, but why?
      if (input.is_valid())
        input.request_cancel();
    }
  }

  bool await_ready()
  {
    detail::assert_no_co_await_in_catch();

    if (input.is_ready())
    {
      output = std::move(input);
      return true;
    }
    return false;
  }
  auto await_resume()
  {
    return output.get();
  }
  template <typename P>
  void await_suspend(std::experimental::coroutine_handle<P> coro)
  {
    input.then(coro.promise().executor,
               [this, dead = this->dead, coro](auto result_future) mutable {
                 // make it atomic? reordering must not occur
                 if (*dead)
                   return;
                 this->output = std::move(result_future);
                 coro.resume();
               });
  }
};
}
}

template <typename R>
auto operator co_await(tc::future<R>&& f)
{
  return tc::detail::future_awaiter<tc::future, R>{
      static_cast<tc::future<R>&&>(f)};
}

template <typename R>
auto operator co_await(tc::shared_future<R>&& f)
{
  return tc::detail::future_awaiter<tc::shared_future, R>{
      static_cast<tc::shared_future<R>&&>(f)};
}

namespace tconcurrent
{
namespace lazy
{
namespace detail
{
struct sink_task;

struct sink_promise
{
  executor executor;

  sink_promise() = default;
  sink_promise(sink_promise const&) = delete;
  sink_promise(sink_promise&&) = delete;
  sink_promise& operator=(sink_promise const&) = delete;
  sink_promise& operator=(sink_promise&&) = delete;

  auto initial_suspend()
  {
    return std::experimental::suspend_always{};
  }
  auto final_suspend()
  {
    return std::experimental::suspend_never{};
  }
  void unhandled_exception()
  {
    std::terminate();
  }

  void return_void()
  {
  }

  sink_task get_return_object();
};

struct sink_task
{
public:
  using promise_type = sink_promise;

  std::experimental::coroutine_handle<promise_type> coro;
};

inline sink_task sink_promise::get_return_object()
{
  return sink_task{
      std::experimental::coroutine_handle<sink_promise>::from_promise(*this)};
}

template <typename R>
struct coro_runner
{
  template <typename Coro, typename Awaitable>
  static sink_task run(Coro& coro, Awaitable awaitable)
  {
    try
    {
      coro.receiver.set_value(co_await std::move(awaitable));
    }
    catch (...)
    {
      coro.receiver.set_error(std::current_exception());
    }
    coro.keep_alive = nullptr;
  }
};

template <>
struct coro_runner<void>
{
  template <typename Coro, typename Awaitable>
  static sink_task run(Coro& coro, Awaitable awaitable)
  {
    try
    {
      co_await std::move(awaitable);
      coro.receiver.set_value();
    }
    catch (...)
    {
      coro.receiver.set_error(std::current_exception());
    }
    coro.keep_alive = nullptr;
  }
};

template <typename Receiver>
struct coroutine_holder
{
  Receiver receiver;

  std::experimental::coroutine_handle<sink_promise> coro;
  std::shared_ptr<coroutine_holder> keep_alive;

  template <typename ReceiverArg>
  coroutine_holder(ReceiverArg&& receiver)
    : receiver(std::forward<ReceiverArg>(receiver))
  {
  }

  void cancel()
  {
#ifndef TCONCURRENT_ALLOW_CANCEL_IN_CATCH
    ::tconcurrent::detail::assert_no_cancel_in_catch();
#endif

    if (!coro)
      return;

    auto const executor = coro.promise().executor;
    assert((!executor || executor.is_in_this_context()) &&
           "cancelation is not supported cross-executor");

    if (coro.done())
      return;

    coro.destroy();
    coro = nullptr;
    receiver.set_done();
    keep_alive = nullptr;
  }
};
}

template <typename E, typename Awaitable>
auto run_resumable(E&& executor, Awaitable&& awaitable)
{
  using return_type = typename Awaitable::value_type;

  return [executor = std::forward<E>(executor),
          awaitable =
              std::forward<Awaitable>(awaitable)](auto&& receiver) mutable {
    auto coro = std::make_shared<
        detail::coroutine_holder<std::decay_t<decltype(receiver)>>>(
        std::forward<decltype(receiver)>(receiver));
    coro->keep_alive = coro;
    coro->coro =
        detail::coro_runner<return_type>::run(*coro, std::move(awaitable)).coro;
    coro->coro.promise().executor = std::move(executor);
    coro->coro.resume();
    // we just ran the coroutine, it may have died right away, so we need to
    // check
    if (coro->keep_alive)
      coro->receiver.get_cancelation_token()->set_canceler(
          [coro] { coro->cancel(); });
  };
}
}

template <typename E, typename F>
auto async_resumable(std::string const& name, E&& executor, F&& cb)
    -> future<typename std::decay_t<decltype(cb())>::value_type>
{
  using return_task_type = std::decay_t<decltype(cb())>;
  using return_type = typename return_task_type::value_type;

  auto task = lazy::async_then(
      lazy::async(executor),
      lazy::run_resumable(executor,
                          [](std::decay_t<F> cb) -> cotask<return_type> {
                            co_return co_await cb();
                          }(std::forward<F>(cb))));
  return submit_to_future<return_type>(std::move(task));
}

namespace detail
{
struct yielder
{
  std::shared_ptr<bool> dead = std::make_shared<bool>(false);

  yielder(yielder&&) = delete;
  yielder(yielder const&) = delete;
  yielder& operator=(yielder&&) = delete;
  yielder& operator=(yielder const&) = delete;

  ~yielder()
  {
    *dead = true;
  }
  bool await_ready() const noexcept
  {
    return false;
  }

  template <typename P>
  void await_suspend(std::experimental::coroutine_handle<P> coroutine)
  {
    async(coroutine.promise().executor,
          [coroutine, dead = this->dead]() mutable {
            if (*dead)
              return;
            coroutine.resume();
          });
  }

  void await_resume() noexcept
  {
  }
};
}
}

#define TC_AWAIT(future) (co_await future)
#define TC_YIELD() (co_await ::tconcurrent::detail::yielder{})
#define TC_RETURN(value) co_return value

#endif
