#include <sys/types.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstring>
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

    bool Socket::SendDescriptor(int fd, uint64_t connection_id) {
        msghdr msg;
        alignas(cmsghdr) char ccmsg[CMSG_SPACE(sizeof(int))];
        memset(ccmsg, 0, sizeof ccmsg);

        struct iovec iov;
        memset(&iov, 0, sizeof iov);
        iov.iov_base = &connection_id;
        iov.iov_len = sizeof connection_id;

        memset(&msg, 0, sizeof msg);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = ccmsg;
        msg.msg_controllen = CMSG_SPACE(sizeof(int));

        cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_len = CMSG_LEN(sizeof(fd));

        memcpy(CMSG_DATA(cmsg), &fd, sizeof fd);

        msg.msg_controllen = sizeof ccmsg;
        msg.msg_flags = 0;

        ssize_t sent;
        do {
            sent = sendmsg(sid, &msg, 0);
        } while (sent < 0 && errno == EINTR);
        return sent == static_cast<ssize_t>(sizeof connection_id);
    }

    bool Socket::ReceiveDescriptor(int *fd, uint64_t *connection_id) {
        msghdr msg;
        alignas(cmsghdr) char ccmsg[CMSG_SPACE(sizeof(int))];
        memset(ccmsg, 0, sizeof ccmsg);

        iovec iov;
        memset(&iov, 0, sizeof iov);
        iov.iov_base = connection_id;
        iov.iov_len = sizeof *connection_id;

        memset(&msg, 0, sizeof msg);
        msg.msg_name = 0;
        msg.msg_namelen = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = ccmsg;
        msg.msg_controllen = CMSG_SPACE(sizeof(int));

        *fd = -1;
        ssize_t received;
        do {
            received = recvmsg(sid, &msg, 0);
        } while (received < 0 && errno == EINTR);

        cmsghdr *cmsg = received > 0 ? CMSG_FIRSTHDR(&msg) : NULL;
        if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len >= CMSG_LEN(sizeof *fd))
            memcpy(fd, CMSG_DATA(cmsg), sizeof *fd);

        bool valid = received == static_cast<ssize_t>(sizeof *connection_id) &&
                     !(msg.msg_flags & (MSG_CTRUNC | MSG_TRUNC)) && *fd >= 0 &&
                     cmsg && cmsg->cmsg_len == CMSG_LEN(sizeof *fd) &&
                     CMSG_NXTHDR(&msg, cmsg) == NULL;
        if (!valid && *fd >= 0) {
            shutdown(*fd, SHUT_RDWR);
            close(*fd);
            *fd = -1;
        }
        if (!valid && received >= 0)
            errno = EBADMSG;
        return valid;
    }

}
