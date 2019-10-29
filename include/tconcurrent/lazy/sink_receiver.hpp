#ifndef TCONCURRENT_LAZY_SINK_RECEIVER_HPP
#define TCONCURRENT_LAZY_SINK_RECEIVER_HPP

#include <exception>

#include <tconcurrent/lazy/cancelation_token.hpp>

namespace tconcurrent
{
namespace lazy
{
class sink_receiver
{
public:
  sink_receiver() : _cancelation_token(new lazy::cancelation_token)
  {
  }

  auto get_cancelation_token()
  {
    return _cancelation_token;
  }
  template <typename... V>
  void set_value(V...)
  {
    delete _cancelation_token;
  }
  template <typename E>
  void set_error(E&& e)
  {
    std::terminate();
  }
  void set_done()
  {
    delete _cancelation_token;
  }

private:
  cancelation_token* _cancelation_token;
};
}
}

#endif
