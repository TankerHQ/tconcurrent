<a href="#readme"><img src="https://tanker.io/images/github-logo.png" alt="Tanker logo" width="180" /></a>

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

# Tanker coroutine library

## Table of Contents

 * [Overview](#overview)
 * [Setup](#setup)
 * [Doc](#Doc)
 * [Contributing](#contributing)
 * [License and Terms](#license-and-terms)

## Overview

tconcurrent is a coroutine library that allows writing asynchronous code that is
both C++14 and coroutines-TS-compatible.

The coroutines are stackful when compiled in C++14 mode and stackless when
using the coroutines-TS mode.

### Example

tconcurrent exposes an `async_resumable` function that will run a coroutine
asynchronously. A coroutine function must return a `tc::cotask` and can then use
the `TC_AWAIT` and `TC_RETURN` macros.

```c++
void start_request()
{
  tc::future<int> f = tc::async_resumable([]() -> tc::cotask<int> {
    int const val = TC_AWAIT(receive_value());
    TC_AWAIT(send_value(val * 2));
    TC_RETURN(42);
  });
}
```

### Quick documentation

#### Promise and future

tconcurrent implements the promise and future concepts as they are implemented
in most libraries, like boost. A future represents a result that may arrive
later, or an error (which can be a cancelation). This result is produced by a
task (e.g. running a calculation, or an asynchronous read from a file
descriptor).

#### Execution contexts and executors

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

#### Coroutines

The main feature of tconcurrent is the coroutines. `async_resumable` runs a
coroutine asynchronously and returns a future with the return value of the
coroutine.

A coroutine function must return a `cotask` which is templated on the return
value. It must use `TC_RETURN` instead of return. To await a result (another
`cotask` or a `future`, it can use `TC_AWAIT`. To avoid life-time errors, it is
recommended to never do anything else than `TC_AWAIT` with a `cotask`,
especially not storing it, moving it or returning it. Doing such things can lead
to use-after-free or use-after-return errors with the arguments of the called
function. In the stackless coroutines mode, a coroutine method called but not
awaited will have no effect.

It is not legal to call `TC_AWAIT` in a `catch` clause. This will not compile
with the coroutines-TS in compliant compilers (only Visual C++ at this time),
and trigger a runtime assertion otherwise.

It is also not legal to call `TC_AWAIT` in a destructor since a destructor does
not return a `cotask`.

#### Cancelation

Everything in tconcurrent is built with the support for cancelation. Futures
have a `request_cancel` method which will forward the cancelation request to the
underlying task that should produce the future result. That task has to
implement cancelation as it can; it may not support cancelation at all and
`request_cancel` will have no effect.

tconcurrent primitives give a special guarantee which is that if the cancel is
requested on the same execution context as the corresponding task, and that
execution context is single threaded, the cancelation is synchronous (i.e. when
`request_cancel` returns, the future is ready with a canceled result). This is
useful when you need to cancel tasks in a destructor so that there are no more
running tasks on the current object and it becomes safe to destroy.

All of tconcurrent primitives support cancelation, including coroutines. When
canceling a coroutine in a single-threaded environment, the coroutine will be
aborted synchronously (i.e. the stack will be unwound and all destructors will
be called). For stackful coroutines, this is done by throwing a special
exception named `detail::abort_coroutine` at the suspension point. This
exception does not inherit from `std::exception`, and must not be intercepted.
You must rethrow it if you catch it.

As a consequence to the coroutines limitations, it is not authorized to cancel a
coroutine from a `catch` clause.

### Setup

We are actively working to allow external developers to build and test this SDK
from its source.

### Doc

To generate and open documentation:

```
$ cd doc && doxygen && xdg-open build/html/index.html
```

## Contributing

We welcome feedback. Feel free to open any issue on the Github bug tracker.

## License and Terms

The tconcurrent library is licensed under the
[Apache License, version 2.0](http://www.apache.org/licenses/LICENSE-2.0).
