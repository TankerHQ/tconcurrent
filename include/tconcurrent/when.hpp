#ifndef TCONCURRENT_WHEN_HPP
#define TCONCURRENT_WHEN_HPP

#include <atomic>
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
  when_all_callback(std::vector<F>& futures)
    : _p(std::make_shared<shared>(futures))
  {
    assert(!futures.empty());

    _p->canceler = _p->prom.get_cancelation_token().make_scope_canceler(
        [p = _p] { p->request_cancel(); });
  }

  void operator()(unsigned int index, F future)
  {
    assert(_p->count < _p->total);

    _p->finished_futures[index] = std::move(future);
    if (++_p->count == _p->total)
    {
      _p->canceler = {};
      _p->prom.set_value(std::move(_p->finished_futures));
    }
  };

  future<std::vector<F>> get_future()
  {
    return _p->prom.get_future();
  }

private:
  struct shared
  {
    std::vector<F> finished_futures;
    std::vector<std::function<void()>> cancelers;
    std::atomic<unsigned int> count;
    unsigned int const total;
    promise<std::vector<F>> prom;
    cancelation_token::scope_canceler canceler;

    shared(std::vector<F>& futures)
      : count(0), total(futures.size())
    {
      finished_futures.resize(futures.size());

      cancelers.reserve(futures.size());
      std::transform(
          futures.begin(),
          futures.end(),
          std::back_inserter(cancelers),
          [&](F& future) { return future.make_canceler(); });
    }

    void request_cancel()
    {
      for (auto const& canceler : cancelers)
        canceler();
    }
  };

  std::shared_ptr<shared> _p;
};

}

/** Get a future that will be ready when all the given futures are ready
 *
 * If a cancelation is requested on the returned future, the cancelation request
 * is propagated to the futures given as argument.
 *
 * \return a future<std::vector<future<T>>> that always finishes with a value.
 */
template <typename InputIterator>
future<std::vector<typename std::iterator_traits<InputIterator>::value_type>>
when_all(InputIterator first, InputIterator last)
{
  using value_type = typename std::iterator_traits<InputIterator>::value_type;

  static_assert(detail::is_future<value_type>::value,
                "when_all must be called on iterators of futures");

  if (first == last)
    return make_ready_future(std::vector<value_type>{});

  std::vector<value_type> futlist;
  for (InputIterator it = first; it != last; ++it)
    futlist.push_back(*it);

  detail::when_all_callback<value_type> cb{futlist};

  unsigned int index = 0;
  for (auto& fut : futlist)
  {
    fut.then(get_synchronous_executor(), [index, cb](value_type f) mutable {
      cb(index, std::move(f));
    });
    ++index;
  }

  return cb.get_future();
}
}

#endif
