#include <tconcurrent/barrier.hpp>

#include <cassert>

namespace tconcurrent
{
barrier::barrier(unsigned int target) : _target(target)
{
}

void barrier::operator()()
{
  bool done;
  {
    scope_lock lock(_mutex);
    ++_waiting;
    assert(_waiting <= _target);

    done = _waiting == _target;

    if (!done)
      _cond.wait(lock, [&] { return _waiting == _target; });
  }

  if (done)
    _cond.notify_all();
}
}
