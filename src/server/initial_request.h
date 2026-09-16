#ifndef _INITIAL_REQUEST_H_
#define _INITIAL_REQUEST_H_

#include <cstdint>

enum RequestState {
    REQUEST_PENDING,
    REQUEST_ACCEPTED,
    REQUEST_REJECTED
};

struct InitialRequest {
    RequestState state;
    bool new_channel;
    uint64_t channel;
};

InitialRequest InspectInitialRequest(int fd);

#endif /* _INITIAL_REQUEST_H_ */
