#include <sys/types.h>
#include <sys/socket.h>
#include <cerrno>
#include "socket.h"

namespace net {

    ssize_t Socket::Receive(void *buf, size_t len) {
        return recv(sid, buf, len, 0);
    }

    ssize_t Socket::Send(const void *buf, size_t len) {
        ssize_t sent;
        do {
            sent = send(sid, buf, len, 0);
        } while (sent < 0 && errno == EINTR);
        return sent;
    }

}
