#ifndef _CONNECTION_QUEUE_H_
#define _CONNECTION_QUEUE_H_

#include <mutex>

class ConnectionQueue {
private:
    int wake_socket[2];
    std::mutex mutex;
    int queued_fd;
    bool closed;

    void Drain();

public:
    ConnectionQueue();
    ~ConnectionQueue();

    bool IsValid() const;
    bool IsClosed();
    int GetDescriptor() const;
    bool Push(int fd);
    bool Pop(int *fd);
    bool Close(int &fd);

    ConnectionQueue(const ConnectionQueue &) = delete;
    ConnectionQueue &operator=(const ConnectionQueue &) = delete;
};

#endif /* _CONNECTION_QUEUE_H_ */
