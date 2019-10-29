#include <atomic>
#include <iostream>

#include <boost/thread/tss.hpp>

#include <tconcurrent/detail/util.hpp>
#include <tconcurrent/thread_pool.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/post.hpp>

namespace tconcurrent
{

// We do a pimpl to avoid exposing boost asio whose header can take up to a
// second to parse on some machines. Since this file is usually included
// everywhere, we want to keep it light.
struct thread_pool::impl
{
  boost::asio::io_service _io;
  std::unique_ptr<boost::asio::io_service::work> _work;
  std::vector<std::thread> _threads;
  std::atomic<unsigned> _num_running_threads{0};
  std::atomic<bool> _dead{false};

  error_handler_cb _error_cb{detail::default_error_cb};
  task_trace_handler_cb _task_trace_handler;
};

namespace detail
{
void default_error_cb(std::exception_ptr const&)
{
  std::cerr << "An error occured in threadpool" << std::endl;
  assert(false &&
         "An error occured in threadpool and no error handler was provided");
}
}

namespace
{
#if TCONCURRENT_USE_THREAD_LOCAL
thread_local void* current_executor;
#define SET_THREAD_LOCAL(tl, val) tl = val
#define GET_THREAD_LOCAL(tl) tl
#else
void noopdelete(void*)
{
}
boost::thread_specific_ptr<void> current_executor(noopdelete);
#define SET_THREAD_LOCAL(tl, val) tl.reset(val)
#define GET_THREAD_LOCAL(tl) tl.get()
#endif
}

thread_pool::thread_pool() : _p(new impl)
{
}

thread_pool::~thread_pool()
{
  if (!_prevent_destruction)
  {
    _p->_dead = true;
    stop();

    // When calling exit() any non-main thread is likely to be terminated
    // (especially on Windows), destructors will not run and the program may be
    // interrupted at any point. As a result tconcurrent could deadlock in Boost
    // Asio's IOCP code.
    if (_p->_num_running_threads.load())
    {
      std::cerr
          << "WARNING: It seems one of tconcurrent's threads died. Do NOT call "
             "exit() or terminate live threads, this may result in a deadlock!"
          << std::endl;
    }
    else
    {
      delete _p;
    }
  }
}

bool thread_pool::is_in_this_context() const
{
  return GET_THREAD_LOCAL(current_executor) == this;
}

bool thread_pool::is_single_threaded() const
{
  return _p->_threads.size() == 1;
}

boost::asio::io_service& thread_pool::get_io_service()
{
  return _p->_io;
}

void thread_pool::start(unsigned int thread_count)
{
  if (_p->_work)
    throw std::runtime_error("the threadpool is already running");

  _p->_work = std::make_unique<boost::asio::io_service::work>(_p->_io);
  for (unsigned int i = 0; i < thread_count; ++i)
    _p->_threads.emplace_back([this] { run_thread(); });
}

void thread_pool::run_thread()
{
  SET_THREAD_LOCAL(current_executor, this);
  ++_p->_num_running_threads;
  while (true)
  {
    try
    {
      _p->_io.run();
      SET_THREAD_LOCAL(current_executor, nullptr);
      break;
    }
    catch (...)
    {
      try
      {
        _p->_error_cb(std::current_exception());
      }
      catch (...)
      {
        std::cerr << "exception thrown in error handler" << std::endl;
        assert(false && "exception thrown in error handler");
      }
    }
  }
  --_p->_num_running_threads;
}

void thread_pool::stop()
{
  _p->_work = nullptr;
  for (auto& th : _p->_threads)
    th.join();
  _p->_threads.clear();
}

bool thread_pool::is_running() const
{
  return _p->_work != nullptr;
}

void thread_pool::set_error_handler(error_handler_cb cb)
{
  assert(cb);
  _p->_error_cb = std::move(cb);
}

void thread_pool::signal_error(std::exception_ptr const& e)
{
  _p->_error_cb(e);
}

void thread_pool::set_task_trace_handler(task_trace_handler_cb cb)
{
  _p->_task_trace_handler = std::move(cb);
}

void thread_pool::post(fu2::unique_function<void()> work, std::string name)
{
  assert(!_p->_dead.load());
  boost::asio::post(
      _p->_io,
      [this, work = std::move(work), name = std::move(name)]() mutable {
        if (_p->_task_trace_handler)
        {
          auto const before = std::chrono::steady_clock::now();
          work();
          auto const ellapsed = std::chrono::steady_clock::now() - before;
          _p->_task_trace_handler(name, ellapsed);
        }
        else
        {
          work();
        }
      });
}

void thread_pool::prevent_destruction()
{
  _prevent_destruction = true;
}
}
