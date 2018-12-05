#ifndef TCONCURRENT_WHEN_HPP
#define TCONCURRENT_WHEN_HPP

#include <atomic>

#include <flags/flags.hpp>

#include <tconcurrent/promise.hpp>

namespace tconcurrent
{
enum class when_any_options
{
  none = 0,
  auto_cancel = 1 << 0,
};
}

ALLOW_FLAGS_FOR_ENUM(tconcurrent::when_any_options)

namespace tconcurrent
{
namespace detail
{

template <typename T>
struct is_future : std::false_type
{
};

template <typename T>
struct is_future<future<T>> : std::true_type
{
};

template <typename T>
struct is_future<shared_future<T>> : std::true_type
{
};

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

    shared(std::vector<F>& futures) : count(0), total(futures.size())
    {
      finished_futures.resize(futures.size());

      cancelers.reserve(futures.size());
      std::transform(futures.begin(),
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

  for (unsigned int index = 0; index < futlist.size(); ++index)
    futlist[index].then(
        get_synchronous_executor(),
        [index, cb](value_type f) mutable { cb(index, std::move(f)); });

  return cb.get_future();
}

template <class Sequence>
struct when_any_result
{
  std::size_t index;
  Sequence futures;
};

namespace detail
{
template <typename F>
class when_any_callback
{
public:
  using result_type = when_any_result<std::vector<F>>;

  when_any_callback(std::vector<F> futures, when_any_options options)
    : _p(std::make_shared<shared>(std::move(futures))), _options(options)
  {
    assert(!_p->futures.empty());

    _p->self_canceler = _p->prom.get_cancelation_token().make_scope_canceler(
        [p = _p] { p->request_cancel(); });

    for (unsigned int index = 0; index < _p->futures.size(); ++index)
      _p->futures[index].then(
          get_synchronous_executor(),
          [self = *this, index](auto&& f) mutable { self(index); });
  }

  void operator()(unsigned int index)
  {
    if (!_p->triggered.exchange(true))
    {
      if (_options & when_any_options::auto_cancel)
        for (unsigned int i = 0; i < _p->futures.size(); ++i)
          if (i != index)
            _p->futures[i].request_cancel();
      _p->prom.set_value(result_type{index, std::move(_p->futures)});
    }
  };

  future<result_type> get_future()
  {
    return _p->prom.get_future();
  }

private:
  struct shared
  {
    std::vector<F> futures;
    std::vector<std::function<void()>> future_cancelers;
    std::atomic<bool> triggered{false};
    promise<result_type> prom;
    cancelation_token::scope_canceler self_canceler;

    shared(std::vector<F> afutures) : futures(std::move(afutures))
    {
      future_cancelers.reserve(futures.size());
      std::transform(futures.begin(),
                     futures.end(),
                     std::back_inserter(future_cancelers),
                     [&](F& future) { return future.make_canceler(); });
    }

    void request_cancel()
    {
      for (auto const& canceler : future_cancelers)
        canceler();
    }
  };

  std::shared_ptr<shared> _p;
  when_any_options _options;
};
}

/** Get a future that will be ready when any one of the given futures is ready
 *
 * If a cancelation is requested on the returned future, the cancelation request
 * is propagated to the futures given as argument.
 *
 * If the input range is empty, returns a ready future with an index of
 * size_t(-1).
 *
 * \return a future<when_any_result<std::vector<future<T>>>> that always
 * finishes with a value.
 */
template <typename InputIterator>
future<when_any_result<
    std::vector<typename std::iterator_traits<InputIterator>::value_type>>>
when_any(InputIterator first,
         InputIterator last,
         when_any_options options = when_any_options::none)
{
  using value_type = typename std::iterator_traits<InputIterator>::value_type;

  static_assert(detail::is_future<value_type>::value,
                "when_any must be called on iterators of futures");

  if (first == last)
    return make_ready_future(
        when_any_result<std::vector<value_type>>{size_t(-1), {}});

  std::vector<value_type> futlist;
  futlist.insert(futlist.begin(), first, last);

  detail::when_any_callback<value_type> cb{std::move(futlist), options};

  return cb.get_future();
}
}

#endif
