#ifndef TCONCURRENT_ASIO_USE_FUTURE_HPP
#define TCONCURRENT_ASIO_USE_FUTURE_HPP

#include <tconcurrent/future.hpp>
#include <tconcurrent/promise.hpp>

#include <boost/asio/async_result.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

namespace tconcurrent
{
namespace asio
{
struct use_future_t
{
};

constexpr use_future_t use_future;

namespace detail
{

template <typename T>
class promise_creator
{
public:
  using future_type = future<T>;

  future_type get_future()
  {
    return _promise.get_future();
  }

protected:
  tc::promise<T> _promise;
};

// For completion signature void().
class promise_handler_0 : public promise_creator<void>
{
public:
  void operator()()
  {
    this->_promise.set_value({});
  }
};

// For completion signature void(error_code).
class promise_handler_ec_0 : public promise_creator<void>
{
public:
  void operator()(const boost::system::error_code& ec)
  {
    if (ec)
    {
      this->_promise.set_exception(
          std::make_exception_ptr(boost::system::system_error(ec)));
    }
    else
    {
      this->_promise.set_value({});
    }
  }
};

// For completion signature void(exception_ptr).
class promise_handler_ex_0 : public promise_creator<void>
{
public:
  void operator()(const std::exception_ptr& ex)
  {
    if (ex)
    {
      this->_promise.set_exception(ex);
    }
    else
    {
      this->_promise.set_value({});
    }
  }
};

// For completion signature void(T).
template <typename T>
class promise_handler_1 : public promise_creator<T>
{
public:
  template <typename Arg>
  void operator()(Arg&& arg)
  {
    this->_promise.set_value(std::forward<Arg>(arg));
  }
};

// For completion signature void(error_code, T).
template <typename T>
class promise_handler_ec_1 : public promise_creator<T>
{
public:
  template <typename Arg>
  void operator()(const boost::system::error_code& ec, Arg&& arg)
  {
    if (ec)
    {
      this->_promise.set_exception(
          std::make_exception_ptr(boost::system::system_error(ec)));
    }
    else
      this->_promise.set_value(std::forward<Arg>(arg));
  }
};

// For completion signature void(exception_ptr, T).
template <typename T>
class promise_handler_ex_1 : public promise_creator<T>
{
public:
  template <typename Arg>
  void operator()(const std::exception_ptr& ex, Arg&& arg)
  {
    if (ex)
      this->_promise.set_exception(ex);
    else
      this->_promise.set_value(std::forward<Arg>(arg));
  }
};

// Helper template to choose the appropriate concrete promise handler
// implementation based on the supplied completion signature.
template <typename>
class promise_handler_selector;

template <>
class promise_handler_selector<void()> : public promise_handler_0
{
};

template <>
class promise_handler_selector<void(boost::system::error_code)>
  : public promise_handler_ec_0
{
};

template <>
class promise_handler_selector<void(std::exception_ptr)>
  : public promise_handler_ex_0
{
};

template <typename Arg>
class promise_handler_selector<void(Arg)> : public promise_handler_1<Arg>
{
};

template <typename Arg>
class promise_handler_selector<void(boost::system::error_code, Arg)>
  : public promise_handler_ec_1<Arg>
{
};

template <typename Arg>
class promise_handler_selector<void(std::exception_ptr, Arg)>
  : public promise_handler_ex_1<Arg>
{
};

template <typename Signature>
class promise_handler : public promise_handler_selector<Signature>
{
public:
  typedef void result_type;

  promise_handler(use_future_t)
  {
  }
};
}
}
}

namespace boost
{
namespace asio
{
template <typename Result, typename... Args>
class async_result<::tconcurrent::asio::use_future_t, Result(Args...)>
{
public:
  using completion_handler_type =
      ::tconcurrent::asio::detail::promise_handler<Result(Args...)>;
  using return_type = typename completion_handler_type::future_type;

  explicit async_result(completion_handler_type& h) : _future(h.get_future())
  {
  }

  return_type get()
  {
    return std::move(_future);
  }

private:
  return_type _future;
};
}
}

#endif
