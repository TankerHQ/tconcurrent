#ifndef TCONCURRENT_DELAY_HPP
#define TCONCURRENT_DELAY_HPP

#include <chrono>

#include <tconcurrent/future.hpp>
#include <tconcurrent/thread_pool.hpp>

namespace tconcurrent
{

future<void> async_wait(thread_pool& pool,
                        std::chrono::steady_clock::duration delay);

inline future<void> async_wait(std::chrono::steady_clock::duration delay)
{
  return async_wait(get_default_executor(), delay);
}

}

#endif
