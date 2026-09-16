#ifndef _SERVER_H_
#define _SERVER_H_

#include <string>

class AppConfig;

const int SERVER_STARTUP_FAILURE = 2;

int RunServer(const AppConfig &cfg, int listen_socket, int supervisor_fd,
              const std::string &log_name, const std::string &description,
              const std::string &restart_message);

#endif /* _SERVER_H_ */
