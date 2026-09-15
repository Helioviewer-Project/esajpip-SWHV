#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>
#include "channel.h"

using namespace std;

ChannelInbox::ChannelInbox() : wake_socket{-1, -1}, pending(false), closed(false) {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0) {
        wake_socket[0] = sockets[0];
        wake_socket[1] = sockets[1];
    }
}

ChannelInbox::~ChannelInbox() {
    if (wake_socket[0] >= 0)
        close(wake_socket[0]);
    if (wake_socket[1] >= 0)
        close(wake_socket[1]);
}

bool ChannelInbox::IsValid() const {
    return wake_socket[0] >= 0;
}

bool ChannelInbox::IsClosed() {
    lock_guard<std::mutex> lock(mutex);
    return closed;
}

int ChannelInbox::GetDescriptor() const {
    return wake_socket[0];
}

bool ChannelInbox::Push(const ChannelConnection &connection) {
    lock_guard<std::mutex> lock(mutex);
    if (closed || pending || wake_socket[1] < 0)
        return false;
    this->connection = connection;
    pending = true;

    char byte = 0;
    ssize_t sent;
    do {
        sent = send(wake_socket[1], &byte, 1, MSG_DONTWAIT);
    } while (sent < 0 && errno == EINTR);
    if (sent == 1)
        return true;
    pending = false;
    return false;
}

bool ChannelInbox::Pop(ChannelConnection *connection) {
    lock_guard<std::mutex> lock(mutex);
    // Drain under the lock so Push cannot leave a
    // pending connection without a corresponding wake-up.
    Drain();
    if (!pending)
        return false;
    *connection = this->connection;
    pending = false;
    return true;
}

void ChannelInbox::Drain() {
    char byte;
    ssize_t received;
    do {
        received = recv(wake_socket[0], &byte, 1, MSG_DONTWAIT);
    } while (received < 0 && errno == EINTR);
}

bool ChannelInbox::Close(ChannelConnection *connection) {
    lock_guard<std::mutex> lock(mutex);
    bool was_closed = closed;
    closed = true;
    bool had_pending = pending;
    if (pending && connection)
        *connection = this->connection;
    pending = false;

    if (!was_closed && wake_socket[1] >= 0) {
        char byte = 0;
        ssize_t sent;
        do {
            sent = send(wake_socket[1], &byte, 1, MSG_DONTWAIT);
        } while (sent < 0 && errno == EINTR);
    }
    return had_pending;
}
