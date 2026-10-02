#pragma once

#include <string>

namespace server {

class Config;

// Serve until SIGINT or SIGTERM. An empty log name logs to standard output.
int RunServer(const Config &cfg, const std::string &log_name,
              const std::string &description, unsigned int io_threads,
              unsigned int open_threads);

} // namespace server
