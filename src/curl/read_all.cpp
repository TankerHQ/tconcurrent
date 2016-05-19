#include <tconcurrent/curl/curl.hpp>

#include <boost/algorithm/string.hpp>

#include <tconcurrent/promise.hpp>

namespace tconcurrent
{
namespace curl
{

namespace
{
struct read_all_helper
{
  multi& _multi;
  std::shared_ptr<request> _req;
  read_all_result::header_type _header;
  read_all_result::data_type _data;
  promise<read_all_result> _promise;

  read_all_helper(multi& multi) : _multi(multi)
  {
  }
};
}

future<read_all_result> read_all(multi& multi, std::shared_ptr<request> req)
{
  auto ra = std::make_shared<read_all_helper>(multi);
  // creates a cycle, but it will be broken when the request finishes
  ra->_req = std::move(req);

  ra->_req->set_header_callback(
      [ra](request&, char const* data, std::size_t size) -> std::size_t {
        // for \r\n
        if (size < 2)
          return 0;

        if (size == 2)
          return size;

        // we may receive multiple response time, for example for code
        // 100 Continue. I don't know if there are other examples of that
        if (size >= 4 && std::strncmp(data, "HTTP", 4) == 0 &&
            std::find(data, data + size, ':') == data + size)
          return size;

        // remove the \r\n
        size -= 2;

        auto const semicolon = std::find(data, data + size, ':');
        // incorrect header? take into account the space after the semi-colon
        if (semicolon + 2 > data + size)
          return 0;

        std::string key(data, semicolon);
        boost::algorithm::to_lower(key);
        ra->_header[std::move(key)] = std::string(semicolon + 2, data + size);

        return size + 2;
      });

  ra->_req->set_read_callback(
      [ra](request&, void const* data, std::size_t size) {
        auto u8data = static_cast<uint8_t const*>(data);
        ra->_data.insert(ra->_data.end(), u8data, u8data + size);
        return size;
      });

  ra->_req->set_finish_callback([ra](request&, CURLcode code) {
    if (code == CURLE_OK)
      ra->_promise.set_value(
          {ra->_req, std::move(ra->_header), std::move(ra->_data)});
    else
      ra->_promise.set_exception(std::make_exception_ptr(std::runtime_error(
          std::string("error occured: ") + curl_easy_strerror(code))));
    // break the cycles
    ra->_promise = {};
    ra->_req = nullptr;
  });

  ra->_promise.get_cancelation_token().set_cancelation_callback([ra] {
    // if query is already finished
    if (!ra->_req)
      return;

    ra->_multi.cancel(ra->_req.get());
    ra->_promise.set_exception(std::make_exception_ptr(operation_canceled{}));
    ra->_promise = {};
    ra->_req = nullptr;
  });

  multi.process(ra->_req.get());

  return ra->_promise.get_future();
}

}
}
