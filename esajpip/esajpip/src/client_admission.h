#ifndef _CLIENT_ADMISSION_H_
#define _CLIENT_ADMISSION_H_

enum AdmissionResult {
    ADMISSION_PENDING,
    ADMISSION_ACCEPTED,
    ADMISSION_REJECTED
};

AdmissionResult CheckAdmission(int fd);

#endif /* _CLIENT_ADMISSION_H_ */
