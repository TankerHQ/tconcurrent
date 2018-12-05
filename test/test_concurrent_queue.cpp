#include <doctest.h>

#include <tconcurrent/concurrent_queue.hpp>

using namespace tconcurrent;

SCENARIO("test concurrent_queue")
{
  GIVEN("an empty queue")
  {
    concurrent_queue<int> q;
    THEN("it is empty")
    {
      CHECK(0 == q.size());
    }
    THEN("we can not pop")
    {
      CHECK(!q.pop().is_ready());
    }
    THEN("we can push")
    {
      q.push(1);
      q.push(2);
      q.push(3);
      CHECK(3 == q.size());
    }
    THEN("pushing unlocks a poper")
    {
      auto fut = q.pop();
      CHECK(!fut.is_ready());
      q.push(18);
      CHECK(fut.is_ready());
      CHECK(18 == fut.get());
    }
  }
  GIVEN("a queue with 3 values")
  {
    concurrent_queue<int> q;
    q.push(1);
    q.push(2);
    q.push(3);
    THEN("it holds 3 values")
    {
      CHECK(3 == q.size());
    }
    THEN("we can pop")
    {
      CHECK(q.pop().is_ready());
      CHECK(2 == q.size());
    }
    THEN("we can pop in the same order")
    {
      CHECK(1 == q.pop().get());
      CHECK(2 == q.pop().get());
      CHECK(3 == q.pop().get());

      CHECK(0 == q.size());
    }
    THEN("we can push and pop in the same order")
    {
      CHECK(1 == q.pop().get());
      q.push(4);
      CHECK(2 == q.pop().get());
      q.push(5);
      CHECK(3 == q.pop().get());
      CHECK(4 == q.pop().get());
      CHECK(5 == q.pop().get());

      CHECK(0 == q.size());
    }
  }
}
