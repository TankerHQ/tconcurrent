#ifndef TCONCURRENT_CURL_HPP
#define TCONCURRENT_CURL_HPP

#include <tconcurrent/future.hpp>

#include <map>
#include <functional>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <curl/curl.h>

namespace tconcurrent
{
namespace curl
{

namespace detail
{
struct curl_multi_cleanup_t
{
  void operator()(CURLM* multi)
  {
    curl_multi_cleanup(multi);
  }
};
using CURLM_unique_ptr = std::unique_ptr<CURLM, curl_multi_cleanup_t>;

struct curl_easy_cleanup_t
{
  void operator()(CURL* easy)
  {
    curl_easy_cleanup(easy);
  }
};
using CURL_unique_ptr = std::unique_ptr<CURL, curl_easy_cleanup_t>;

struct curl_slist_cleanup_t
{
  void operator()(struct curl_slist* l)
  {
    curl_slist_free_all(l);
  }
};
using curl_slist_unique_ptr =
    std::unique_ptr<struct curl_slist, curl_slist_cleanup_t>;
}

class request;

class multi
{
public:
  multi();
  multi(boost::asio::io_service& io_service);
  ~multi();

  multi(multi const&) = delete;
  multi(multi&&) = delete;
  multi& operator=(multi const&) = delete;
  multi& operator=(multi&&) = delete;

  void process(request* req);

  CURLM* get_multi()
  {
    return _multi.get();
  }

private:
  struct async_socket
  {
    uint8_t wanted_action = 0;
    uint8_t current_action = 0;
    boost::asio::ip::tcp::socket socket;

    async_socket(boost::asio::io_service& io_service) : socket(io_service)
    {
    }
  };

  using scope_lock = std::lock_guard<std::recursive_mutex>;

  // this mutex must be recursive because when curl calls us back, we don't know
  // if it's because someone called curl or because one of our own callback
  // (whchi already holds the lock) called curl
  std::recursive_mutex _mutex;
  bool _dying = false;
  boost::asio::io_service& _io_service;
  std::map<curl_socket_t, std::unique_ptr<async_socket>> _sockets;
  future<void> _timer_future;
  detail::CURLM_unique_ptr _multi;

  // Check for completed transfers, and remove their easy handles
  void remove_finished();

  // CURLOPT_OPENSOCKETFUNCTION
  curl_socket_t opensocket(curlsocktype purpose, struct curl_sockaddr* address);
  // CURLOPT_CLOSESOCKETFUNCTION
  int close_socket(curl_socket_t item);
  // CURLMOPT_TIMERFUNCTION
  int multi_timer_cb(CURLM* multi, long timeout_ms);
  // CURLMOPT_SOCKETFUNCTION
  int socketfunction_cb(CURL* easy, curl_socket_t s, int action);
  // Called by asio when there is an action on a socket
  void event_cb(async_socket* asocket,
                uint8_t action,
                boost::system::error_code const& ec);
  // Called by asio when our timeout expires
  void timer_cb();

  static curl_socket_t opensocket_c(void* clientp,
                                    curlsocktype purpose,
                                    struct curl_sockaddr* address);
  static int close_socket_c(void* clientp, curl_socket_t item);
  static int socketfunction_cb_c(
      CURL* easy, curl_socket_t s, int action, void* userp, void* socketp);
  static int multi_timer_cb_c(CURLM* multi, long timeout_ms, void* g);

  friend request;
};

class request
{
public:
  using header_callback =
      std::function<std::size_t(request&, char const*, std::size_t)>;
  using read_callback =
      std::function<std::size_t(request&, void const*, std::size_t)>;
  using finish_callback = std::function<void(request&, CURLcode)>;

  request();

  request(request const&) = delete;
  request(request&&) = delete;
  request& operator=(request const&) = delete;
  request& operator=(request&&) = delete;

  void set_header_callback(header_callback cb)
  {
    _header_cb = std::move(cb);
  }
  void set_read_callback(read_callback cb)
  {
    _read_cb = std::move(cb);
  }
  void set_finish_callback(finish_callback cb)
  {
    _finish_cb = std::move(cb);
  }

  CURL* get_curl()
  {
    return _easy.get();
  }

  void set_url(std::string url);
  std::string const& get_url() const
  {
    return _url;
  }

  void add_header(std::string const& header);

private:
  std::string _url;
  detail::curl_slist_unique_ptr _header;

  std::array<char, CURL_ERROR_SIZE> _error;
  header_callback _header_cb;
  read_callback _read_cb;
  finish_callback _finish_cb;
  detail::CURL_unique_ptr _easy;

  size_t header_cb(char* ptr, size_t size, size_t nmemb);
  size_t write_cb(void* ptr, size_t size, size_t nmemb);

  static size_t header_cb_c(char* ptr, size_t size, size_t nmemb, void* data);
  static size_t write_cb_c(void* ptr, size_t size, size_t nmemb, void* data);

  friend multi;
};

using request_ptr = std::shared_ptr<request>;

struct read_all_result
{
  using header_type = std::map<std::string, std::string>;
  using data_type = std::vector<uint8_t>;

  request_ptr req;
  header_type header;
  data_type data;
};

future<read_all_result> read_all(request_ptr req);

inline future<read_all_result> read_all(multi& multi, request_ptr const& req)
{
  auto fut = read_all(req);
  multi.process(&*req);
  return fut;
}

}
}

#endif
