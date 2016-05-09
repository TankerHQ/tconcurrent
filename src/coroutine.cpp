#include <tconcurrent/coroutine.hpp>

#include <boost/thread.hpp>

namespace tconcurrent
{
namespace detail
{
namespace
{
boost::thread_specific_ptr<detail::coroutine_control*> current;
}

detail::coroutine_control*& get_current_coroutine_ptr()
{
  auto p = current.get();
  if (!p)
    current.reset(p = new detail::coroutine_control*(nullptr));
  return *p;
}

}
}
