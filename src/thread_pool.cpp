#include <iostream>

#include <boost/thread/tss.hpp>

#include <tconcurrent/thread_pool.hpp>
#include <tconcurrent/detail/util.hpp>

#include <boost/asio/io_service.hpp>

namespace tconcurrent
{

struct thread_pool::impl
{
  boost::asio::io_service _io;
  std::unique_ptr<boost::asio::io_service::work> _work;
  std::vector<std::thread> _threads;
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
{}
boost::thread_specific_ptr<void> current_executor(noopdelete);
#define SET_THREAD_LOCAL(tl, val) tl.reset(val)
#define GET_THREAD_LOCAL(tl) tl.get()
#endif
}

thread_pool::thread_pool()
  : _p(new impl)
{
}

thread_pool::~thread_pool()
{
  _p->_dead = true;
  stop();
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
    _p->_threads.emplace_back(
        [this]{
          run_thread();
        });
}

void thread_pool::run_thread()
{
  SET_THREAD_LOCAL(current_executor, this);
  while (true)
  {
    try
    {
      _p->_io.run();
      SET_THREAD_LOCAL(current_executor, nullptr);
      return;
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
}

void thread_pool::stop()
{
  _p->_work = nullptr;
  for (auto &th : _p->_threads)
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

void thread_pool::post(std::function<void()> work, std::string name)
{
  assert(!_p->_dead.load());
  _p->_io.post([ this, work = std::move(work), name = std::move(name) ] {
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

thread_pool& get_global_single_thread()
{
  static thread_pool tp;
  return tp;
}

thread_pool& get_global_thread_pool()
{
  static thread_pool tp;
  return tp;
}

void start_thread_pool(unsigned int thread_count)
{
  auto& tp = get_global_thread_pool();
  if (!tp.is_running())
    tp.start(thread_count);
}

void start_single_thread()
{
  auto& tp = get_global_single_thread();
  if (!tp.is_running())
    tp.start(1);
}

thread_pool& get_default_executor()
{
  start_single_thread();
  return get_global_single_thread();
}

thread_pool& get_background_executor()
{
  start_thread_pool(std::thread::hardware_concurrency());
  return get_global_thread_pool();
}

synchronous_executor& get_synchronous_executor()
{
  static synchronous_executor e;
  return e;
}

}
