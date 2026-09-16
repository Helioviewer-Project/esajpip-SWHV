#ifndef _CRASH_REPORT_H_
#define _CRASH_REPORT_H_

#include <cstdint>

namespace crash_report {

bool Initialize(int fd);
void SetChannel(uint64_t channel);

}

#endif /* _CRASH_REPORT_H_ */
