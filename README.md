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
int main()
{
  // Run an asynchronous task, like `std::async`
  tc::future<std::string> f1 = tc::async([]() -> std::string {
    return "42";
  });

  // Run a resumable asynchronous task
  tc::future<int> f2 = tc::async_resumable([]() -> tc::cotask<int> {
    int const val = TC_AWAIT(receive_value());
    TC_AWAIT(send_value(val * 2));
    TC_RETURN(42);
  });

  f1.get();
  f2.get();
}
```

### Setup

We are actively working to allow external developers to build and test this SDK
from its source.

### Doxygen

To generate and open documentation:

```
$ cd doc && doxygen && xdg-open build/html/index.html
```

## Documentation

To better understand how tconcurrent works, a big picture explanation is here:

- [Async primitives](doc/1-async-primitives.md)
- [Coroutines](doc/2-coroutines.md)
- [Task canceler](doc/3-task-canceler.md)

## Contributing

We welcome feedback. Feel free to open any issue on the Github bug tracker.

## License and Terms

The tconcurrent library is licensed under the
[Apache License, version 2.0](http://www.apache.org/licenses/LICENSE-2.0).
