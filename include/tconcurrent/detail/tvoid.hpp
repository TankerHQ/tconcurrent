#ifndef TCONCURRENT_DETAIL_TVOID_HPP
#define TCONCURRENT_DETAIL_TVOID_HPP

namespace tconcurrent
{
struct tvoid
{
};

namespace detail
{
template <typename T>
struct void_to_tvoid_
{
  using type = T;
};
template <>
struct void_to_tvoid_<void>
{
  using type = tvoid;
};

template <typename T>
using void_to_tvoid_t = typename void_to_tvoid_<T>::type;
}
}

#endif
