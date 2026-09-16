#ifndef _SUPERVISOR_H_
#define _SUPERVISOR_H_

#include <string>

class AppConfig;
class AppInfo;

int RunSupervisor(const AppConfig &cfg, AppInfo &app_info,
                  int listen_socket, const std::string &log_name,
                  const std::string &description);

#endif /* _SUPERVISOR_H_ */
