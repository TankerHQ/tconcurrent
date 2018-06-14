#include <iostream>

#include <tconcurrent/async.hpp>

int main()
{
  std::cout << tc::async([]() { return "Hello, World!"; }).get() << std::endl;
}
