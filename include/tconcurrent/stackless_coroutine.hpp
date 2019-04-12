#ifndef TCONCURRENT_STACKLESS_COROUTINE_HPP
#define TCONCURRENT_STACKLESS_COROUTINE_HPP

#include <tconcurrent/async.hpp>
#include <tconcurrent/future.hpp>
#include <tconcurrent/promise.hpp>

#include <experimental/coroutine>

namespace tconcurrent
{
template <typename E, typename F>
auto async_resumable(std::string const& name, E&& executor, F&& cb)
    -> future<typename std::decay_t<decltype(cb())>::value_type>;

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
    // suspend always so that we can get the get of the coroutine when it is
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
class [[nodiscard]] cotask {
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
      {
        assert(!"not done");
      }
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
      assert(!coroutine.done());
      return coroutine.done();
    }
    decltype(auto) await_resume()
    {
      return coroutine.promise().result();
    }
    template <typename R>
    bool await_suspend(
        std::experimental::coroutine_handle<detail::task_promise<R>>
            caller_awaiter)
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
  friend auto async_resumable(std::string const& name, E&& executor, F&& cb)
      ->future<typename std::decay_t<decltype(cb())>::value_type>;
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
namespace detail
{
template <typename F, typename R>
struct task_control
{
  F cb;
  cotask<R> cotask;
  cancelation_token::scope_canceler canceler;

  task_control(F cb)
    : cb(std::move(cb)), cotask([& cb = this->cb] {
      try
      {
        return cb();
      }
      catch (...)
      {
        throw std::runtime_error(std::string(typeid(F).name()) +
                                 " is not a coroutine, got exception");
      }
    }())
  {
  }

  task_control(task_control&&) = default;
};
}

template <typename E, typename F>
auto async_resumable(std::string const& name, E&& executor, F&& cb)
    -> future<typename std::decay_t<decltype(cb())>::value_type>
{
  using return_task_type = std::decay_t<decltype(cb())>;
  using return_type = typename return_task_type::value_type;

  auto const fullName = name + " (" + typeid(F).name() + ")";

  auto token = std::make_shared<cancelation_token>();

  auto ctrl =
      std::make_shared<detail::task_control<std::decay_t<F>, return_type>>(
          std::forward<F>(cb));
  ctrl->cotask.set_executor(executor);

  auto pack = package_cancelable<future<return_type>()>(
      [ctrl = std::move(ctrl), token] {
        auto pack = package<return_type()>(
            [ctrl] { return ctrl->cotask.get(); }, token);
        ctrl->cotask.set_continuation(std::move(std::get<0>(pack)));
        ctrl->cotask.run();
        ctrl->canceler = token->make_scope_canceler(
            [& cotask = ctrl->cotask] { cotask.cancel(); });
        return std::move(std::get<1>(pack));
      },
      token);

  executor.post(std::move(std::get<0>(pack)), fullName);

  return std::move(std::get<1>(pack)).update_chain_name(fullName).unwrap();
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
