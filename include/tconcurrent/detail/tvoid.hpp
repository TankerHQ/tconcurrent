#ifndef TCONCURRENT_DETAIL_TVOID_HPP
#define TCONCURRENT_DETAIL_TVOID_HPP

#include <type_traits>

namespace tconcurrent
{
struct tvoid
{
};

namespace detail
{
template <typename T>
using void_to_tvoid_t = std::conditional_t<std::is_same_v<T, void>, tvoid, T>;
}
}

#endif
