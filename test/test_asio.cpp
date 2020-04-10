#include <tconcurrent/asio_use_future.hpp>
#include <tconcurrent/async.hpp>
#include <tconcurrent/executor.hpp>

#include <boost/asio/steady_timer.hpp>

#include <doctest.h>

using namespace tconcurrent;

namespace
{
template <typename Signature, typename F, typename CompletionHandler>
auto test_asio(F&& f, CompletionHandler&& handler) ->
    typename boost::asio::async_result<typename std::decay_t<CompletionHandler>,
                                       Signature>::return_type
{
  boost::asio::async_completion<CompletionHandler, Signature> init{handler};
  tc::async(
      [f = std::forward<F>(f),
       completion_handler = std::move(init.completion_handler)]() mutable {
        f(completion_handler);
      });
  return init.result.get();
}
}

TEST_SUITE_BEGIN("asio");

TEST_CASE("void success")
{
  auto fut = test_asio<void()>([](auto&& c) { c(); }, asio::use_future);
  static_assert(std::is_same<decltype(fut), future<void>>::value,
                "asio deduced the wrong type from use_future");
  fut.get();
}

TEST_CASE("void error_code")
{
  auto fut = test_asio<void(boost::system::error_code)>(
      [](auto&& c) {
        c(boost::system::errc::make_error_code(
            boost::system::errc::bad_address));
      },
      asio::use_future);
  static_assert(std::is_same<decltype(fut), future<void>>::value,
                "asio deduced the wrong type from use_future");
  CHECK_THROWS_AS(fut.get(), boost::system::system_error);
}

TEST_CASE("void exception_ptr")
{
  auto fut = test_asio<void(std::exception_ptr)>(
      [](auto&& c) {
        c(std::make_exception_ptr(
            boost::system::system_error(boost::system::errc::make_error_code(
                boost::system::errc::bad_address))));
      },
      asio::use_future);
  static_assert(std::is_same<decltype(fut), future<void>>::value,
                "asio deduced the wrong type from use_future");
  CHECK_THROWS_AS(fut.get(), boost::system::system_error);
}

TEST_CASE("int success")
{
  auto fut = test_asio<void(int)>([](auto&& c) { c(42); }, asio::use_future);
  static_assert(std::is_same<decltype(fut), future<int>>::value,
                "asio deduced the wrong type from use_future");
  CHECK(fut.get() == 42);
}

TEST_CASE("int error_code")
{
  auto fut = test_asio<void(boost::system::error_code, int)>(
      [](auto&& c) {
        c(boost::system::errc::make_error_code(
              boost::system::errc::bad_address),
          0);
      },
      asio::use_future);
  static_assert(std::is_same<decltype(fut), future<int>>::value,
                "asio deduced the wrong type from use_future");
  CHECK_THROWS_AS(fut.get(), boost::system::system_error);
}

TEST_CASE("int exception_ptr")
{
  auto fut = test_asio<void(std::exception_ptr, int)>(
      [](auto&& c) {
        c(std::make_exception_ptr(
              boost::system::system_error(boost::system::errc::make_error_code(
                  boost::system::errc::bad_address))),
          0);
      },
      asio::use_future);
  static_assert(std::is_same<decltype(fut), future<int>>::value,
                "asio deduced the wrong type from use_future");
  CHECK_THROWS_AS(fut.get(), boost::system::system_error);
}

TEST_CASE("steady_timer success")
{
  boost::asio::steady_timer timer(get_default_executor().get_io_service(),
                                  std::chrono::milliseconds(1));
  auto fut = timer.async_wait(asio::use_future);
  static_assert(std::is_same<decltype(fut), future<void>>::value,
                "asio deduced the wrong type from use_future");
  fut.get();
}

TEST_CASE("steady_timer canceled")
{
  boost::asio::steady_timer timer(get_default_executor().get_io_service(),
                                  std::chrono::seconds(10));
  auto fut = timer.async_wait(asio::use_future);
  static_assert(std::is_same<decltype(fut), future<void>>::value,
                "asio deduced the wrong type from use_future");
  timer.cancel();
  CHECK_THROWS_AS(fut.get(), boost::system::system_error);
}

TEST_SUITE_END();
