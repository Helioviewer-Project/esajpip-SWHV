#ifndef _IPC_MUTEX_H_
#define _IPC_MUTEX_H_


#include <pthread.h>
#include "ipc_object.h"


namespace ipc
{

  /**
   * IPC object that offers the functionality of a mutex,
   * implemented by means of the pthread mutex API.
   *
   * @see IPCObject
   */
  class Mutex :public IPCObject
  {
  private:
    pthread_t locker;		///< Id. of the thread that locks the mutex
    pthread_mutex_t mutex;	///< Mutex information

  public:
    /**
     * Pointer to a Mutex object.
     */
    typedef std::tr1::shared_ptr<Mutex> Ptr;

    /**
     * Initializes the object without locking the mutex.
     * @return <code>true</code> if successful.
     */
    virtual bool Init()
    {
      return Init(false);
    }

    /**
     * Initializes the object.
     * @param initial_owner If <code>true</code> the mutex is locked.
     * @return <code>true</code> if successful.
     */
    bool Init(bool initial_owner);

    /**
     * Performs a wait operation with the object to get it.
     * @param time_out Time out (infinite by default).
     * @return <code>WAIT_OBJECT</code> if successful,
     * <code>WAIT_TIMEOUT</code> if time out or <code>
     * WAIT_ERROR</code> is error.
     */
    virtual WaitResult Wait(int time_out = -1);

    /**
     * Release the resources associated to the IPC object and
     * sets the internal status to <code>false</code>.
     * @return <code>true</code> if successful.
     */
    virtual bool Dispose();

    /**
     * Releases/unlocks the mutex.
     * @return <code>true</code> if successful.
     */
    bool Release();
  };

}


#endif /* _IPC_MUTEX_H_ */
