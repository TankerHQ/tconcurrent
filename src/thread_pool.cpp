#include <iostream>

#include <boost/thread/tss.hpp>

#include <tconcurrent/thread_pool.hpp>
#include <tconcurrent/detail/util.hpp>

namespace tconcurrent
{

namespace
{
void noopdelete(void*)
{}
// can't use c++11 thread_local because of osx and windoz \o/
boost::thread_specific_ptr<void> current_executor(noopdelete);
}

thread_pool::~thread_pool()
{
  _dead = true;
  stop();
}

bool thread_pool::is_in_this_context() const
{
  return current_executor.get() == this;
}

void thread_pool::start(unsigned int thread_count)
{
  if (_work)
    throw std::runtime_error("the threadpool is already running");

  _work = detail::make_unique<boost::asio::io_service::work>(_io);
  for (unsigned int i = 0; i < thread_count; ++i)
    _threads.emplace_back(
        [this]{
          run_thread();
        });
}

void thread_pool::run_thread()
{
  current_executor.reset(this);
  while (true)
  {
    try
    {
      _io.run();
      current_executor.reset(nullptr);
      return;
    }
    catch (std::exception &e)
    {
      // TODO replace these by error handlers
      std::cerr << "Error caught in threadpool: " << e.what() << std::endl;
    }
    catch (...)
    {
      std::cerr << "Unknown error caught in threadpool" << std::endl;
    }
  }
}

void thread_pool::stop()
{
  _work = nullptr;
  for (auto &th : _threads)
    th.join();
  _threads.clear();
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

thread_pool& get_default_executor()
{
  start_thread_pool(std::thread::hardware_concurrency());
  return get_global_thread_pool();
}

}
