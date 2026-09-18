#ifndef _SERVER_INITIAL_REQUEST_H_
#define _SERVER_INITIAL_REQUEST_H_

#include <cstddef>
#include <string>

enum RequestState {
    REQUEST_PENDING,
    REQUEST_ACCEPTED,
    REQUEST_REJECTED
};

struct InitialRequest {
    RequestState state;
    bool new_channel;
    bool tid;
    bool handled;
    std::string channel;
};

InitialRequest ClassifyInitialRequest(const char *data, std::size_t length);
InitialRequest InspectInitialRequest(int fd);

#endif /* _SERVER_INITIAL_REQUEST_H_ */
