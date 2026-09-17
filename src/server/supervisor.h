#ifndef _SUPERVISOR_H_
#define _SUPERVISOR_H_

#include <string>

class Config;

int RunSupervisor(const Config &cfg, int listen_socket,
                  const std::string &log_name,
                  const std::string &description);

#endif /* _SUPERVISOR_H_ */
