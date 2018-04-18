#ifndef TCONCURRENT_EXECUTOR_HPP
#define TCONCURRENT_EXECUTOR_HPP

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

private:
  struct impl_base
  {
    virtual ~impl_base() = default;
    virtual void post(std::function<void()>, std::string) = 0;
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
