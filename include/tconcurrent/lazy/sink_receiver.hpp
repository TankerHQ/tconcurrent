#ifndef TCONCURRENT_LAZY_SINK_RECEIVER_HPP
#define TCONCURRENT_LAZY_SINK_RECEIVER_HPP

#include <exception>

#include <tconcurrent/lazy/cancelation_token.hpp>

namespace tconcurrent
{
namespace lazy
{
/** Receiver that discards its result and aborts on error.
 */
class sink_receiver
{
public:
  sink_receiver() = default;
  sink_receiver(sink_receiver const&) = default;
  sink_receiver(sink_receiver&&) = default;
  sink_receiver& operator=(sink_receiver const&) = default;
  sink_receiver& operator=(sink_receiver&&) = default;

  auto get_cancelation_token()
  {
    return _cancelation_token.get();
  }
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

private:
  std::shared_ptr<cancelation_token> _cancelation_token =
      std::make_shared<cancelation_token>();
};
}
}

#endif
