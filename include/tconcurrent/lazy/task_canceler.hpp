#ifndef TCONCURRENT_LAZY_TASK_CANCELER_HPP
#define TCONCURRENT_LAZY_TASK_CANCELER_HPP

#include <tconcurrent/lazy/cancelation_token.hpp>

#include <mutex>
#include <vector>

namespace tconcurrent
{
namespace lazy
{
class task_canceler
{
public:
  ~task_canceler()
  {
    terminate(true);
  }

  template <typename Sender>
  auto wrap(Sender&& sender)
  {
    return task_canceler_sender<std::decay_t<Sender>>{
        this, std::forward<Sender>(sender)};
  }

  void terminate()
  {
    terminate(false);
  }

private:
  using mutex = std::recursive_mutex;
  using lock_guard = std::lock_guard<mutex>;

  template <typename R>
  struct task_canceler_receiver
  {
    task_canceler* _canceler;
    R _next_receiver;

    auto get_cancelation_token()
    {
      return _next_receiver.get_cancelation_token();
    }
    template <typename... V>
    void set_value(V&&... vals)
    {
      _canceler->complete(get_cancelation_token());
      _next_receiver.set_value(std::forward<V>(vals)...);
    }
    template <typename E>
    void set_error(E&& e)
    {
      _canceler->complete(get_cancelation_token());
      _next_receiver.set_error(std::forward<E>(e));
    }
    void set_done()
    {
      _canceler->complete(get_cancelation_token());
      _next_receiver.set_done();
    }
  };

  template <typename Sender>
  struct task_canceler_sender
  {
    template <template <typename...> class Tuple>
    using value_types = typename Sender::template value_types<Tuple>;

    task_canceler* _tc;
    Sender sender;

    template <typename Receiver>
    void submit(Receiver&& receiver)
    {
      {
        lock_guard _(_tc->_mutex);

        if (_tc->_terminating)
        {
          throw std::runtime_error(
              "submitting a task to a terminating task_canceler");
        }

        _tc->_cancelation_tokens.emplace_back(receiver.get_cancelation_token());
      }

      sender.submit(task_canceler_receiver<std::decay_t<Receiver>>{
          _tc, std::forward<Receiver>(receiver)});
    }
  };

  mutex _mutex;
  std::vector<cancelation_token*> _cancelation_tokens;
  bool _terminating{false};

  void complete(cancelation_token* token)
  {
    lock_guard _(_mutex);
    _cancelation_tokens.erase(
        std::remove(
            _cancelation_tokens.begin(), _cancelation_tokens.end(), token),
        _cancelation_tokens.end());
  }

  void terminate(bool terminating)
  {
    lock_guard _(_mutex);

    if (terminating)
      _terminating = true;
    for (auto const& token : _cancelation_tokens)
      token->request_cancel();
  }
};
}
}

#endif
