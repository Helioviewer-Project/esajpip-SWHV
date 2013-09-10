#include <assert.h>
#include <errno.h>
#include <sched.h>
#include "mutex.h"


namespace ipc
{

  bool Mutex::Init(bool initial_owner)
  {
    assert(!IsValid());

    bool res = false;

    pthread_mutexattr_t mutexAttr;

    pthread_mutexattr_init(&mutexAttr);
    pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutexattr_setpshared(&mutexAttr, PTHREAD_PROCESS_PRIVATE);

    if(!pthread_mutex_init(&mutex, &mutexAttr)) {
      if(initial_owner) res = (Wait() == WAIT_OBJECT);
      else res = true;
    }

    if(!res) pthread_mutex_destroy(&mutex);
    pthread_mutexattr_destroy(&mutexAttr);

    if(res) IPCObject::Init();
    return res;
  }

  WaitResult Mutex::Wait(int time_out)
  {
    assert(IsValid());

    if(time_out <= -1) {
      if(pthread_mutex_lock(&mutex)) return WAIT_ERROR;
      else {
        locker = pthread_self();
        return WAIT_OBJECT;
      }

    } else {
      int res;
      struct timespec delay = {0, 1000000};

      while((res = pthread_mutex_trylock(&mutex)) == EBUSY) {
        if(!time_out) return WAIT_TIMEOUT;
        else {
          nanosleep(&delay, NULL);
          time_out--;
        }
      }

      if(res) return WAIT_ERROR;
      else {
        locker = pthread_self();
        return WAIT_OBJECT;
      }
    }
  }

  bool Mutex::Release()
  {
    assert(IsValid());

    if(locker != pthread_self()) return false;
    else {
      if(pthread_mutex_unlock(&mutex)) return false;
      else {
        //pthread_yield();
	sched_yield();
        return true;
      }
    }
  }

  bool Mutex::Dispose()
  {
    if(!IsValid()) return true;
    else {
      Release();

      if(pthread_mutex_destroy(&mutex)) return false;
      else {
        IPCObject::Dispose();
        return true;
      }
    }
  }

}
