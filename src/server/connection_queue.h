#ifndef _CONNECTION_QUEUE_H_
#define _CONNECTION_QUEUE_H_

#include <mutex>

class ConnectionQueue {
private:
    int wake_socket[2];
    std::mutex mutex;
    int connection;
    bool pending;
    bool closed;

    void Drain();

public:
    ConnectionQueue();
    ~ConnectionQueue();

    bool IsValid() const;
    bool IsClosed();
    int GetDescriptor() const;
    bool Push(int connection);
    bool Pop(int *connection);
    bool Close(int &connection);

    ConnectionQueue(const ConnectionQueue &) = delete;
    ConnectionQueue &operator=(const ConnectionQueue &) = delete;
};

#endif /* _CONNECTION_QUEUE_H_ */
