#include <tconcurrent/coroutine.hpp>

#ifdef TCONCURRENT_SANITIZER
#include <iostream>
#endif

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

#ifdef TCONCURRENT_SANITIZER
namespace
{
thread_local void* thread_stack;
thread_local size_t thread_stacksize;
}

#ifdef __APPLE__
stack_bounds get_stack_bounds()
{
  if (!thread_stack)
  {
    pthread_t self = pthread_self();
    thread_stack = pthread_get_stackaddr_np(self);
    thread_stacksize = pthread_get_stacksize_np(self);
  }
  return {thread_stack, thread_stacksize};
}
#elif __linux__
stack_bounds get_stack_bounds()
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
#endif
#endif

}
}
