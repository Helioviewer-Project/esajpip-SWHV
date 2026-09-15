#include "trace.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <time.h>
#include <unistd.h>

using namespace std;

namespace {

#ifndef ESAJPIP_LOG_LIMIT
const size_t LOG_LIMIT = 1024ULL * 1024 * 1024;
#else
const size_t LOG_LIMIT = ESAJPIP_LOG_LIMIT;
#endif

const size_t MAX_MESSAGE = 1900;
const int QUEUE_SIZE = 64 * 1024;

struct RecordHeader {
    int64_t seconds;
    int32_t microseconds;
    bool truncated;
};

int read_fd = -1;
int write_fd = -1;
int output_fd = STDOUT_FILENO;
bool file_output = false;
size_t output_size = 0;
string output_name;
atomic<unsigned long> dropped(0);

bool SetNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void CloseQueue() {
    close(read_fd);
    close(write_fd);
    read_fd = -1;
    write_fd = -1;
}

bool Send(const string &message) {
    if (write_fd < 0)
        return false;

    timeval now;
    gettimeofday(&now, NULL);
    RecordHeader header = {};
    header.seconds = now.tv_sec;
    header.microseconds = static_cast<int32_t>(now.tv_usec);
    header.truncated = message.size() > MAX_MESSAGE;
    size_t length = min(message.size(), MAX_MESSAGE);
    iovec buffers[] = {
        {&header, sizeof header},
        {const_cast<char *>(message.data()), length}
    };
    msghdr packet;
    memset(&packet, 0, sizeof packet);
    packet.msg_iov = buffers;
    packet.msg_iovlen = 2;

    ssize_t sent;
    do {
        sent = sendmsg(write_fd, &packet, MSG_DONTWAIT);
    } while (sent < 0 && errno == EINTR);
    return sent == static_cast<ssize_t>(sizeof header + length);
}

bool WriteAll(int fd, const char *data, size_t length) {
    while (length > 0) {
        ssize_t written = write(fd, data, length);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return false;
        data += written;
        length -= written;
    }
    return true;
}

bool Rotate() {
    close(output_fd);
    string backup = output_name + ".1";
    unlink(backup.c_str());
    if (rename(output_name.c_str(), backup.c_str()) != 0)
        return false;
    output_fd = open(output_name.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    output_size = 0;
    return output_fd >= 0;
}

}

bool TraceSystem::Initialize(const string &file_name) {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) != 0)
        return false;
    read_fd = sockets[0];
    write_fd = sockets[1];
    if (!SetNonBlocking(read_fd) || !SetNonBlocking(write_fd) ||
        setsockopt(read_fd, SOL_SOCKET, SO_RCVBUF, &QUEUE_SIZE, sizeof QUEUE_SIZE) != 0 ||
        setsockopt(write_fd, SOL_SOCKET, SO_SNDBUF, &QUEUE_SIZE, sizeof QUEUE_SIZE) != 0) {
        CloseQueue();
        return false;
    }

    if (file_name.empty())
        return true;

    char timestamp[20];
    time_t now = time(NULL);
    tm local;
    if (!localtime_r(&now, &local) ||
        !strftime(timestamp, sizeof timestamp, "%Y%m%d.%H%M%S", &local)) {
        CloseQueue();
        return false;
    }

    output_name = file_name + "." + timestamp + ".log";
    output_fd = open(output_name.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);
    if (output_fd < 0) {
        CloseQueue();
        return false;
    }

    struct stat status;
    if (fstat(output_fd, &status) != 0) {
        close(output_fd);
        output_fd = STDOUT_FILENO;
        CloseQueue();
        return false;
    }
    output_size = status.st_size;
    file_output = true;
    return true;
}

int TraceSystem::ReadDescriptor() {
    return read_fd;
}

void TraceSystem::CloseParentDescriptors() {
    if (read_fd >= 0) {
        close(read_fd);
        read_fd = -1;
    }
    if (file_output && output_fd >= 0) {
        close(output_fd);
        output_fd = -1;
    }
}

void TraceSystem::Write(const string &message) {
    unsigned long count = dropped.exchange(0);
    if (count != 0) {
        ostringstream summary;
        summary << count << " log message" << (count == 1 ? "" : "s") << " dropped";
        if (!Send(summary.str())) {
            dropped.fetch_add(count + 1);
            return;
        }
    }
    if (!Send(message))
        dropped.fetch_add(1);
}

bool TraceSystem::DrainOne() {
    char packet[sizeof(RecordHeader) + MAX_MESSAGE];
    ssize_t length;
    do {
        length = recv(read_fd, packet, sizeof packet, MSG_DONTWAIT);
    } while (length < 0 && errno == EINTR);
    if (length < static_cast<ssize_t>(sizeof(RecordHeader)))
        return false;

    RecordHeader header;
    memcpy(&header, packet, sizeof header);
    time_t seconds = header.seconds;
    tm local;
    char timestamp[25];
    if (!localtime_r(&seconds, &local) ||
        !strftime(timestamp, sizeof timestamp, "%Y-%m-%d %H:%M:%S", &local))
        return false;

    char prefix[32];
    int prefix_length = snprintf(prefix, sizeof prefix, "%s,%03d: ", timestamp,
                                 header.microseconds / 1000);
    if (prefix_length < 0 || static_cast<size_t>(prefix_length) >= sizeof prefix)
        return false;

    string line(prefix, prefix_length);
    line.append(packet + sizeof header, length - sizeof header);
    if (header.truncated)
        line += "...";
    line += " \n";
    if (!WriteAll(output_fd, line.data(), line.size()))
        return false;

    if (file_output) {
        output_size += line.size();
        if (output_size >= LOG_LIMIT)
            return Rotate();
    }
    return true;
}
