#pragma once

#include <string>

namespace server {

class Config;
class InetAddress;

int RunServer(const Config &cfg, const server::InetAddress &listen_address,
              const std::string &log_name, const std::string &description,
              unsigned int worker_threads);

} // namespace server
