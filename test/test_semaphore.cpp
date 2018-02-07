#include <doctest.h>

#include <tconcurrent/semaphore.hpp>

using namespace tconcurrent;

SCENARIO("test semaphore")
{
  GIVEN("an empty semaphore")
  {
    semaphore sem{0};

    THEN("it is null")
    {
      CHECK(0 == sem.count());
    }

    THEN("we can release it")
    {
      sem.release();
      CHECK(1 == sem.count());
    }

    THEN("we can not acquire it")
    {
      auto fut = sem.acquire();
      CHECK(!fut.is_ready());
      CHECK(0 == sem.count());

      AND_WHEN("someone releases it")
      {
        sem.release();

        THEN("the acquire succeeds")
        {
          CHECK(fut.is_ready());
          CHECK(0 == sem.count());
        }
      }
    }
  }

  GIVEN("a semaphore 4-initialized")
  {
    semaphore sem{4};

    THEN("it holds 4")
    {
      CHECK(4 == sem.count());
    }

    THEN("we can release it")
    {
      sem.release();
      CHECK(5 == sem.count());
    }

    THEN("we can acquire it")
    {
      auto fut = sem.acquire();
      CHECK(fut.is_ready());
      CHECK(3 == sem.count());
    }

    // FIXME this is broken because future expects a copyable type
    //THEN("we can get a scope_lock")
    //{
    //  auto l = sem.get_scoped_lock();
    //}
  }
}

