# Coroutines

The main feature of tconcurrent is the coroutines. `async_resumable` runs a
coroutine asynchronously and returns a future with the return value of the
coroutine.

This is similar to Boost ASIO's `co_spawn` except that `async_resumable` takes a
callable instead of an awaitable.

## How to use them

A coroutine function must return a `cotask` which is an awaitable lightweight
object templated on the return value. It must use `TC_RETURN` instead of return.
To await a result (another `cotask` or a `future`), it must use `TC_AWAIT`.

```c++
tc::cotask<int> receive_value();
tc::cotask<void> send_value();

int main()
{
  tc::future<int> f = tc::async_resumable([]() -> tc::cotask<int> {
    int const val = TC_AWAIT(receive_value());
    TC_AWAIT(send_value(val * 2));
    TC_RETURN(42);
  });
  f.get();
}
```

## C++20 and the coroutines-TS

tconcurrent has two compatible implementations of coroutines so that the same
code can be compiled in two flavors.

The first one is a stackful implementation in `stackful_coroutine.hpp`. It
relies on Boost Context to allocate stacks and switch between them.

The second one is a stackless implementation in `stackless_coroutine.hpp`. It
relies on C++20's coroutines. With this implementation, the `TC_AWAIT` and
`TC_RETURN` macros expand to `co_await` and `co_return` respectively.

Note that C++20's only brings building blocks for coroutines, but a library is
needed to implement what's needed to have usable coroutines, so tconcurrent is
still needed in a C++20 environment.

To choose between the two implementations, the code only needs to include
`tconcurrent/coroutine.hpp`, and the build system needs to define the macro
`TCONCURRENT_COROUTINES_TS` to `1` to switch to the stackless implementation.

Important: The two implementation are not ABI-compatible. Do not link code using
both implementations in the same binary!

## Cancelation

tconcurrent coroutines can only be run on single-threaded execution contexts.
They can also only be canceled from an executor bound to the execution context
they are running on.

This gives the guarantee to tconcurrent that a coroutine can only be canceled on
a suspension point, and further allows tconcurrent to synchronously cancel
coroutines.

When `request_cancel()` is called on a future linked to a coroutine, the
coroutine is immediately destroyed. In the stackful implementation, the
coroutine is resumed and the suspension point will throw an `abort_coroutine`
exception which must not be caught. This will allow all the destructors to run.
In the stackless implementation, `coroutine_handle::destroy()` is called.

## Pitfalls

### Differences between the stackful and stackless implementation

In the stackful coroutines mode, a coroutine method called but not awaited will
run, `TC_AWAIT` is almost a no-op.

In the stackless coroutines mode, a coroutine method called but not
awaited will have no effect.

In other words, stackful coroutines are hot-start and stackless coroutines are
cold-start coroutines.

```c++
tc::cotask<void> print_hello();

tc::cotask<void> work()
{
  // If you forget TC_AWAIT, this code will just work in stackful mode. However,
  // in stackless mode, print_hello will *not* be called.
  print_hello();
}
```

In stackless mode, some compiler will issue a warning if you forget to use the
returned `cotask`.

### Storing a cotask

To avoid life-time errors, it is recommended to never do anything else than
`TC_AWAIT` with a `cotask`, especially not storing it, moving it or returning
it. Doing such things can lead to use-after-free or use-after-return errors with
the arguments of the called function.

This is not a limitation of tconcurrent but rather a limitation of C++20's
coroutines.

In the stackless implementation, the following code is undefined behavior.

```c++
tc::cotask<void> print(std::string const& s)
{
  std::cout << s;
  TC_RETURN();
}

tc::cotask<void> work()
{
  // This line will allocate a temporary std::string, store a cotask and destroy
  // the std::string
  auto task = print("test");

  // This will run the print function, which has captured by address the now
  // destroyed std::string
  TC_AWAIT(task); // CRASH!

  // It is thus recommended to not store cotasks
  TC_AWAIT(print("test")); // safe
}
```

### Other caveats

It is not legal to call `TC_AWAIT` in a `catch` clause. This will not compile
with the coroutines-TS on compliant compilers. In stackful mode, such a code
will trigger an assertion failure.

Due to a limitation in Boost Context, canceling a coroutine from a `catch`
clause is also impossible and will trigger an assertion failure. Note that this
works fine with the stackless coroutines.

It is not legal either to call `TC_AWAIT` in a destructor since a destructor
cannot return a `cotask`. In stackless mode, it will not compile. In stackful
mode, it may trigger undefined behavior.
