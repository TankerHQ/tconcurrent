#include <catch.hpp>

#include <tconcurrent/curl/curl.hpp>
#include <tconcurrent/promise.hpp>

using namespace tconcurrent;
using namespace tconcurrent::curl;

TEST_CASE("curl simple request")
{
  multi mul;

  promise<void> finished;
  long httpcode = 0;
  long expectedhttpcode = 0;
  bool dataread = false;
  request req;
  req.set_read_callback([&](request& r, void const*, std::size_t size) {
    curl_easy_getinfo(r.get_curl(), CURLINFO_RESPONSE_CODE, &httpcode);
    dataread = true;
    return size;
  });
  req.set_finish_callback(
      [=](request&, CURLcode) mutable { finished.set_value(0); });

  SECTION("http")
  {
    expectedhttpcode = 200;
    req.set_url("http://httpbin.org/get?TEST=test");
  }

  SECTION("https")
  {
    expectedhttpcode = 200;
    req.set_url("https://httpbin.org/get?TEST=test");
  }

  SECTION("not found")
  {
    expectedhttpcode = 404;
    req.set_url("http://httpbin.org/whatever");
  }

  SECTION("post")
  {
    expectedhttpcode = 200;
    req.set_url("http://httpbin.org/post");
    static char const buf[] = "Test test test";
    curl_easy_setopt(req.get_curl(), CURLOPT_POST, 1l);
    curl_easy_setopt(
        req.get_curl(), CURLOPT_POSTFIELDSIZE, long(sizeof(buf) - 1));
    curl_easy_setopt(req.get_curl(), CURLOPT_POSTFIELDS, buf);
  }

  mul.process(&req);
  finished.get_future().wait();
  CHECK(dataread);
  CHECK(expectedhttpcode == httpcode);
}

TEST_CASE("curl multiple requests")
{
  static auto const NB = 5;

  multi mul;

  promise<void> finished[NB];
  bool dataread[NB] = {false};
  request req[NB];
  for (int i = 0; i < NB; ++i)
  {
    req[i].set_read_callback(
        [&dataread, i](request&, void const*, std::size_t size) {
          dataread[i] = true;
          return size;
        });
    promise<void> finish = finished[i];
    req[i].set_finish_callback(
        [finish](request&, CURLcode) mutable { finish.set_value(0); });
    req[i].set_url("http://httpbin.org/drip?numbytes=100&duration=1");
  }

  for (int i = 0; i < NB; ++i)
    mul.process(&req[i]);

  for (int i = 0; i < NB; ++i)
  {
    finished[i].get_future().wait();
    CHECK(dataread[i]);
  }
}

TEST_CASE("curl read_all")
{
  multi mul;
  auto req = std::make_shared<request>();

  SECTION("simple")
  {
    req->set_url("http://httpbin.org/get?TEST=test");
  }

  SECTION("post continue")
  {
    req->set_url("http://httpbin.org/post");
    static char const buf[] = "Test test test";
    curl_easy_setopt(req->get_curl(), CURLOPT_POST, 1l);
    curl_easy_setopt(
        req->get_curl(), CURLOPT_POSTFIELDSIZE, long(sizeof(buf) - 1));
    curl_easy_setopt(req->get_curl(), CURLOPT_POSTFIELDS, buf);
    req->add_header("Expect: 100-continue");
  }

  auto fut = read_all(mul, req);

  auto& result = fut.get();
  long httpcode;
  curl_easy_getinfo(req->get_curl(), CURLINFO_RESPONSE_CODE, &httpcode);

  CHECK(200 == httpcode);
  CHECK(!result.header.empty());
  CHECK(!result.data.empty());
}
