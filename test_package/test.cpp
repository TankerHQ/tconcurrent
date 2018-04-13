#include <iostream>

#include <tconcurrent/coroutine.hpp>

int main()
{
  std::cout
      << tc::async_resumable([](tc::awaiter&) { return "Hello, World!"; }).get()
      << std::endl;
}
