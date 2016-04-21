#include <tconcurrent/coroutine.hpp>

namespace tconcurrent
{
namespace detail
{
namespace
{
thread_local detail::coroutine_control* current;
}

detail::coroutine_control*& get_current_coroutine_ptr()
{
  return current;
}

}
}
