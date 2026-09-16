#ifndef _NET_POLL_TABLE_H_
#define _NET_POLL_TABLE_H_

#include <poll.h>
#include <vector>

namespace net {
    class PollTable {
    private:
        std::vector<pollfd> fds;

    public:
        void Add(int fd, int events) {
            pollfd item = {fd, static_cast<short>(events), 0};
            fds.push_back(item);
        }

        int Poll(int timeout = -1) {
            return poll(fds.data(), fds.size(), timeout);
        }

        int GetSize() const {
            return fds.size();
        }

        void RemoveAt(int position) {
            fds.erase(fds.begin() + position);
        }

        pollfd &operator[](int position) {
            return fds[position];
        }
    };
}

#endif /* _NET_POLL_TABLE_H_ */
