#ifndef _CHILD_H_
#define _CHILD_H_

class AppConfig;

namespace net {
    class UnixAddress;
}

int RunChild(const AppConfig &cfg, int parent_fd,
             net::UnixAddress &child_address,
             const net::UnixAddress &parent_address);

#endif /* _CHILD_H_ */
