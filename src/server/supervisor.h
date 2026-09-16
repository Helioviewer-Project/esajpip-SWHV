#ifndef _SUPERVISOR_H_
#define _SUPERVISOR_H_

#include <string>

class AppConfig;
class AppInfo;

namespace net {
    class Socket;
}

int RunSupervisor(const AppConfig &cfg, AppInfo &app_info,
                  net::Socket &listen_socket, const std::string &log_name,
                  const std::string &description);

#endif /* _SUPERVISOR_H_ */
