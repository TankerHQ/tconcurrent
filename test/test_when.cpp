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

  auto all = when_all(std::make_move_iterator(futures.begin()),
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
  auto all = when_all(std::make_move_iterator(futures.begin()),
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

  auto all = when_all(std::make_move_iterator(futures.begin()),
                      std::make_move_iterator(futures.end()));
  all.request_cancel();

  CHECK(std::all_of(promises.begin(), promises.end(), [](auto& prom) {
    return prom.get_cancelation_token().is_cancel_requested();
  }));
}

TEST_CASE("when_any on empty vector should return a ready future")
{
  std::vector<future<int>> futures;
  auto any = when_any(std::make_move_iterator(futures.begin()),
                      std::make_move_iterator(futures.end()));
  CHECK(any.is_ready());
  auto result = any.get();
  CHECK(size_t(-1) == result.index);
  CHECK(0 == result.futures.size());
}

TEST_CASE("when_any")
{
  auto const NB_FUTURES = 10;

  std::vector<promise<void>> promises(NB_FUTURES);
  std::vector<future<void>> futures;
  for (auto const& prom : promises)
    futures.push_back(prom.get_future());

  SUBCASE("should propagate cancel")
  {
    auto any = when_any(std::make_move_iterator(futures.begin()),
                        std::make_move_iterator(futures.end()));
    any.request_cancel();

    CHECK(std::all_of(promises.begin(), promises.end(), [](auto& prom) {
      return prom.get_cancelation_token().is_cancel_requested();
    }));
  }

  SUBCASE("should get ready when one future is ready")
  {
    auto any = when_any(std::make_move_iterator(futures.begin()),
                        std::make_move_iterator(futures.end()));
    CHECK(!any.is_ready());

    promises[NB_FUTURES / 2].set_value({});

    CHECK(any.is_ready());

    auto result = any.get();
    CHECK(futures.size() == result.futures.size());
    CHECK(result.index == NB_FUTURES / 2);
    // check that only one future is ready
    for (size_t i = 0; i < NB_FUTURES; ++i)
      if (i == NB_FUTURES / 2)
        CHECK(result.futures[i].is_ready());
      else
        CHECK(!result.futures[i].is_ready());
  }

  SUBCASE("should return a ready future if one future is already ready")
  {
    promises[NB_FUTURES / 2].set_value({});

    auto any = when_any(std::make_move_iterator(futures.begin()),
                        std::make_move_iterator(futures.end()));
    CHECK(any.is_ready());
  }

  SUBCASE("should work if multiple futures get ready")
  {
    auto const NB_FUTURES = 10;

    std::vector<promise<void>> promises(NB_FUTURES);
    std::vector<future<void>> futures;
    for (auto const& prom : promises)
      futures.push_back(prom.get_future());

    auto any = when_any(std::make_move_iterator(futures.begin()),
                        std::make_move_iterator(futures.end()));
    CHECK(!any.is_ready());

    promises[1].set_value({});
    promises[2].set_value({});

    CHECK(any.is_ready());
  }

  SUBCASE("should cancel all other futures when a future gets ready")
  {
    auto any = when_any(std::make_move_iterator(futures.begin()),
                        std::make_move_iterator(futures.end()),
                        when_any_options::auto_cancel);

    promises[0].set_value({});

    CHECK(std::all_of(++promises.begin(), promises.end(), [](auto&& p) {
      return p.get_cancelation_token().is_cancel_requested();
    }));
  }
}
