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

  cancelation_token& get_cancelation_token()
  {
    return *_p->get_cancelation_token();
  }

private:
  using shared_type = detail::shared_base<value_type>;

  detail::promise_ptr<shared_type> _p;
};

}

#endif
