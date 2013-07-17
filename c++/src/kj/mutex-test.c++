// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "mutex.h"
#include "debug.h"
#include <pthread.h>
#include <unistd.h>
#include <gtest/gtest.h>

namespace kj {
namespace {

// I tried to use std::thread but it threw a pure-virtual exception.  It's unclear if it's meant
// to be ready in GCC 4.7.

class Thread {
public:
  template <typename Func>
  explicit Thread(Func&& func) {
    KJ_ASSERT(pthread_create(
        &thread, nullptr, &runThread<Decay<Func>>,
        new Decay<Func>(kj::fwd<Func>(func))) == 0);
  }
  ~Thread() {
    KJ_ASSERT(pthread_join(thread, nullptr) == 0);
  }

private:
  pthread_t thread;

  template <typename Func>
  static void* runThread(void* ptr) {
    Func* func = reinterpret_cast<Func*>(ptr);
    KJ_DEFER(delete func);
    (*func)();
    return nullptr;
  }
};

inline void delay() { usleep(10000); }

TEST(Mutex, MutexGuarded) {
  MutexGuarded<uint> value(123);

  {
    Locked<uint> lock = value.lock();
    EXPECT_EQ(123, *lock);

    Thread thread([&]() {
      Locked<uint> threadLock = value.lock();
      EXPECT_EQ(456, *threadLock);
      *threadLock = 789;
    });

    delay();
    EXPECT_EQ(123, *lock);
    *lock = 456;
    auto earlyRelease = kj::mv(lock);
  }

  EXPECT_EQ(789, *value.lock());

  {
    auto rlock1 = value.lockForRead();

    Thread thread2([&]() {
      Locked<uint> threadLock = value.lock();
      *threadLock = 321;
    });

    delay();
    EXPECT_EQ(789, *rlock1);

    {
      auto rlock2 = value.lockForRead();
      EXPECT_EQ(789, *rlock2);
      auto rlock3 = value.lockForRead();
      EXPECT_EQ(789, *rlock3);
      auto rlock4 = value.lockForRead();
      EXPECT_EQ(789, *rlock4);
    }

    delay();
    EXPECT_EQ(789, *rlock1);
    auto earlyRelease = kj::mv(rlock1);
  }

  EXPECT_EQ(321, *value.lock());
}

TEST(Mutex, Lazy) {
  Lazy<uint> lazy;
  bool initStarted = false;

  Thread thread([&]() {
    EXPECT_EQ(123, lazy.get([&](SpaceFor<uint>& space) -> Own<uint> {
      __atomic_store_n(&initStarted, true, __ATOMIC_RELAXED);
      delay();
      return space.construct(123);
    }));
  });

  // Spin until the initializer has been entered in the thread.
  while (!__atomic_load_n(&initStarted, __ATOMIC_RELAXED)) {
    sched_yield();
  }

  EXPECT_EQ(123, lazy.get([](SpaceFor<uint>& space) { return space.construct(456); }));
  EXPECT_EQ(123, lazy.get([](SpaceFor<uint>& space) { return space.construct(789); }));
}

}  // namespace
}  // namespace kj
