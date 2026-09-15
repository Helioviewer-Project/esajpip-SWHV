#ifndef _APP_INFO_H_
#define _APP_INFO_H_

#include <cassert>
#include <iostream>

using namespace std;

/**
 * Contains the run-time information of the application.
 * This class can be printed.
 */
class AppInfo {
private:
    /**
     * Contains the data block that is maintained in
     * shared memory.
     */
    struct Data {
        int parent_pid;            ///< PID of the parent process
        int child_pid;            ///< PID of the child process
        int num_connections;    ///< Number of open connections

        /**
         * Clears the values.
         */
        void Reset() {
            parent_pid = 0;
            child_pid = 0;
            num_connections = 0;
        }
    };

    int lock_file;                ///< Lock file
    Data *data_ptr;                ///< Pointer to the shared memory block
    bool is_running_;                ///< <code>true</code> if the application is running

public:
    /**
     * Initializes the object.
     */
    AppInfo() {
        lock_file = -1;
        is_running_ = false;
        data_ptr = NULL;
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
    bool is_running() const {
        return is_running_;
    }

    friend ostream &operator<<(ostream &out, const AppInfo &app) {
        out << "Status: " << (app.is_running() ? "running" : "stopped") << endl;

        if (app.is_running()) {
            out << "Parent PID: " << app->parent_pid << endl;
            out << "Child PID: " << app->child_pid << endl;
            out << "Num. connections: " << app->num_connections << endl;
        }

        return out;
    }

    Data *operator->() const {
        assert(data_ptr);
        return data_ptr;
    }
};

#endif /* _APP_INFO_H_ */
