#ifndef _SERVER_SERVER_H_
#define _SERVER_SERVER_H_

#include <string>

class Config;
namespace net { class InetAddress; }

int RunServer(const Config &cfg, const net::InetAddress &listen_address,
              const std::string &log_name, const std::string &description);

#endif /* _SERVER_SERVER_H_ */
