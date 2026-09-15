#ifndef _PARENT_H_
#define _PARENT_H_

class AppConfig;
class AppInfo;

namespace net {
    class Socket;
}

int RunParent(const AppConfig &cfg, AppInfo &app_info, net::Socket &listen_socket);

#endif /* _PARENT_H_ */
