#ifndef TCONCURRENT_LAZY_SYNC_WAIT_HPP
#define TCONCURRENT_LAZY_SYNC_WAIT_HPP

#include <tconcurrent/detail/tvoid.hpp>
#include <tconcurrent/lazy/cancelation_token.hpp>
#include <tconcurrent/lazy/detail.hpp>
#include <tconcurrent/operation_canceled.hpp>

#include <boost/variant2/variant.hpp>

#include <condition_variable>
#include <mutex>

namespace tconcurrent
{
namespace lazy
{
namespace detail
{
template <typename T>
struct sync_state
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

  std::mutex mtx;
  std::condition_variable cv;
  boost::variant2::variant<v_none, v_exception, v_value> data;
};

template <class T>
struct sync_receiver_base
{
  sync_state<T>* _state;
  cancelation_token* _cancelation_token;
  auto get_cancelation_token()
  {
    return _cancelation_token;
  }
  template <typename V>
  void _set(V&& v)
  {
    _cancelation_token->reset();
    {
      auto lk = std::unique_lock<std::mutex>{_state->mtx};
      _state->data.template emplace<std::decay_t<V>>(std::forward<V>(v));
    }
    _state->cv.notify_one();
  }
  template <typename E>
  void set_error(E&& e)
  {
    _set(typename sync_state<T>::v_exception{std::forward<E>(e)});
  }
  void set_done()
  {
    _set(typename sync_state<T>::v_exception{
        std::make_exception_ptr(operation_canceled())});
  }
};

template <class T>
struct sync_receiver : sync_receiver_base<T>
{
  sync_receiver(sync_state<T>* state, cancelation_token* ctoken)
    : sync_receiver_base<T>{state, ctoken}
  {
  }

  void set_value(T&& v)
  {
    this->_set(typename sync_state<T>::v_value{std::forward<T>(v)});
  }
};

template <>
struct sync_receiver<void> : sync_receiver_base<tvoid>
{
  sync_receiver(sync_state<tvoid>* state, cancelation_token* ctoken)
    : sync_receiver_base<tvoid>{state, ctoken}
  {
  }

  void set_value()
  {
    this->_set(sync_state<tvoid>::v_value{{}});
  }
};

template <class T, class Sender>
struct do_sync_wait
{
  static T sync_wait(Sender sender, cancelation_token& c)
  {
    using state_t = detail::sync_state<T>;
    state_t state;

    sender.submit(detail::sync_receiver<T>{&state, &c});

    {
      auto lk = std::unique_lock<std::mutex>{state.mtx};
      state.cv.wait(lk, [&state] {
        return !boost::variant2::holds_alternative<typename state_t::v_none>(
            state.data);
      });
    }

    if (auto const exc =
            boost::variant2::get_if<typename state_t::v_exception>(&state.data))
      std::rethrow_exception(exc->exc);
    return std::move(
        boost::variant2::get<typename state_t::v_value>(state.data).value);
  }
};

template <class Sender>
struct do_sync_wait<void, Sender>
{
  static void sync_wait(Sender sender, cancelation_token& c)
  {
    using state_t = detail::sync_state<tvoid>;
    state_t state;

    sender.submit(detail::sync_receiver<void>{&state, &c});

    {
      auto lk = std::unique_lock<std::mutex>{state.mtx};
      state.cv.wait(lk, [&state] {
        return !boost::variant2::holds_alternative<typename state_t::v_none>(
            state.data);
      });
    }

    if (auto const exc =
            boost::variant2::get_if<typename state_t::v_exception>(&state.data))
      std::rethrow_exception(exc->exc);
  }
};
}

/** Run \p sender to its completion and return the result.
 */
template <class Sender>
auto sync_wait(Sender&& sender, cancelation_token& c)
{
  using return_type = detail::extract_single_value_type_t<std::decay_t<Sender>>;
  return detail::do_sync_wait<return_type, std::decay_t<Sender>>::sync_wait(
      std::forward<Sender>(sender), c);
}
}
}

#endif
