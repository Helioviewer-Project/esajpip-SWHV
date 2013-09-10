#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>

#ifndef _SC_AVPHYS_PAGES
#include <sys/sysctl.h>
#endif

#include "app_info.h"


using namespace std;


#define LOCK_FILE "/tmp/esa_jpip_server.lock"


bool AppInfo::Init()
{
  struct flock fl;

  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 1;

  bool res = false;
  mode_t prevm = umask(0);

  if((lock_file = open(LOCK_FILE, O_WRONLY | O_CREAT, 0666)) != -1) {
	  is_running_ = (fcntl(lock_file, F_SETLK, &fl) == -1);

	  if((shmid = shmget(ftok(LOCK_FILE, 'c'), sizeof(Data), IPC_CREAT | 0666)) >= 0) {
		  if((data_ptr = (Data *)shmat(shmid, NULL, 0)) != (Data *)-1) {
		    if(!is_running_) data_ptr->Reset();
		    res = true;
		  }
	  }
  }

  umask(prevm);
  return res;
}

AppInfo& AppInfo::Update()
{
  #if defined(_SC_AVPHYS_PAGES)
  {
    long page_size = sysconf(_SC_PAGE_SIZE);
    long avpages = sysconf(_SC_AVPHYS_PAGES);
    available_memory_ = (avpages * page_size / (1024.0 * 1024.0));
  }
  #elif defined(HW_PHYSMEM)
  { 
    unsigned int physmem;
    size_t len = sizeof(physmem);
    static int mib[2] = { CTL_HW, HW_PHYSMEM };

    sysctl(mib, sizeof(mib) * sizeof(mib[0]), &physmem, &len, NULL, 0);
    available_memory_ = (double) physmem;
  }
  #endif

  {
    is_running_ = false;

    int lock;
    struct flock fl;

    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 1;

    if((lock = open(LOCK_FILE, O_RDONLY, 0666)) != -1)
    {
      if(fcntl(lock, F_GETLK, &fl) != -1)
        is_running_ = (fl.l_type != F_UNLCK);

      close(lock);
    }
  }

  if(!is_running_) {
    father_memory_ = 0;
    child_memory_ = 0;
    child_time_ = 0;
    num_threads_ = 0;

  } else {
  	#ifndef _NO_READPROC
    	struct proc_t usage;

    	get_proc_stats(data_ptr->father_pid, &usage);
    	father_memory_ = ((double)usage.size / (1024.0 * 1024.0));

    	get_proc_stats(data_ptr->child_pid, &usage);
    	child_memory_ = (double)(usage.rss) * 4096.0 / (1024 * 1024);
    	child_time_ = usage.utime + usage.stime;

    	num_threads_ = usage.nlwp - 1;
    	if(num_threads_ < 0) num_threads_ = 0;
		#endif
  }

  {
    time_ = 0;

    string line, cpu;
    unsigned long t1, t2;
    ifstream fin("/proc/stat");

    if(getline(fin, line)) {
      istringstream str(line);

      if(str >> cpu >> t1 >> t2) {
        time_ = t1 * t2;
        while(str >> t2) time_ += t2;
      }
    }
  }

  return *this;
}

AppInfo::~AppInfo()
{
}
