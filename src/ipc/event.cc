#include <errno.h>
#include <assert.h>
#include <sys/time.h>
#include "event.h"

namespace ipc
{

  bool Event::Init(bool manual_reset, bool initial_state)
  {
    assert(!IsValid());

    bool res = false;

    pthread_mutexattr_t mutexAttr;

    pthread_mutexattr_init(&mutexAttr);
    pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutexattr_setpshared(&mutexAttr, PTHREAD_PROCESS_PRIVATE);

    if(!pthread_mutex_init(&mutex, &mutexAttr)) {
      if(!pthread_cond_init(&condv, NULL)) {
        this->manual_reset = manual_reset;
        state = initial_state;
        res = true;
      }
    }

    if(res) IPCObject::Init();
    else {
      pthread_mutex_destroy(&mutex);
      pthread_cond_destroy(&condv);
    }

    pthread_mutexattr_destroy(&mutexAttr);

    return res;
  }

  WaitResult Event::Wait(int time_out)
  {
    assert(IsValid());

    WaitResult res = WAIT_OBJECT;

    pthread_mutex_lock(&mutex);

    if(!state) {
      if(time_out <= -1) pthread_cond_wait(&condv, &mutex);
      else {
        struct timespec ts;

        #ifndef CLOCK_REALTIME
          gettimeofday((timeval *)&ts, NULL);
          
          ts.tv_sec += (time_out / 1000);

        #else
          clock_gettime(CLOCK_REALTIME, &ts);

          ts.tv_sec += (time_out / 1000);
          ts.tv_nsec += (time_out % 1000) * 1000000;

          ts.tv_sec += (ts.tv_nsec / 1000000000);
          ts.tv_nsec %= 1000000000;
        #endif

        if(pthread_cond_timedwait(&condv, &mutex, &ts) == ETIMEDOUT)
          res = WAIT_TIMEOUT;
      }

    } else {
      if(!manual_reset) state = false;
    }

    pthread_mutex_unlock(&mutex);

    return res;
  }

  bool Event::Set(bool new_state)
  {
    assert(IsValid());

    pthread_mutex_lock(&mutex);

    if(new_state) {
      if(!state) {
        if(!manual_reset) pthread_cond_signal(&condv);
        else {
          pthread_cond_broadcast(&condv);
          state = true;
        }
      }

    } else {
      if(manual_reset) state = false;
    }

    pthread_mutex_unlock(&mutex);

    return true;
  }

  bool Event::Pulse()
  {
    assert(IsValid());

    pthread_mutex_lock(&mutex);

    if(!manual_reset) pthread_cond_signal(&condv);
    else pthread_cond_broadcast(&condv);

    pthread_mutex_unlock(&mutex);

    return true;
  }

  bool Event::Dispose()
  {
    if(!IsValid()) return true;
    else {
      Reset();

      pthread_mutex_destroy(&mutex);
      pthread_cond_destroy(&condv);
      IPCObject::Dispose();

      return true;
    }
  }

}
