#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <cstring>
#include <fcntl.h>

#include "app_info.h"

using namespace std;

#define LOCK_FILE "/tmp/esa_jpip_server.lock"

bool AppInfo::Init() {
    struct flock fl;
    memset(&fl, 0, sizeof fl);
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 1;

    bool res = false;
    mode_t prevm = umask(0);

    if ((lock_file = open(LOCK_FILE, O_WRONLY | O_CREAT, 0666)) != -1) {
        is_running_ = (fcntl(lock_file, F_SETLK, &fl) == -1);

        int shmid = shmget(ftok(LOCK_FILE, 'c'), sizeof(Data), IPC_CREAT | 0666);
        if (shmid >= 0) {
            if ((data_ptr = (Data *) shmat(shmid, NULL, 0)) != (Data *) -1) {
                if (!is_running_) data_ptr->Reset();
                res = true;
            }
        }
    }

    umask(prevm);
    return res;
}
