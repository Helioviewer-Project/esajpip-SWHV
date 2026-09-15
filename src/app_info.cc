#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "app_info.h"

using namespace std;

#ifndef ESAJPIP_LOCK_FILE
#define ESAJPIP_LOCK_FILE "/tmp/esa_jpip_server.lock"
#endif

bool AppInfo::Init() {
    struct flock fl;
    memset(&fl, 0, sizeof fl);
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 1;

    lock_file = open(ESAJPIP_LOCK_FILE, O_WRONLY | O_CREAT, 0600);
    if (lock_file < 0)
        return false;

    struct stat lock_status;
    if (fstat(lock_file, &lock_status) != 0 || !S_ISREG(lock_status.st_mode) ||
        lock_status.st_uid != geteuid() || fchmod(lock_file, 0600) != 0) {
        close(lock_file);
        lock_file = -1;
        return false;
    }

    if (fcntl(lock_file, F_SETLK, &fl) != 0) {
        if (errno != EACCES && errno != EAGAIN) {
            close(lock_file);
            lock_file = -1;
            return false;
        }
        is_running_ = true;
    }

    key_t key = ftok(ESAJPIP_LOCK_FILE, 'c');
    if (key == static_cast<key_t>(-1)) {
        close(lock_file);
        lock_file = -1;
        return false;
    }

    int shmid = shmget(key, sizeof(Data), IPC_CREAT | 0600);
    struct shmid_ds shared_status;
    if (shmid < 0 || shmctl(shmid, IPC_STAT, &shared_status) != 0 ||
        shared_status.shm_perm.uid != geteuid()) {
        close(lock_file);
        lock_file = -1;
        return false;
    }

    if ((shared_status.shm_perm.mode & 0777) != 0600) {
        shared_status.shm_perm.mode =
            (shared_status.shm_perm.mode & ~0777) | 0600;
        if (shmctl(shmid, IPC_SET, &shared_status) != 0) {
            close(lock_file);
            lock_file = -1;
            return false;
        }
    }

    void *address = shmat(shmid, NULL, 0);
    if (address == reinterpret_cast<void *>(-1)) {
        close(lock_file);
        lock_file = -1;
        return false;
    }
    data_ptr = static_cast<Data *>(address);
    if (!is_running_)
        data_ptr->Reset();
    return true;
}
