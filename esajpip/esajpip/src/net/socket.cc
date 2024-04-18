#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <cassert>
#include <cstring>
#include "socket.h"
#include "poll_table.h"

#ifndef POLLRDHUP
#define POLLRDHUP         (0)
#endif

namespace net {

    bool Socket::SetBlockingMode(bool state) {
        int cur = fcntl(sid, F_GETFL, 0);

        if (state) cur |= O_NONBLOCK;
        else if (cur & O_NONBLOCK)
            cur ^= O_NONBLOCK;

        return fcntl(sid, F_SETFL, cur) != -1;
    }

    bool Socket::IsBlockingMode() {
        int cur = fcntl(sid, F_GETFL, 0);
        return (bool) (cur & O_NONBLOCK);
    }

    bool Socket::IsValid() {
        PollFD poll_fd(sid, POLLRDHUP | POLLERR | POLLHUP | POLLNVAL);
        return (poll(&poll_fd, 1, 0) == 0);
    }

    int Socket::WaitForInput(int time_out) {
        PollFD poll_fd(sid, POLLIN);
        return poll(&poll_fd, 1, time_out);
    }

    int Socket::WaitForOutput(int time_out) {
        PollFD poll_fd(sid, POLLOUT);
        return poll(&poll_fd, 1, time_out);
    }

    ssize_t Socket::Receive(void *buf, size_t len, bool prevent_block) {
        if (!prevent_block)
            return recv(sid, buf, len, 0);

        PollFD poll_fd(sid, POLLIN);
        if (poll(&poll_fd, 1, 0) <= 0) return -1;
        return recv(sid, buf, len, 0);
    }

    ssize_t Socket::ReceiveFrom(Address *address, void *buf, size_t len, bool prevent_block) {
        socklen_t sock_len = address->GetSize();

        if (!prevent_block)
            return recvfrom(sid, buf, len, 0, address->GetSockAddr(), &sock_len);

        PollFD poll_fd(sid, POLLIN);
        if (poll(&poll_fd, 1, 0) <= 0) return -1;
        return recvfrom(sid, buf, len, 0, address->GetSockAddr(), &sock_len);
    }

    ssize_t Socket::Send(const void *buf, size_t len, bool prevent_block) {
        if (!prevent_block)
            return send(sid, buf, len, 0);

        PollFD poll_fd(sid, POLLOUT);
        if (poll(&poll_fd, 1, 0) <= 0) return -1;
        return send(sid, buf, len, 0);
    }

    ssize_t Socket::SendTo(const Address &address, const void *buf, size_t len, bool prevent_block) {
        if (!prevent_block)
            return sendto(sid, buf, len, 0, address.GetSockAddr(), address.GetSize());

        PollFD poll_fd(sid, POLLOUT);
        if (poll(&poll_fd, 1, 0) <= 0) return -1;
        return sendto(sid, buf, len, 0, address.GetSockAddr(), address.GetSize());
    }

    bool Socket::SendDescriptor(const Address &address, int fd, int aux) {
        msghdr msg;
        cmsghdr *cmsg;
        char ccmsg[50];

        assert(sizeof(ccmsg) >= CMSG_SPACE(sizeof(int)));

        struct iovec iov;
        memset(&iov, 0, sizeof iov);
        iov.iov_base = &aux;
        iov.iov_len = sizeof(aux);

        memset(&msg, 0, sizeof msg);
        msg.msg_name = address.GetSockAddr();
        msg.msg_namelen = address.GetSize();
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = ccmsg;
        msg.msg_controllen = CMSG_SPACE(sizeof(int));

        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_len = CMSG_LEN(sizeof(fd));

        *(int *) CMSG_DATA(cmsg) = fd;

        msg.msg_controllen = cmsg->cmsg_len;
        msg.msg_flags = 0;

        return (sendmsg(sid, &msg, 0) != -1);
    }

    bool Socket::ReceiveDescriptor(int *fd, int *aux) {
        msghdr msg;
        cmsghdr *cmsg;
        char ccmsg[50];

        assert(sizeof(ccmsg) >= CMSG_SPACE(sizeof(int)));

        int aux2;
        iovec iov;
        memset(&iov, 0, sizeof iov);
        if (aux) {
            iov.iov_base = aux;
            iov.iov_len = sizeof(*aux);
        } else {
            iov.iov_base = &aux2;
            iov.iov_len = sizeof(aux2);
        }

        memset(&msg, 0, sizeof msg);
        msg.msg_name = 0;
        msg.msg_namelen = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = ccmsg;
        msg.msg_controllen = CMSG_SPACE(sizeof(int));

        if (recvmsg(sid, &msg, 0) <= 0)
            return false;

        cmsg = CMSG_FIRSTHDR(&msg);
        if (!cmsg)
            return false;

        if (cmsg->cmsg_type != SCM_RIGHTS)
            return false;

        *fd = *(int *) CMSG_DATA(cmsg);
        return true;
    }

}
