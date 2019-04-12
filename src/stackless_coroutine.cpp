// We do not include stackless_coroutine.hpp because the file might not compile
// in this context

#include <iostream>

namespace tconcurrent
{
namespace detail
{
void assert_no_cancel_in_catch()
{
  if (std::uncaught_exception() || std::current_exception())
  {
    std::cerr << "Fatal error: it is not possible to switch coroutine (with "
                 "TC_AWAIT) in a catch clause. This will cause undefined "
                 "behavior in the non-coroutines-TS mode. If you don't plan "
                 "on compiling without the coroutines-TS, you can define the "
                 "TCONCURRENT_ALLOW_CANCEL_IN_CATCH macro."
              << std::endl;
    std::terminate();
  }
}

void assert_no_co_await_in_catch()
{
  if (std::uncaught_exception() || std::current_exception())
  {
    std::cerr << "Fatal error: it is not possible to switch coroutine (with "
                 "TC_AWAIT) in a catch clause."
              << std::endl;
    std::terminate();
  }
}
}
}
