#ifndef TCONCURRENT_DETAIL_UTIL_HPP
#define TCONCURRENT_DETAIL_UTIL_HPP

#include <memory>

namespace tconcurrent
{
namespace detail
{

template <typename>
struct result_of_; // not defined

template <typename R, typename... Args>
struct result_of_<R(Args...)>
{
  using type = R;
};

template <typename F>
using result_of_t_ = typename result_of_<F>::type;

}
}

namespace tc = tconcurrent;

#endif
