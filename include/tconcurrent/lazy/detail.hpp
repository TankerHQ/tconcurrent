#ifndef TCONCURRENT_LAZY_DETAIL_HPP
#define TCONCURRENT_LAZY_DETAIL_HPP

#include <tuple>

namespace tconcurrent
{
namespace lazy
{
namespace detail
{
template <typename Tuple>
struct extract_single_value_type_impl
{
  static_assert(!sizeof(Tuple),
                "A tuple with multiple values is not allowed here");
};

template <typename Value>
struct extract_single_value_type_impl<std::tuple<Value>>
{
  using type = Value;
};

template <>
struct extract_single_value_type_impl<std::tuple<>>
{
  using type = void;
};

template <typename Sender>
using extract_single_value_type = extract_single_value_type_impl<
    typename Sender::template value_types<std::tuple>>;

template <typename Sender>
using extract_single_value_type_t =
    typename extract_single_value_type<Sender>::type;
}
}
}

#endif
