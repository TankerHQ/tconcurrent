#include <tconcurrent/executor.hpp>

#include <tconcurrent/thread_pool.hpp>

#include <emscripten.h>

extern "C"
{
  EMSCRIPTEN_KEEPALIVE
  void tc_executor_do_call(std::function<void()>* pf)
  {
    std::unique_ptr<std::function<void()>> f(pf);
    (*pf)();
  }
}

namespace tconcurrent
{
namespace
{
class default_execution_context
{
public:
  void post(std::function<void()> f, std::string name)
  {
    EM_ASM_({ setTimeout(function() { Module._tc_executor_do_call($0); }, 0); },
            new auto(f));
  }

  boost::asio::io_service& get_io_service()
  {
    throw std::runtime_error("no io service on this executor");
  }

  bool is_single_threaded()
  {
    return true;
  }

  bool is_in_this_context()
  {
    return true;
  }

  void signal_error(std::exception_ptr const& e)
  {
    EM_ASM(console.error("got an uncaught execption"););
  }
};

default_execution_context default_context;
}

executor get_default_executor()
{
  return executor(default_context);
}

executor get_background_executor()
{
  return executor(default_context);
}
}
