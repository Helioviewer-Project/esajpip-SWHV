#ifndef _CHANNEL_INBOX_H_
#define _CHANNEL_INBOX_H_

#include <cstdint>
#include <mutex>

struct ChannelConnection {
    uint64_t id;
    int fd;
};

class ChannelInbox {
private:
    int wake_socket[2];
    std::mutex mutex;
    ChannelConnection connection;
    bool pending;
    bool closed;

    void Drain();

public:
    ChannelInbox();
    ~ChannelInbox();

    bool IsValid() const;
    bool IsClosed();
    int GetDescriptor() const;
    bool Push(const ChannelConnection &connection);
    bool Pop(ChannelConnection *connection);
    bool Close(ChannelConnection &connection);

    ChannelInbox(const ChannelInbox &) = delete;
    ChannelInbox &operator=(const ChannelInbox &) = delete;
};

#endif /* _CHANNEL_INBOX_H_ */
