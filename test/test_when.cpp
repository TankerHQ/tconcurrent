#include <doctest.h>

#include <tconcurrent/when.hpp>

using namespace tconcurrent;

TEST_CASE("test when_all")
{
  auto const NB_FUTURES = 100;

  std::vector<promise<void>> promises(NB_FUTURES);
  std::vector<future<void>> futures;
  for (auto const& prom : promises)
    futures.push_back(prom.get_future());

  // set every other future as ready
  for (unsigned int i = 0; i < NB_FUTURES; ++i)
    if (i % 2)
      promises[i].set_value({});

  auto all = when_all(
      std::make_move_iterator(futures.begin()),
      std::make_move_iterator(futures.end()));
  CHECK(!all.is_ready());

  // set all other futures as ready
  for (unsigned int i = 0; i < NB_FUTURES; ++i)
    if (!(i % 2))
      promises[i].set_value({});

  CHECK(all.is_ready());
  auto futs = all.get();
  CHECK(futures.size() == futs.size());
  CHECK(std::all_of(futs.begin(), futs.end(), [](auto const& fut) {
    return fut.is_ready() && fut.has_value();
  }));
}

TEST_CASE("when_all on empty vector should return a ready future")
{
  std::vector<future<int>> futures;
  auto all = when_all(
      std::make_move_iterator(futures.begin()),
      std::make_move_iterator(futures.end()));
  CHECK(all.is_ready());
  auto futs = all.get();
  CHECK(0 == futs.size());
}

TEST_CASE("when_all should propagate cancel")
{
  auto const NB_FUTURES = 100;

  std::vector<promise<void>> promises(NB_FUTURES);
  std::vector<future<void>> futures;
  for (auto const& prom : promises)
    futures.push_back(prom.get_future());

  auto all = when_all(
      std::make_move_iterator(futures.begin()),
      std::make_move_iterator(futures.end()));
  all.request_cancel();

  CHECK(std::all_of(promises.begin(), promises.end(), [](auto& prom) {
    return prom.get_cancelation_token().is_cancel_requested();
  }));
}
