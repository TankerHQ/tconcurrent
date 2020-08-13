#include <tconcurrent/async_wait.hpp>

#include <tconcurrent/packaged_task.hpp>
#include <tconcurrent/promise.hpp>

#include <emscripten.h>

namespace tconcurrent
{
namespace
{
struct async_wait_data
{
  promise<void> prom;
  bool fired{false};
  std::function<void()> callback;
  uint32_t timeout_id;
};
}
}

extern "C"
{
  EMSCRIPTEN_KEEPALIVE
  void tc_async_wait_do_call(
      std::shared_ptr<tconcurrent::async_wait_data>* pdata)
  {
    std::unique_ptr<std::shared_ptr<tconcurrent::async_wait_data>> data(pdata);
    auto cb = std::move((*data)->callback);
    if (cb)
      cb();
  }
}

namespace tconcurrent
{
future<void> async_wait(executor, std::chrono::steady_clock::duration delay)
{
  EM_ASM(if (!Module.tconcurrent_await_id) {
    Module.tconcurrent_last_timeout_id = 1;
    Module.tconcurrent_timeouts = {};
  });

  auto const data = std::make_shared<async_wait_data>();

  data->prom.get_cancelation_token().push_cancelation_callback([data] {
    if (std::exchange(data->fired, true))
      return;

    data->prom.get_cancelation_token().pop_cancelation_callback();
    data->prom.set_exception(std::make_exception_ptr(operation_canceled()));
    data->callback = nullptr;

    EM_ASM_(
        {
          clearTimeout(Module.tconcurrent_timeouts[$0]);
          delete Module.tconcurrent_timeouts[$0];
        },
        data->timeout_id);
  });

  data->callback = [data]() mutable {
    if (std::exchange(data->fired, true))
      return;

    data->prom.get_cancelation_token().pop_cancelation_callback();
    data->prom.set_value({});
    // can't reset to nullptr here as we might delete this callback
  };

  auto ppdata = new auto(data);

  auto const mdelay =
      std::chrono::duration_cast<std::chrono::milliseconds>(delay);
  // node's setTimeout returns an object, thanks node.
  data->timeout_id = EM_ASM_(
      {
        id = Module.tconcurrent_last_timeout_id++;
        Module.tconcurrent_timeouts[id] = setTimeout(
            function() { Module._tc_async_wait_do_call($0); }, $1);
        return id;
      },
      ppdata,
      int32_t(mdelay.count()));

  return data->prom.get_future();
}
}
