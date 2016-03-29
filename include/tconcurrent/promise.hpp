#ifndef TCONCURRENT_PROMISE_HPP
#define TCONCURRENT_PROMISE_HPP

#include <memory>

#include <tconcurrent/future.hpp>

namespace tconcurrent
{

template <typename T>
class promise
{
public:
  using value_type = typename future<T>::value_type;

  promise() : _p(std::make_shared<detail::shared_base<value_type>>())
  {
  }

  future<T> get_future() const
  {
    return future<T>(_p);
  }

  void set_value(value_type const& val)
  {
    _p->set(val);
  }
  void set_value(value_type&& val)
  {
    _p->set(std::move(val));
  }

  void set_exception(std::exception_ptr exc)
  {
    _p->set_exception(std::move(exc));
  }

private:
  std::shared_ptr<detail::shared_base<value_type>> _p;
};

}

#endif
