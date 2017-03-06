#ifndef TCONCURRENT_ASYNC_WAIT_HPP
#define TCONCURRENT_ASYNC_WAIT_HPP

#include <chrono>

#include <tconcurrent/future.hpp>
#include <tconcurrent/thread_pool.hpp>

#include <tconcurrent/detail/export.hpp>

namespace tconcurrent
{

/** Return a future that will be ready in \p delay
 *
 * The returned future is cancelable. If the executor is single threaded, a
 * cancelation request will immediately put the future in a canceled state.
 */
TCONCURRENT_EXPORT
future<void> async_wait(thread_pool& pool,
                        std::chrono::steady_clock::duration delay);

inline future<void> async_wait(std::chrono::steady_clock::duration delay)
{
  return async_wait(get_default_executor(), delay);
}

}

#endif
