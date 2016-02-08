#ifndef TCONCURRENT_WHEN_HPP
#define TCONCURRENT_WHEN_HPP

#include <iostream>
#include <tconcurrent/promise.hpp>

namespace tconcurrent
{
namespace detail
{

template <typename T>
struct is_future : std::false_type
{};

template <typename T>
struct is_future<future<T>> : std::true_type
{};

template <typename F>
class when_all_callback
{
public:
  when_all_callback(std::vector<F> futures)
    : _p(std::make_shared<shared>(std::move(futures)))
  {
    assert(!_p->futurelist.empty());
  }

  void operator()(F const&)
  {
    if (++_p->count == _p->total)
      _p->prom.set_value(std::move(_p->futurelist));
  };

  future<std::vector<F>> get_future()
  {
    return _p->prom.get_future();
  }

private:
  struct shared
  {
    std::vector<F> futurelist;
    std::atomic<unsigned int> count;
    unsigned int const total;
    promise<std::vector<F>> prom;

    shared(std::vector<F> futlist)
      : futurelist(std::move(futlist)), count(0), total(futurelist.size())
    {}
  };

  std::shared_ptr<shared> _p;
};

}

template <typename InputIterator>
future<std::vector<typename InputIterator::value_type>> when_all(
    InputIterator first, InputIterator last)
{
  using value_type = typename InputIterator::value_type;

  static_assert(detail::is_future<value_type>::value,
                "when_all must be called on iterators of futures");

  if (first == last)
    return make_ready_future(std::vector<value_type>{});

  std::vector<value_type> futlist;
  for (InputIterator it = first; it != last; ++it)
    futlist.push_back(*it);

  detail::when_all_callback<value_type> cb{futlist};

  for (auto& fut : futlist)
    fut.then(cb);

  return cb.get_future();
}
}

#endif
