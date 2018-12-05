#ifndef TCONCURRENT_DETAIL_BOOST_FWD_HPP
#define TCONCURRENT_DETAIL_BOOST_FWD_HPP

#include <boost/version.hpp>

namespace boost
{
namespace asio
{
#if BOOST_VERSION < 106600
class io_service;
#else
class io_context;
using io_service = io_context;
#endif
}

namespace system
{
class error_code;
}
}

#endif
