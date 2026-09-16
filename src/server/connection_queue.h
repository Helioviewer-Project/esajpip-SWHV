#ifndef _CONNECTION_QUEUE_H_
#define _CONNECTION_QUEUE_H_

#include <mutex>

struct ChannelConnection {
    int fd;
};

class ConnectionQueue {
private:
    int wake_socket[2];
    std::mutex mutex;
    ChannelConnection connection;
    bool pending;
    bool closed;

    void Drain();

public:
    ConnectionQueue();
    ~ConnectionQueue();

    bool IsValid() const;
    bool IsClosed();
    int GetDescriptor() const;
    bool Push(const ChannelConnection &connection);
    bool Pop(ChannelConnection *connection);
    bool Close(ChannelConnection &connection);

    ConnectionQueue(const ConnectionQueue &) = delete;
    ConnectionQueue &operator=(const ConnectionQueue &) = delete;
};

#endif /* _CONNECTION_QUEUE_H_ */
