#ifndef _ESAJPIP_CONFIG_H_
#define _ESAJPIP_CONFIG_H_

#include <cstdint>
#include <iostream>
#include <string>

class Config {
private:
    int port_ = 0;
    std::string address_;
    std::string image_directory_;
    int max_chunk_size_ = 0;
    int max_connections_ = 0;
    int max_channels_ = 0;
    int initial_timeout_ = 0;
    int connection_timeout_ = 0;
    std::string log_directory_;
    bool file_logging_ = false;
    bool log_requests_ = false;

public:
    bool Load(const char *file_name, std::string &error_message);

    friend std::ostream &operator<<(std::ostream &out, const Config &cfg) {
        out << "Configuration:\n"
            << "\tListen: " << (cfg.address_.empty() ? "*" : cfg.address_)
            << ':' << cfg.port_ << '\n'
            << "\tImages: " << cfg.image_directory_ << '\n'
            << "\tChunk size: " << cfg.max_chunk_size_ << '\n'
            << "\tConnections: " << cfg.max_connections_ << '\n'
            << "\tChannels: " << cfg.max_channels_ << '\n'
            << "\tInitial timeout: " << cfg.initial_timeout_ << '\n'
            << "\tConnection timeout: " << cfg.connection_timeout_ << '\n'
            << "\tLogging: "
            << (cfg.file_logging_ ? cfg.log_directory_ : "standard output") << '\n'
            << "\tRequest logging: " << (cfg.log_requests_ ? "yes" : "no")
            << '\n';
        return out;
    }

    uint16_t port() const {
        return static_cast<uint16_t>(port_);
    }

    const std::string &address() const {
        return address_;
    }

    const std::string &image_directory() const {
        return image_directory_;
    }

    const std::string &log_directory() const {
        return log_directory_;
    }

    int max_chunk_size() const {
        return max_chunk_size_;
    }

    int max_connections() const {
        return max_connections_;
    }

    int max_channels() const {
        return max_channels_;
    }

    int initial_timeout() const {
        return initial_timeout_;
    }

    bool file_logging() const {
        return file_logging_;
    }

    bool log_requests() const {
        return log_requests_;
    }

    int connection_timeout() const {
        return connection_timeout_;
    }

};

#endif /* _ESAJPIP_CONFIG_H_ */
