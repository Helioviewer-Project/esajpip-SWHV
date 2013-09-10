#ifndef _IPC_RDWR_LOCK_H_
#define _IPC_RDWR_LOCK_H_


#include <pthread.h>
#include "ipc_object.h"


namespace ipc
{

  /**
   * IPC object that offers the functionality of a read/write
   * lock, implemented by means of the pthread rwlock API.
   *
   * @see IPCObject
   */
  class RdWrLock :public IPCObject
  {
  private:
    pthread_rwlock_t rwlock;	///< Read/write lock information

  public:
    /**
     * Pointer to a RdWrLock object.
     */
    typedef std::tr1::shared_ptr<RdWrLock> Ptr;

    /**
     * Initializes the object.
     * @return <code>true</code> if successful.
     */
    virtual bool Init();

    /**
     * Performs a wait operation with the object to get it
     * for reading.
     * @param time_out Time out (infinite by default).
     * @return <code>WAIT_OBJECT</code> if successful,
     * <code>WAIT_TIMEOUT</code> if time out or <code>
     * WAIT_ERROR</code> is error.
     */
    virtual WaitResult Wait(int time_out = -1);

    /**
     * Performs a wait operation with the object to get it
     * for writing.
     * @param time_out Time out (infinite by default).
     * @return <code>WAIT_OBJECT</code> if successful,
     * <code>WAIT_TIMEOUT</code> if time out or <code>
     * WAIT_ERROR</code> is error.
     */
    WaitResult WaitForWriting(int time_out = -1);

    /**
     * Release the resources associated to the IPC object and
     * sets the internal status to <code>false</code>.
     * @return <code>true</code> if successful.
     */
    virtual bool Dispose();

    /**
     * Releases the lock.
     * @return <code>true</code> if successful.
     */
    bool Release();
  };

}

#endif /* _IPC_RDWR_LOCK_H_ */
