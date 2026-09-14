#include <sys/types.h>
#include <sys/socket.h>
#include <cstring>
#include "socket.h"

namespace net {

    ssize_t Socket::Receive(void *buf, size_t len) {
        return recv(sid, buf, len, 0);
    }

    ssize_t Socket::SendTo(const Address &address, const void *buf, size_t len) {
        return sendto(sid, buf, len, 0, address.GetSockAddr(), address.GetSize());
    }

    bool Socket::SendDescriptor(const Address &address, int fd, int aux) {
        msghdr msg;
        cmsghdr *cmsg;
        alignas(cmsghdr) char ccmsg[CMSG_SPACE(sizeof(int))];

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

        memcpy(CMSG_DATA(cmsg), &fd, sizeof fd);

        msg.msg_controllen = cmsg->cmsg_len;
        msg.msg_flags = 0;

        return (sendmsg(sid, &msg, 0) != -1);
    }

    bool Socket::ReceiveDescriptor(int *fd, int *aux) {
        msghdr msg;
        cmsghdr *cmsg;
        alignas(cmsghdr) char ccmsg[CMSG_SPACE(sizeof(int))];

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

        memcpy(fd, CMSG_DATA(cmsg), sizeof *fd);

        return true;
    }

}
