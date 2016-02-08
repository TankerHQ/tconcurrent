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
    auto pack = package<future<void>()>([this]
                                        {
                                          return do_call();
                                        });
    _future = std::get<1>(pack).unwrap();
    // set a dummy cancel function so that stop doesn't fail
    _cancel = []{};
    get_default_executor().post(std::get<0>(pack));
  }
}

future<void> periodic_task::stop()
{
  scope_lock l(_mutex);
  if (_state != State::Running)
    return _future.then([&](future<void> const&){});

  assert(_future.is_valid() && _cancel);

  _state = State::Stopping;
  _cancel();
  return _future.then([&](future<void> const&)
                      {
                        scope_lock l(_mutex);
                        // can be Stopping, or Stopped if the callback threw on
                        // the last run
                        assert(_state != State::Running);
                        _state = State::Stopped;
                      });
}

void periodic_task::reschedule()
{
  assert(!_mutex.try_lock() && "_mutex must be locked to call reschedule");

  assert(_state != State::Stopped);
  if (_state == State::Stopping)
    return;

  auto bundle = async_wait(_period);
  _future = bundle.fut.and_then([this](void*)
                                {
                                  return do_call();
                                }).unwrap();
  _cancel = std::move(bundle.cancel);
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
  catch (std::exception& e)
  {
    // TODO proper error reporting (x3)
    std::cout << "error in periodic_task: " << e.what() << std::endl;
  }
  catch (...)
  {
    std::cout << "unknown error in periodic_task" << std::endl;
  }

  scope_lock l(_mutex);
  if (!success)
  {
    _state = State::Stopped;
    return make_ready_future();
  }

  return fut.then([this](decltype(fut) const& fut)
                  {
                    scope_lock l(_mutex);
                    if (fut.has_value())
                      reschedule();
                    else
                    {
                      std::cout << "error in future of periodic task"
                                << std::endl;
                      _state = State::Stopped;
                    }
                  });
}

}
