#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>
#include "connection_queue.h"

using namespace std;

ConnectionQueue::ConnectionQueue()
    : wake_socket{-1, -1}, queued_fd(-1), closed(false) {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0) {
        wake_socket[0] = sockets[0];
        wake_socket[1] = sockets[1];
    }
}

ConnectionQueue::~ConnectionQueue() {
    if (wake_socket[0] >= 0)
        close(wake_socket[0]);
    if (wake_socket[1] >= 0)
        close(wake_socket[1]);
}

bool ConnectionQueue::IsValid() const {
    return wake_socket[0] >= 0;
}

bool ConnectionQueue::IsClosed() {
    lock_guard<std::mutex> lock(mutex);
    return closed;
}

int ConnectionQueue::GetDescriptor() const {
    return wake_socket[0];
}

bool ConnectionQueue::Push(int fd) {
    lock_guard<std::mutex> lock(mutex);
    if (closed || queued_fd >= 0 || wake_socket[1] < 0)
        return false;
    queued_fd = fd;

    char byte = 0;
    ssize_t sent;
    do {
        sent = send(wake_socket[1], &byte, 1, MSG_DONTWAIT);
    } while (sent < 0 && errno == EINTR);
    if (sent == 1)
        return true;
    queued_fd = -1;
    return false;
}

bool ConnectionQueue::Pop(int *fd) {
    lock_guard<std::mutex> lock(mutex);
    // Drain under the lock so Push cannot leave a
    // pending connection without a corresponding wake-up.
    Drain();
    if (queued_fd < 0)
        return false;
    *fd = queued_fd;
    queued_fd = -1;
    return true;
}

void ConnectionQueue::Drain() {
    char byte;
    ssize_t received;
    do {
        received = recv(wake_socket[0], &byte, 1, MSG_DONTWAIT);
    } while (received < 0 && errno == EINTR);
}

bool ConnectionQueue::Close(int &fd) {
    lock_guard<std::mutex> lock(mutex);
    closed = true;
    if (queued_fd < 0)
        return false;

    fd = queued_fd;
    queued_fd = -1;
    return true;
}
