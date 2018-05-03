#ifndef TCONCURRENT_EXECUTOR_HPP
#define TCONCURRENT_EXECUTOR_HPP

#include <tconcurrent/detail/boost_fwd.hpp>
#include <tconcurrent/detail/export.hpp>

#include <functional>
#include <memory>
#include <string>

namespace tconcurrent
{
class executor
{
public:
  template <typename T,
            typename = std::enable_if_t<!std::is_same<T, executor>::value>>
  executor(T& e) : _p(std::make_shared<impl<std::decay_t<T>>>(e))
  {
  }

  executor(executor const&) = default;
  executor(executor&&) = default;
  executor& operator=(executor const&) = default;
  executor& operator=(executor&&) = default;

  void post(std::function<void()> work, std::string name = {})
  {
    _p->post(std::move(work), std::move(name));
  }

  boost::asio::io_service& get_io_service()
  {
    return _p->get_io_service();
  }

  bool is_single_threaded()
  {
    return _p->is_single_threaded();
  }

  bool is_in_this_context()
  {
    return _p->is_in_this_context();
  }

  void signal_error(std::exception_ptr const& e)
  {
    return _p->signal_error(e);
  }

private:
  struct impl_base
  {
    virtual ~impl_base() = default;
    virtual void post(std::function<void()>, std::string) = 0;
    virtual boost::asio::io_service& get_io_service() = 0;
    virtual bool is_single_threaded() = 0;
    virtual bool is_in_this_context() = 0;
    virtual void signal_error(std::exception_ptr const& e) = 0;
  };

  template <typename T>
  class impl : public impl_base
  {
  public:
    impl(T& context) : _context(context)
    {
    }

    void post(std::function<void()> f, std::string name) override
    {
      _context.post(std::move(f), std::move(name));
    }

    boost::asio::io_service& get_io_service() override
    {
      return _context.get_io_service();
    }

    bool is_single_threaded() override
    {
      return _context.is_single_threaded();
    }

    bool is_in_this_context() override
    {
      return _context.is_in_this_context();
    }

    void signal_error(std::exception_ptr const& e) override
    {
      return _context.signal_error(e);
    }

  private:
    T& _context;
  };

  std::shared_ptr<impl_base> _p;
};

class thread_pool;
TCONCURRENT_EXPORT thread_pool& get_global_single_thread();
TCONCURRENT_EXPORT executor get_default_executor();
TCONCURRENT_EXPORT executor get_background_executor();

/// Executor that runs its work in-place
class synchronous_executor
{
public:
  template <typename F>
  void post(F&& work, std::string const& = {})
  {
    work();
  }
};

inline synchronous_executor get_synchronous_executor()
{
  return synchronous_executor{};
}
}

#endif
