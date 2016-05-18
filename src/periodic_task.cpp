#include <iostream>

#include <tconcurrent/periodic_task.hpp>
#include <tconcurrent/packaged_task.hpp>
#include <tconcurrent/delay.hpp>

namespace tconcurrent
{

periodic_task::~periodic_task()
{
  stop().wait();
}

void periodic_task::start(StartOption opt)
{
  scope_lock l(_mutex);

  if (!_callback)
    throw std::runtime_error("callback must be set before the task is started");

  if (_state == State::Stopping)
    throw std::runtime_error(
        "can't start a periodic task that is not fully stopped");
  if (_state == State::Running)
    return;

  _state = State::Running;

  if (opt == no_option)
    reschedule();
  else
  {
    // post the callback immediately
    auto pack = package<future<void>()>([this] { return do_call(); });
    _future = std::get<1>(pack).unwrap();
    _executor->post(std::get<0>(pack));
  }
}

future<void> periodic_task::stop()
{
  scope_lock l(_mutex);
  if (_state == State::Stopped)
    return make_ready_future();
  if (_state != State::Running)
    return _future.then(get_synchronous_executor(),
                        [&](future<void> const&) {});

  assert(_future.is_valid());

  _state = State::Stopping;
  _future.request_cancel();
  return _future.then(get_synchronous_executor(), [&](future<void> const&) {
    scope_lock l(_mutex);
    // can be Stopping, or Stopped if the callback threw on
    // the last run
    assert(_state != State::Running);
    _state = State::Stopped;
  });
}

void periodic_task::reschedule()
{
  // _mutex must be locked

  assert(_state != State::Stopped);
  if (_state == State::Stopping)
    return;

  _future = async_wait(*_executor, _period)
                .and_then(get_synchronous_executor(),
                          [this](tvoid) { return do_call(); })
                .unwrap()
                .then([](future<void> const& fut) {});
}

future<void> periodic_task::do_call()
{
  auto const cb = [&]
  {
    scope_lock l(_mutex);
    return _callback;
  }();

  bool success = false;
  future<void> fut;
  try
  {
    fut = cb();
    success = true;
  }
  catch (...)
  {
    _executor->signal_error(std::current_exception());
  }

  scope_lock l(_mutex);
  if (!success)
  {
    _state = State::Stopped;
    return make_ready_future();
  }

  return fut.then(get_synchronous_executor(), [this](decltype(fut) const& fut) {
    scope_lock l(_mutex);
    if (fut.has_value())
      reschedule();
    else
      try
      {
        fut.get();
      }
      catch (operation_canceled const&)
      {
      }
      catch (...)
      {
        _executor->signal_error(std::current_exception());
        _state = State::Stopped;
      }
  });
}

}
