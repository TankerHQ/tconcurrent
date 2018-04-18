#ifndef TCONCURRENT_EXECUTOR_HPP
#define TCONCURRENT_EXECUTOR_HPP

#include <tconcurrent/detail/export.hpp>

#include <functional>
#include <string>

namespace tconcurrent
{
class executor
{
public:
  template <typename T,
            typename = std::enable_if_t<!std::is_same<T, executor>::value>>
  executor(T& e)
    : _post([&e](auto&&... args) {
      e.post(std::forward<decltype(args)>(args)...);
    })
  {
  }

  executor(executor const&) = default;
  executor(executor&&) = default;
  executor& operator=(executor const&) = default;
  executor& operator=(executor&&) = default;

  void post(std::function<void()> work, std::string name = {})
  {
    _post(std::move(work), std::move(name));
  }

private:
  std::function<void(std::function<void()>, std::string name)> _post;
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
