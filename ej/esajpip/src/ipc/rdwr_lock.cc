#include "rdwr_lock.h"
#include <assert.h>
#include <errno.h>
#include <sched.h>

#include <iostream>
using namespace std;


namespace ipc
{

  bool RdWrLock::Init()
  {
    assert(!IsValid());

    bool res = false;

    pthread_rwlockattr_t rwlockAttr;

    pthread_rwlockattr_init(&rwlockAttr);
    pthread_rwlockattr_setpshared(&rwlockAttr, PTHREAD_PROCESS_PRIVATE);

    res = !pthread_rwlock_init(&rwlock, &rwlockAttr);
    pthread_rwlockattr_destroy(&rwlockAttr);

    if(res) IPCObject::Init();
    return res;
  }

  WaitResult RdWrLock::Wait(int time_out)
  {
    assert(IsValid());

    if(time_out <= -1) {
      if(pthread_rwlock_rdlock(&rwlock)) return WAIT_ERROR;
      else return WAIT_OBJECT;

    } else {
      int res;
      struct timespec delay = {0, 1000000};

      while((res = pthread_rwlock_tryrdlock(&rwlock)) == EBUSY) {
        if(!time_out) return WAIT_TIMEOUT;
        else {
          nanosleep(&delay, NULL);
          time_out--;
        }
      }

      if(res) return WAIT_ERROR;
      else return WAIT_OBJECT;
    }
  }

  WaitResult RdWrLock::WaitForWriting(int time_out)
  {
    assert(IsValid());

    if(time_out <= -1) {
      if(pthread_rwlock_wrlock(&rwlock)) return WAIT_ERROR;
      else return WAIT_OBJECT;

    } else {
      int res;
      struct timespec delay = {0, 1000000};

      while((res = pthread_rwlock_trywrlock(&rwlock)) == EBUSY) {
        if(!time_out) return WAIT_TIMEOUT;
        else {
          nanosleep(&delay, NULL);
          time_out--;
        }
      }

      if(res) return WAIT_ERROR;
      else return WAIT_OBJECT;
    }
  }

  bool RdWrLock::Release()
  {
    assert(IsValid());

    if(pthread_rwlock_unlock(&rwlock)) return false;
    else {
      //pthread_yield();
	sched_yield();
      return true;
    }
  }

  bool RdWrLock::Dispose()
  {
    if(!IsValid()) return true;
    else {
      Release();

      if(pthread_rwlock_destroy(&rwlock)) return false;
      else {
        IPCObject::Dispose();
        return true;
      }
    }
  }

}
