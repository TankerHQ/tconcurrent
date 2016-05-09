#include <tconcurrent/curl/curl.hpp>
#include <tconcurrent/thread_pool.hpp>
#include <tconcurrent/async.hpp>
#include <tconcurrent/delay.hpp>

#include <chrono>
#include <iostream>

namespace
{
constexpr uint8_t ActionNone = 0x0;
constexpr uint8_t ActionRead = 0x1;
constexpr uint8_t ActionWrite = 0x2;
}

namespace tconcurrent
{
namespace curl
{

// C bouncers
curl_socket_t multi::opensocket_c(void* clientp,
                                  curlsocktype purpose,
                                  struct curl_sockaddr* address)
{
  return static_cast<multi*>(clientp)->opensocket(purpose, address);
}

int multi::close_socket_c(void* clientp, curl_socket_t item)
{
  return static_cast<multi*>(clientp)->close_socket(item);
}

int multi::multi_timer_cb_c(CURLM* cmulti, long timeout_ms, void* g)
{
  return static_cast<multi*>(g)->multi_timer_cb(cmulti, timeout_ms);
}

int multi::socketfunction_cb_c(
    CURL* easy, curl_socket_t s, int action, void* userp, void* socketp)
{
  return static_cast<multi*>(userp)->socketfunction_cb(easy, s, action);
}

size_t request::header_cb_c(char* ptr, size_t size, size_t nmemb, void* data)
{
  return static_cast<request*>(data)->header_cb(ptr, size, nmemb);
}

size_t request::write_cb_c(void* ptr, size_t size, size_t nmemb, void* data)
{
  return static_cast<request*>(data)->write_cb(ptr, size, nmemb);
}

// real code

multi::multi()
  : multi(get_default_executor().get_io_service())
{
}

multi::multi(boost::asio::io_service& io_service)
  : _io_service(io_service), _multi(curl_multi_init())
{
  if (!_multi)
    throw std::runtime_error("curl_multi_init() failed");

  curl_multi_setopt(
      _multi.get(), CURLMOPT_SOCKETFUNCTION, &multi::socketfunction_cb_c);
  curl_multi_setopt(_multi.get(), CURLMOPT_SOCKETDATA, this);
  curl_multi_setopt(
      _multi.get(), CURLMOPT_TIMERFUNCTION, &multi::multi_timer_cb_c);
  curl_multi_setopt(_multi.get(), CURLMOPT_TIMERDATA, this);
}

multi::~multi()
{
  future<void> fut;
  std::function<void()> cancel;
  {
    scope_lock l{_mutex};
    _dying = true;
    cancel = _cancel_timer;
    fut = _timer_future;
  }

  if (cancel)
  {
    cancel();
    fut.wait();
  }
}

void multi::process(request* req)
{
  curl_easy_setopt(
      req->_easy.get(), CURLOPT_OPENSOCKETFUNCTION, &multi::opensocket_c);
  curl_easy_setopt(req->_easy.get(), CURLOPT_OPENSOCKETDATA, this);
  curl_easy_setopt(
      req->_easy.get(), CURLOPT_CLOSESOCKETFUNCTION, &multi::close_socket_c);
  curl_easy_setopt(req->_easy.get(), CURLOPT_CLOSESOCKETDATA, this);

  auto rc = curl_multi_add_handle(_multi.get(), req->_easy.get());
  if (CURLM_OK != rc)
    throw std::runtime_error(std::string("curl_multi_add_handle failed: ") +
                             curl_multi_strerror(rc));
}

void multi::remove_finished()
{
  CURLMsg* msg;
  int msgs_left;

  while ((msg = curl_multi_info_read(_multi.get(), &msgs_left)))
  {
    if (msg->msg == CURLMSG_DONE)
    {
      auto easy = msg->easy_handle;

      void* reqptr;
      curl_easy_getinfo(easy, CURLINFO_PRIVATE, &reqptr);
      auto req = static_cast<request*>(reqptr);
      req->_finish_cb(*req, msg->data.result);

      curl_multi_remove_handle(_multi.get(), easy);
    }
  }
}

curl_socket_t multi::opensocket(curlsocktype purpose,
                                struct curl_sockaddr* address)
{
  curl_socket_t sockfd = CURL_SOCKET_BAD;

  // restrict to IPv4
  if (purpose == CURLSOCKTYPE_IPCXN && address->family == AF_INET)
  {
    // create a tcp socket object
    std::unique_ptr<async_socket> asocket{
        new async_socket{_io_service}};

    // open it and get the native handle
    boost::system::error_code ec;
    asocket->socket.open(boost::asio::ip::tcp::v4(), ec);

    if (ec)
    {
      // An error occurred
      // TODO proper error handling
      std::cout << std::endl
                << "Couldn't open socket [" << ec << "][" << ec.message()
                << "]";
    }
    else
    {
      sockfd = asocket->socket.native_handle();
      assert(_sockets.find(sockfd) == _sockets.end());
      _sockets[sockfd] = std::move(asocket);
    }
  }

  return sockfd;
}

int multi::close_socket(curl_socket_t item)
{
  auto cnt = _sockets.erase(item);
  (void)cnt;
  assert(cnt);

  return 0;
}

// Called by asio when there is an action on a socket
void multi::event_cb(async_socket* asocket,
                     uint8_t action,
                     boost::system::error_code const& ec)
{
  if (ec == boost::asio::error::operation_aborted)
    return;

  scope_lock l{_mutex};

  // this action has finished
  asocket->current_action &= ~action;

  // keep it for later in case the socket is deleted
  auto const sockfd = asocket->socket.native_handle();

  int still_running;
  auto const rc = curl_multi_socket_action(
      _multi.get(),
      asocket->socket.native_handle(),
      action == ActionRead ? CURL_CSELECT_IN : CURL_CSELECT_OUT,
      &still_running);

  if (CURLM_OK != rc)
  {
    std::cerr << "curl_multi_socket_action failed: " << curl_multi_strerror(rc)
              << std::endl;
    return;
  }

  // refresh async operations
  socketfunction_cb(nullptr, sockfd, CURL_POLL_NONE);

  remove_finished();

  if (still_running <= 0 && _cancel_timer)
    // last transfer done, kill timeout
    _cancel_timer();
}

// Called by asio when our timeout expires
void multi::timer_cb()
{
  scope_lock l{_mutex};

  int still_running; // don't care
  auto rc = curl_multi_socket_action(
      _multi.get(), CURL_SOCKET_TIMEOUT, 0, &still_running);

  if (CURLM_OK != rc)
  {
    std::cerr << "curl_multi_socket_action failed: "
              << curl_multi_strerror(rc) << std::endl;
    return;
  }
  remove_finished();
}

int multi::multi_timer_cb(CURLM* multi, long timeout_ms)
{
  scope_lock l{_mutex};
  if (_dying)
    return 0;

  // cancel running timer
  if (_cancel_timer)
    _cancel_timer();

  if (timeout_ms > 0)
  {
    // update timer
    auto bundle = async_wait(std::chrono::milliseconds(timeout_ms));
    _timer_future = bundle.fut.and_then(tconcurrent::get_synchronous_executor(),
        [this](void*) { timer_cb(); });
    _cancel_timer = std::move(bundle.cancel);
  }
  else
  {
    // call timeout function immediately
    boost::system::error_code error;
    timer_cb();
  }

  return 0;
}

int multi::socketfunction_cb(CURL*, curl_socket_t s, int curlaction)
{
  auto it = _sockets.find(s);

  if (it == _sockets.end())
  {
    // we don't know this socket, don't care
    return 0;
  }

  auto asocket = it->second.get();

  switch (curlaction)
  {
  case CURL_POLL_NONE:
    break;
  case CURL_POLL_REMOVE:
    asocket->wanted_action = ActionNone;
    break;
  case CURL_POLL_IN:
    asocket->wanted_action = ActionRead;
    break;
  case CURL_POLL_OUT:
    asocket->wanted_action = ActionWrite;
    break;
  case CURL_POLL_INOUT:
    asocket->wanted_action = ActionRead | ActionWrite;
    break;
  }

  if (asocket->current_action == asocket->wanted_action)
    // nothing to do
    return 0;

  // if we want to stop either reading or writing
  if ((asocket->current_action & asocket->wanted_action) !=
      asocket->current_action)
  {
    // cancel all pending asyncs and start over (we can't cancel only read or
    // only write)
    asocket->socket.cancel();
    asocket->current_action = ActionNone;
  }

  auto todo = asocket->wanted_action & ~asocket->current_action;

  if (todo & ActionRead)
  {
    asocket->socket.async_read_some(boost::asio::null_buffers(),
                                    std::bind(&multi::event_cb,
                                              this,
                                              asocket,
                                              ActionRead,
                                              std::placeholders::_1));
    asocket->current_action |= ActionRead;
  }
  else if (todo & ActionWrite)
  {
    asocket->socket.async_write_some(boost::asio::null_buffers(),
                                     std::bind(&multi::event_cb,
                                               this,
                                               asocket,
                                               ActionWrite,
                                               std::placeholders::_1));
    asocket->current_action |= ActionWrite;
  }

  return 0;
}

request::request() : _easy(curl_easy_init())
{
  if (!_easy)
    throw std::runtime_error("curl_easy_init() failed");

  curl_easy_setopt(_easy.get(), CURLOPT_HEADERFUNCTION, &header_cb_c);
  curl_easy_setopt(_easy.get(), CURLOPT_HEADERDATA, this);
  curl_easy_setopt(_easy.get(), CURLOPT_WRITEFUNCTION, &write_cb_c);
  curl_easy_setopt(_easy.get(), CURLOPT_WRITEDATA, this);
  //curl_easy_setopt(_easy.get(), CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(_easy.get(), CURLOPT_ERRORBUFFER, &_error[0]);
  curl_easy_setopt(_easy.get(), CURLOPT_PRIVATE, this);
  curl_easy_setopt(_easy.get(), CURLOPT_NOPROGRESS, 1L);
}

void request::set_url(std::string url)
{
  _url = std::move(url);
  curl_easy_setopt(_easy.get(), CURLOPT_URL, _url.c_str());
}

void request::add_header(std::string const& header)
{
  _header.reset(curl_slist_append(_header.release(), header.c_str()));
  curl_easy_setopt(_easy.get(), CURLOPT_HTTPHEADER, _header.get());
}

size_t request::header_cb(char* ptr, size_t size, size_t nmemb)
{
  if (_header_cb)
    return _header_cb(*this, ptr, size*nmemb);
  else
    return size * nmemb;
}

size_t request::write_cb(void* ptr, size_t size, size_t nmemb)
{
  return _read_cb(*this, ptr, size*nmemb);
}

}
}
