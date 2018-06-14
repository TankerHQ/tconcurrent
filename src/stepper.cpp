#include <tconcurrent/stepper.hpp>

namespace tconcurrent
{
void stepper::operator()(unsigned int step)
{
  {
    scope_lock lock(_mutex);
    _cond.wait(lock, [&] { return step <= _next_step; });
    ++_next_step;
  }
  _cond.notify_all();
}
}
