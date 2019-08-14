#ifndef TCONCURRENT_LAZY_SINK_HPP
#define TCONCURRENT_LAZY_SINK_HPP

#include <exception>

namespace tconcurrent
{
namespace lazy
{
namespace details
{
struct sink_promise
{
  template <typename... V>
  void set_value(V...)
  {
  }
  template <typename E>
  void set_error(E&& e)
  {
    std::terminate();
  }
  void set_done()
  {
  }
};
}
}
}

#endif
