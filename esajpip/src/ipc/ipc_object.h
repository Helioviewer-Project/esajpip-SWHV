#ifndef _IPC_OBJECT_H_
#define _IPC_OBJECT_H_


#ifndef _USE_BOOST
	#include <tr1/memory>
#else
	#include <boost/tr1/memory.hpp>
#endif


namespace ipc
{

  /**
   * Enumeration of the possible values returned
   * when a wait operation is performed for an IPC
   * object.
   */
  enum WaitResult
  {
    WAIT_OBJECT = 0,	///< Wait successful (object got)
    WAIT_TIMEOUT,		///< Time out
    WAIT_ERROR			///< Error
  };


  /**
   * Class base of all the IPC classes that has the basic
   * operations (<code>Init</code>, <code>Wait</code> and
   * <code>Dispose</code>) to be overloaded. It has also
   * an internal boolean value to set the object status.
   *
   * For the IPC objects the Windows IPC philosophy has
   * been adopted because of its simplicity and flexibility.
   * Under this philosophy, the main operation that can be
   * performed to an IPC object is <code>Wait</code>, to wait
   * for getting the control of the object. Depending on
   * the type of the IPC object (mutex, event, etc.), the
   * meaning of "getting" the control of the object can be
   * different.
   */
  class IPCObject
  {
  private:
	/**
	 * Internal status of the object. As it is a private
	 * member, the derived classes must use the methods
	 * <code>Init</code> and <code>Dispose</code> to set
	 * the value of this status.
	 */
    bool valid;

  public:
    /**
     * Pointer to an IPC object.
     */
    typedef std::tr1::shared_ptr<IPCObject> Ptr;

    /**
     * Initializes the internal status to <code>false</code>.
     */
    IPCObject()
    {
      valid = false;
    }

    /**
     * Sets the internal status to <code>true</code>
     * @return <code>true</code> if successful. If it
     * is not overloaded, it always returns <code>true
     * </code>.
     */
    virtual bool Init()
    {
      valid = true;
      return true;
    }

    /**
     * Performs a wait operation with the object to get it.
     * @param time_out Time out (infinite by default).
     * @return <code>WAIT_OBJECT</code> if successful,
     * <code>WAIT_TIMEOUT</code> if time out or <code>
     * WAIT_ERROR</code> is error. If it is not overloaded,
     * it always returns <code>WAIT_ERROR</code>.
     */
    virtual WaitResult Wait(int time_out = -1)
    {
      return WAIT_ERROR;
    }

    /**
     * Returns <code>true</code> if the object is valid,
     * that is, the internal status value is <code>true</code>.
     */
    bool IsValid()
    {
      return valid;
    }

    /**
     * Release the resources associated to the IPC object and
     * sets the internal status to <code>false</code>.
     * @return <code>true</code> if successful. If it is not
     * overloaded, it always returns <code>true</code>.
     */
    virtual bool Dispose()
    {
      valid = false;
      return true;
    }

    /**
     * The desctructor calls the method <code>Dispose</code>.
     */
    virtual ~IPCObject()
    {
      Dispose();
    }
  };

}


#endif /* _IPC_OBJECT_H_ */
