# Async primitives

## Promise and future

tconcurrent implements the promise and future concepts as they are implemented
in most libraries, like boost. A future represents a result that may arrive
later, or an error (which can be a cancelation). This result is produced by a
task (e.g. running a calculation, or an asynchronous read from a file
descriptor).

`tc::future` will consume its result when used: `get()` will return the
value by move. `tc::shared_future` allows reuse of the result: `get()` will
return the value by const reference.

Note that tconcurrent's futures implement the `then()` function to set a
continuation, as opposed to `std::future`.

## Execution contexts and executors

tconcurrent has an executor and execution context abstractions inspired by some
of the discussions for C++2a. They are currently not optimal and expose the
underlying boost ASIO IO service.

An *execution context* is something that executes tasks.
An *executor* is something that defines how tasks are executed.

An *execution context* should expose *executor*s that run tasks on that
*context*, but in tconcurrent *execution context*s implement the *executor*
concept. This should be changed to reflect the design of C++2a executors.

tconcurrent exposes two execution contexts: the default execution context and
the background execution context. They are not running by default, they are
started lazily whenever they are first needed.

The default execution context is the one used when no executor is specified in
the different methods (`async`, `then`, etc).  It is single threaded, you should
run all your code in that context.

The background execution context can be used for computation intensive tasks. It
has as many threads as there are logical CPU cores.

## Sender and receiver

Another proposal for C++ is [the sender/receiver
concept](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0443r14.html).
A modified version of an early version of this proposal was implemented in
tconcurrent mostly because they provide better compilation times and binary size
overhead over promises and futures because they skip the type erasure.

This concept is quite hard to grasp, I recommend watching talks about it:

- An older one, a good introduction and more similar to what's in tconcurrent: https://www.youtube.com/watch?v=h-ExnuD6jms
- A newer one, which focuses more on the how than the why: https://www.youtube.com/watch?v=xLboNIf7BTg

Another more or less equivalent concept is the completion token from Boost ASIO,
which is [proposed in the Networking
TS](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p2444r0.pdf).

## Cancelation

Everything in tconcurrent is built with support for cancelation.

tconcurrent's futures have a `request_cancel` method which will forward the
cancelation request to the underlying task that should produce the future
result. That task has to implement cancelation as it can; it may not support
cancelation at all and `request_cancel` will have no effect.

Senders/receivers also have support for cancelation through a
`cancelation_token`. As for futures, cancelation may have no effect at all.

tconcurrent primitives provide a special guarantee, if the following conditions
are met:

- the cancelation is requested on an executor bound to the same execution
    context as the corresponding task
- the execution context run by a single threaded

Then the cancelation is synchronous (i.e. when `request_cancel` returns, the
future is ready with a canceled result). This is particularly useful when you
need to cancel tasks in a destructor so that there are no more running tasks on
the current object and it becomes safe to destroy. If the execution context is
not single threaded, or if the cancelation is requested on another executor,
there is no guarantee that the cancelation will be synchronous.

All of tconcurrent primitives support cancelation, including coroutines. When
canceling a coroutine in a single-threaded environment, the coroutine will be
aborted synchronously (i.e. the stack will be unwound and all destructors will
be called). For stackful coroutines, this is done by throwing a special
exception named `detail::abort_coroutine` at the suspension point. This
exception does not inherit from `std::exception`, and must not be caught.
You must rethrow it if you catch it.

Important: tconcurrent does not support canceling coroutines from a different
thread, so the coroutine and the cancelation must run on the same
single-threaded execution context.

Note: The word "cancelation" is not present in all dictionaries, however,
"canceling" is. "canceling" is US English, and "cancelling" with two "l"s is UK
English. tconcurrent chose the US spelling.
