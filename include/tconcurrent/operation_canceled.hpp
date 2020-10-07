#ifndef TCONCURRENT_OPERATION_CANCELED_HPP
#define TCONCURRENT_OPERATION_CANCELED_HPP

#include <stdexcept>

namespace tconcurrent
{
struct operation_canceled : std::exception
{
  const char* what() const noexcept override
  {
    return "operation was canceled";
  }
};
}

#endif
