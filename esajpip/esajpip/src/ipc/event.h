#ifndef _IPC_EVENT_H_
#define _IPC_EVENT_H_


#include <pthread.h>
#include "ipc_object.h"


namespace ipc
{

  /**
   * IPC object that offers the functionality of an event
   * (Windows IPC object), implemented by means of a
   * combination of the pthread mutex and conditional
   * variables API.
   *
   * @see IPCObject
   */
  class Event :public IPCObject
  {
  private:
    bool state;				///< Current activation state of the event
    bool manual_reset;		///< Indicates if the event reset is manual
    pthread_cond_t condv;	///< Conditional variable information
    pthread_mutex_t mutex;	///< Mutex information

  public:
    /**
     * Pointer to a Event object.
     */
    typedef std::tr1::shared_ptr<Event> Ptr;

    /**
     * Initializes the object desactivated and with automatic reset.
     * @return <code>true</code> if successful.
     */
    virtual bool Init()
    {
      return Init(false);
    }

    /**
     * Initializes the object.
     * @param manual_reset <code>true</code> if the reset is manual.
     * @param initial_state <code>true</code> if the initial state is
     * activated.
     * @return <code>true</code> if successful.
     */
    bool Init(bool manual_reset, bool initial_state = false);

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
     * Sets the state of the object. If it is activated
     * (with <code>true</code>) and the reset is manual,
     * all the threads waiting for the object will be
     * resumed. If the reset is not manual (automatic),
     * only one thread will be resumed and the state will
     * be set to <code>false</code> again.
     * @param new_state New state of the object.
     * @return <code>true</code> if successful.
     */
    bool Set(bool new_state = true);

    /**
     * Returns the current activation state of the object.
     */
    bool Get() const
    {
      return state;
    }

    /**
     * Generates the same result as if the event has automatic
     * reset and the <code>Set</code> method is called with
     * <code>true</code>, independently of the real reset type.
     * @return <code>true</code> if successful.
     */
    bool Pulse();

    /**
     * Desactivates the object.
     * @return <code>true</code> if successful.
     */
    bool Reset()
    {
      return Set(false);
    }
  };

}


#endif /* _IPC_EVENT_H_ */
