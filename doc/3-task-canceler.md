# Task canceler

Asynchronous code and coroutines make dealing with object life-times harder.
When implementing a class with asynchronous operations, there are two was to
deal with that:

- Before an object is destroyed, require that the caller cancels all pending
    operations and wait for their completion or cancelation
- When an object is destroyed, automatically cancel all pending operations

The task cancelers help implementing the second option.

## What is it?

There are two implementations of the `task_canceler`, one for futures and the
other for senders. They work similarly.

A task_canceler keeps track of all the asynchronous tasks associated with an
object to be able to cancel them in the destructor. It relies on the fact that
canceling on a single-threaded execution context is synchronous.

## How to use it?

The task canceler is supposed to be used as a member of your class. It must be
the last member of your class so that it is the first to be destroyed. All your
functions should use it to store the returned futures.

```c++
class AsyncClass
{
public:
  tc::future<int> work_result()
  {
    return _canceler.run([this]() {
      return tc::async_resumable([]() -> tc::cotask<int> {
        TC_RETURN(TC_AWAIT(do_stuff(_name, _buffer)));
      });
    });
  }

private:
  std::vector<uint8_t> _buffer;
  std::string _name;

  // When this member is destroyed, all tasks will be canceled
  tc::task_canceler _canceler;
};
```
