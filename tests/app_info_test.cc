#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

#include "app_info.h"

using namespace std;

#ifndef ESAJPIP_LOCK_FILE
#error ESAJPIP_LOCK_FILE must name the test lock file
#endif

static void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

int main() {
    unlink(ESAJPIP_LOCK_FILE);

    int old_lock = open(ESAJPIP_LOCK_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    Check(old_lock >= 0, "Could not create the old lock file");
    Check(fchmod(old_lock, 0666) == 0, "Could not set the old lock permissions");
    close(old_lock);

    key_t key = ftok(ESAJPIP_LOCK_FILE, 'c');
    Check(key != static_cast<key_t>(-1), "Could not create the shared-memory key");
    int shmid = shmget(key, 4096, IPC_CREAT | IPC_EXCL | 0666);
    Check(shmid >= 0, "Could not create the old shared-memory segment");

    AppInfo owner;
    if (!owner.Init()) {
        cerr << "Could not initialize application status: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    Check(!owner.is_running(), "The first application instance appears to be running");

    struct stat lock_status;
    Check(stat(ESAJPIP_LOCK_FILE, &lock_status) == 0, "Could not inspect the lock file");
    Check((lock_status.st_mode & 0777) == 0600, "The lock file is not private");

    struct shmid_ds shared_status;
    Check(shmctl(shmid, IPC_STAT, &shared_status) == 0,
          "Could not inspect the shared-memory segment");
    Check((shared_status.shm_perm.mode & 0777) == 0600,
          "The shared-memory segment is not private");

    pid_t child = fork();
    Check(child >= 0, "Could not create the status test child");
    if (child == 0) {
        AppInfo observer;
        _exit(observer.Init() && observer.is_running() ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    int status;
    Check(waitpid(child, &status, 0) == child, "Could not wait for the status test child");
    Check(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS,
          "Lock contention was not reported as a running server");

    Check(shmctl(shmid, IPC_RMID, NULL) == 0,
          "Could not remove the test shared-memory segment");
    Check(unlink(ESAJPIP_LOCK_FILE) == 0, "Could not remove the test lock file");
    return EXIT_SUCCESS;
}
