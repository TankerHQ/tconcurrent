#pragma once

#include <functional>
#include <mutex>

#include <tconcurrent/async.hpp>
#include <tconcurrent/future.hpp>
#include <tconcurrent/promise.hpp>

namespace tconcurrent
{
class job
{
public:
  job(job const&) = delete;
  job(job&&) = delete;
  job& operator=(job const&) = delete;
  job& operator=(job&&) = delete;

  using future_callback = std::function<tc::future<void>()>;

  job(future_callback cb) : _cb(std::move(cb))
  {
  }

  ~job()
  {
    auto fut = [this] {
      scope_lock _(_mutex);
      _stopping = true;
      return std::move(_future);
    }();
    fut.request_cancel();
    fut.wait();
  }

  /** Trigger the job
   *
   * If the job is already scheduled but not started, does nothing. If the job
   * has already started, schedule it once more.
   *
   * \return a future that will finish when the job has run once from begin to
   * end after the call to trigger.
   */
  tc::shared_future<void> trigger()
  {
    scope_lock _(_mutex);
    triggerImpl(false);
    return _future;
  }

  /** Trigger the job and return a future representing the next successful run
   *
   * Same as trigger(), but the future will finish only when the job has run
   * once without error.
   *
   * Note that if the job fails, it will not retry automatically, you will need
   * to call a trigger method.
   */
  tc::shared_future<void> trigger_success()
  {
    scope_lock _(_mutex);
    triggerImpl(true);
    return _successPromises.back().get_future().to_shared();
  }

private:
  using mutex = std::recursive_mutex;
  using scope_lock = std::lock_guard<mutex>;

  future_callback _cb;

  mutex _mutex;
  bool _scheduled{false};
  bool _stopping{false};
  bool _running{false};
  tc::shared_future<void> _future = tc::make_ready_future();
  std::vector<tc::promise<void>> _successPromises;

  void triggerImpl(bool setPromise)
  {
    if (!_scheduled)
    {
      _scheduled = true;
      if (setPromise)
        _successPromises.push_back(tc::promise<void>());
      _future = _future
                    .then(tc::get_synchronous_executor(),
                          [this](tc::shared_future<void> const&) {
                            // this is where we need the mutex to be recursive
                            scope_lock _(_mutex);
                            assert(_scheduled);
                            if (_stopping)
                              return tc::make_ready_future();
                            return reschedule();
                          })
                    .unwrap();
    }
  }

  tc::future<void> reschedule()
  {
    return tc::async([this] {
             size_t pendingPromises;
             {
               scope_lock _(_mutex);
               assert(_scheduled);
               _scheduled = false;
               if (_stopping)
                 return tc::make_ready_future();
               assert(!_running);
               _running = true;
               pendingPromises = _successPromises.size();
             }
             try
             {
               return _cb().then(
                   tc::get_synchronous_executor(),
                   [this, pendingPromises](tc::future<void> const& fut) {
                     scope_lock _(_mutex);
                     // _running may already be false if the task has
                     // been canceled too soon
                     _running = false;
                     if (fut.has_value())
                     {
                       for (size_t i = 0; i < pendingPromises; ++i)
                         _successPromises[i].set_value({});
                       _successPromises.erase(
                           _successPromises.begin(),
                           _successPromises.begin() + pendingPromises);
                     }
                   });
             }
             catch (...)
             {
               scope_lock _(_mutex);
               _running = false;
               return tc::make_ready_future();
             }
           })
        .unwrap();
  }
};
}
