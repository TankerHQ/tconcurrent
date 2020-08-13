#include <tconcurrent/stackful_coroutine.hpp>

#include <iostream>

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

#if TCONCURRENT_SANITIZER
namespace
{
thread_local void* thread_stack;
thread_local size_t thread_stacksize;
}

#ifdef __APPLE__
stack_bounds get_stack_bounds(coroutine_control* ctrl)
{
  if (ctrl->previous_coroutine)
  {
    return {reinterpret_cast<char const*>(ctrl->previous_coroutine->stack.sp) -
                ctrl->previous_coroutine->stack.size,
            ctrl->stack.size};
  }
  else
  {
    if (!thread_stack)
    {
      pthread_t self = pthread_self();
      thread_stack = pthread_get_stackaddr_np(self);
      thread_stacksize = pthread_get_stacksize_np(self);
    }
    return {thread_stack, thread_stacksize};
  }
}
#elif __linux__
stack_bounds get_stack_bounds(coroutine_control* ctrl)
{
  if (ctrl->previous_coroutine)
  {
    return {reinterpret_cast<char const*>(ctrl->previous_coroutine->stack.sp) -
                ctrl->previous_coroutine->stack.size,
            ctrl->stack.size};
  }
  else
  {
    if (!thread_stack)
    {
      pthread_attr_t attr;
      pthread_attr_init(&attr);
      if (pthread_getattr_np(pthread_self(), &attr) != 0)
        std::cerr << "ERROR: failed to get main thread stack, can't annotate "
                     "stack switch, sanitizer false positives will follow"
                  << std::endl;
      pthread_attr_getstack(&attr, &thread_stack, &thread_stacksize);
      pthread_attr_destroy(&attr);
    }
    return {thread_stack, thread_stacksize};
  }
}
#endif
#endif

void assert_not_in_catch()
{
  if (std::uncaught_exception() || std::current_exception())
  {
    std::cerr << "Fatal error: it is not possible to switch coroutine (with "
                 "TC_AWAIT, or canceling a coroutine) while the stack is being "
                 "unwound (i.e. in a destructor) or in a catch clause."
              << std::endl;
    std::terminate();
  }
}
}
}
