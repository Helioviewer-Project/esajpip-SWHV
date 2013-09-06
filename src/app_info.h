#ifndef _APP_INFO_H_
#define _APP_INFO_H_


#include <assert.h>
#include <iostream>
#include <iomanip>

#ifndef _NO_READPROC
#include <proc/readproc.h>
#endif


using namespace std;


/**
 * Contains the run-time information of the application.
 * This class can be printed.
 */
class AppInfo
{
private:
  /**
   * Contains the data block that is maintained in
   * shared memory.
   */
  struct Data
  {
    int father_pid;			///< PID of the father process
    int child_pid;			///< PID of the child process
    int num_connections;	///< Number of open connections
    int child_iterations;	///< Number of iterations done by the child

    /**
     * Clears the values.
     */
    void Reset()
    {
      father_pid = 0;
      child_pid = 0;
      num_connections = 0;
      child_iterations = 0;
    }
  };

  int shmid;					///< Identifier of the shared memory block
  int lock_file;				///< Lock file
  Data *data_ptr;				///< Pointer to the shared memory block
  bool is_running_;				///< <code>true</code> if the application is running
  int num_threads_;				///< Number of active threads
  double child_memory_;			///< Memory used by the child process
  unsigned long time_;			///< Time spent by the father
  double father_memory_;		///< Memory used by the father process
  double available_memory_;		///< Available memory in the system
  unsigned long child_time_;	///< Time spend by the child

public:
  /**
   * Initializes the object.
   */
  AppInfo()
  {
    shmid = -1;
    lock_file = -1;
    child_memory_ = 0;
    father_memory_ = 0;
    is_running_ = false;
    available_memory_ = 0;
    child_time_ = 0;
    data_ptr = NULL;
    num_threads_ = 0;
    time_ = 0;
  }

  /**
   * Initializes the object and the handling of the application
   * run-time information.
   * @return <code>true</code> if successful.
   */
  bool Init();

  /**
   * Returns <code>true</code> if the application is running.
   */
  bool is_running() const
  {
    return is_running_;
  }

  friend ostream& operator <<(ostream &out, const AppInfo &app)
  {
    out << "Status: " << (app.is_running() ? "running" : "stopped") << endl;
    out << "Available memory: " << setiosflags(ios::fixed) << setprecision(2) << app.available_memory() << " MB" << endl;

    if(app.is_running()) {
      out << "Father PID: " << app->father_pid << endl;
      out << "Child PID: " << app->child_pid << endl;
      out << "Child threads: " << app.num_threads() << endl;
      out << "Child iterations: " << app->child_iterations << endl;
      out << "Num. connections: " << app->num_connections << endl;
      out << "Father used memory: " << setiosflags(ios::fixed) << setprecision(2) << app.father_memory() << " MB" << endl;
      out << "Child used memory: " << setiosflags(ios::fixed) << setprecision(2) << app.child_memory() << " MB" << endl;
    }

    return out;
  }

  /**
   * Updates the run-time information of the application.
   */
  AppInfo& Update();

  /**
   * Returns the available memory of the system.
   */
  double available_memory() const
  {
    return available_memory_;
  }

  /**
   * Returns the memory used by the father process.
   */
  double father_memory() const
  {
    return father_memory_;
  }

  /**
   * Returns the memory used by the child process.
   */
  double child_memory() const
  {
    return child_memory_;
  }

  /**
   * Returns the number of active threads.
   */
  int num_threads() const
  {
    return num_threads_;
  }

  /**
   * Returns the time spent by the child process.
   */
  unsigned long child_time() const
  {
    return child_time_;
  }

  /**
   * Returns the time spent by the father process.
   */
  unsigned long time() const
  {
    return time_;
  }

  Data *operator->() const
  {
    assert(data_ptr);
    return data_ptr;
  }

  ~AppInfo();
};


#endif /* _APP_INFO_H_ */
