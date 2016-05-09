#ifndef TCONCURRENT_DETAIL_UTIL_HPP
#define TCONCURRENT_DETAIL_UTIL_HPP

#include <memory>

namespace tconcurrent
{
namespace detail
{

/// C++11 standard doesn't provide make_unique
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

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
