#include <atomic>
#include <iostream>
#include <optional>

#include <boost/thread/tss.hpp>

#include <tconcurrent/detail/util.hpp>
#include <tconcurrent/thread_pool.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

namespace tconcurrent
{

struct thread_pool::impl
{
  using executor_type = boost::asio::io_context::executor_type;
  using work_guard = boost::asio::executor_work_guard<executor_type>;

  boost::asio::io_context _io;
  std::optional<work_guard> _work;
  std::vector<std::thread> _threads;
  std::atomic<unsigned> _num_running_threads{0};
  std::atomic<bool> _dead{false};

  // We need to be fork-safe, which means stopping all our threads before
  // a fork and restoring them after, so that they restart from a clean state
  std::atomic<unsigned> _num_threads_before_fork{0};

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
  _p->_dead = true;
  stop(true);
}

bool thread_pool::is_in_this_context() const
{
  return GET_THREAD_LOCAL(current_executor) == this;
}

bool thread_pool::is_single_threaded() const
{
  return _p->_threads.size() == 1;
}

boost::asio::io_context& thread_pool::get_io_service()
{
  return _p->_io;
}

void thread_pool::start(unsigned int thread_count)
{
  if (_p->_work)
    throw std::runtime_error("the threadpool is already running");

  _p->_work.emplace(boost::asio::make_work_guard(_p->_io.get_executor()));
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

void thread_pool::stop(bool cancel_work)
{
  _p->_work = std::nullopt;
  if (cancel_work)
    _p->_io.stop();
  for (auto& th : _p->_threads)
    th.join();
  _p->_threads.clear();

  // It is very important to at least terminate all threads we started because
  // otherwise a dlclose() call may unmap the library making the lonely thread
  // crash when woken up.
  // However, on Windows, when tconcurrent is loaded as part of a shared
  // library, when main() completes, or when exit() is called, all non-main
  // thread will be immediately killed. This is a problem because our worker
  // threads are most likely stuck in a critical section and killing them there
  // will make the io_context destructor deadlock. This code will detect that
  // threads were killed and not attempt to destroy the io_context.
  // On Windows, if the library has a thread running, FreeLibrary() is a no-op,
  // so we don't have the dlclose() issue described above.
  if (_p->_num_running_threads.load())
  {
    (void)_p.release();
  }
}

void thread_pool::stop_before_fork()
{
  assert(!_p->_num_threads_before_fork);
  _p->_num_threads_before_fork.store(_p->_num_running_threads);

  // Note that this can't _p.release(), that only happens on Windows during
  // process exit, so _p will still be valid in resume_after_fork
  stop(true);
}

void thread_pool::resume_after_fork()
{
  unsigned num_threads = _p->_num_threads_before_fork.load();
  auto error_cb = std::move(_p->_error_cb);
  auto task_trace_handler_cb = std::move(_p->_task_trace_handler);

  // Fresh start
  _p.reset(new impl);
  _p->_error_cb = std::move(error_cb);
  _p->_task_trace_handler = std::move(task_trace_handler_cb);
  this->start(num_threads);
}

bool thread_pool::is_running() const
{
  return _p->_work != std::nullopt;
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
  boost::asio::post(boost::asio::bind_executor(
      _p->_io.get_executor(),
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
      }));
}
}
