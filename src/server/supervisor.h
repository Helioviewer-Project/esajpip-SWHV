#ifndef _SERVER_SUPERVISOR_H_
#define _SERVER_SUPERVISOR_H_

#include <string>

class Config;

int RunSupervisor(const Config &cfg, int listen_socket,
                  const std::string &log_name,
                  const std::string &description);

#endif /* _SERVER_SUPERVISOR_H_ */
