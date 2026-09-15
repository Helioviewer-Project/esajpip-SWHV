#ifndef _CONNECTION_ADMISSION_H_
#define _CONNECTION_ADMISSION_H_

#include <string>

enum AdmissionResult {
    ADMISSION_PENDING,
    ADMISSION_ACCEPTED,
    ADMISSION_REJECTED
};

struct Admission {
    AdmissionResult result;
    bool new_channel;
    std::string channel;
};

Admission CheckAdmission(int fd);

#endif /* _CONNECTION_ADMISSION_H_ */
