#ifndef TCONCURRENT_LAZY_SYNC_WAIT_HPP
#define TCONCURRENT_LAZY_SYNC_WAIT_HPP

#include <tconcurrent/lazy/cancelation_token.hpp>

#include <mpark/variant.hpp>

#include <condition_variable>
#include <mutex>

namespace tconcurrent
{
namespace lazy
{
namespace details
{
template <typename T>
struct sync_state
{
  std::mutex mtx;
  std::condition_variable cv;
  mpark::variant<mpark::monostate, std::exception_ptr, T> data;
};

template <class T>
struct sync_receiver
{
  sync_state<T>* pst;
  cancelation_token* cancelation_token;
  auto get_cancelation_token()
  {
    return cancelation_token;
  }
  template <int I, typename... V>
  void _set(V... xs)
  {
    cancelation_token->reset();
    auto lk = std::unique_lock<std::mutex>{pst->mtx};
    pst->data.template emplace<I>(std::forward<V>(xs)...);
    pst->cv.notify_one();
  }
  template <typename... V>
  void set_value(V... vs)
  {
    _set<2>(std::forward<V>(vs)...);
  }
  template <typename E>
  void set_error(E&& e)
  {
    _set<1>(std::forward<E>(e));
  }
  void set_done()
  {
    _set<1>(std::make_exception_ptr(operation_canceled()));
  }
};
}

template <class T, class Task>
T sync_wait(Task task, cancelation_token& c)
{
  details::sync_state<T> state;

  task(details::sync_receiver<T>{&state, &c});

  {
    auto lk = std::unique_lock<std::mutex>{state.mtx};
    state.cv.wait(lk, [&state] { return state.data.index() != 0; });
  }

  if (state.data.index() == 1)
    std::rethrow_exception(mpark::get<1>(state.data));
  return std::move(mpark::get<2>(state.data));
}
}
}

#endif
