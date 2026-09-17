#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <cstdint>
#include <string>
#include <iostream>

/**
 * Contains the configuration parameters of the
 * application. It is possible to load these
 * parameters from a configuration file. This
 * class can be printed.
 */
class Config {
private:
    int port_;                ///< Listening port
    int file_logging_;                ///< <code>true</code> if file logging is enabled
    int log_requests_;  ///< <code>true</code> if the client requests are logged
    std::string address_;            ///< Listening address
    std::string image_directory_;    ///< Directory containing the images
    std::string log_directory_;    ///< Directory for log files
    int max_chunk_size_;        ///< Maximum chunk size
    int max_connections_;        ///< Maximum number of connections
    int initial_timeout_; ///< Initial request timeout
    int connection_timeout_;        ///< Connection timeout

public:
    /**
     * Initializes the object with zero and empty values.
     */
    Config() {
        port_ = 0;
        file_logging_ = 0;
        address_ = "";
        log_requests_ = 0;
        image_directory_ = "";
        log_directory_ = "";
        max_chunk_size_ = 0;
        max_connections_ = 0;
        initial_timeout_ = 3;
        connection_timeout_ = -1;
    }

    /**
     * Loads the parameters from a configuration file.
     * @param file_name Configuration file.
     * @param error_message Description of a load or validation failure.
     * @return <code>true</code> if successful.
     */
    bool Load(const char *file_name, std::string &error_message);

    friend std::ostream &operator<<(std::ostream &out, const Config &cfg) {
        out << "Configuration:" << std::endl;
        out << "\tListen at: " << cfg.address_ << ":" << cfg.port_ << std::endl;
        out << "\tDirectories:" << std::endl;
        out << "\t\tImages: " << cfg.image_directory_ << std::endl;
        out << "\t\tLogging: " << cfg.log_directory_ << std::endl;
        out << "\tConnections: " << std::endl;
        out << "\t\tLimit: " << cfg.max_connections_ << std::endl;
        out << "\t\tInitial timeout: " << cfg.initial_timeout_ << std::endl;
        out << "\t\tTimeout: " << cfg.connection_timeout() << std::endl;
        out << "\tGeneral:" << std::endl;
        out << "\t\tFile: " << (cfg.file_logging_ == 1 ? "yes" : "no") << std::endl;
        out << "\t\tLog. requests: " << (cfg.log_requests_ == 1 ? "yes" : "no") << std::endl;
        out << "\t\tChunk size: " << cfg.max_chunk_size_ << std::endl;
        return out;
    }

    /**
     * Returns the listening port.
     */
    uint16_t port() const {
        return (uint16_t) port_;
    }

    /**
     * Returns the listening address.
     */
    const std::string &address() const {
        return address_;
    }

    /**
     * Returns the image directory.
     */
    const std::string &image_directory() const {
        return image_directory_;
    }

    /**
     * Returns the log directory.
     */
    const std::string &log_directory() const {
        return log_directory_;
    }

    /**
     * Returns the maximum chunk size.
     */
    int max_chunk_size() const {
        return max_chunk_size_;
    }

    /**
     * Returns the maximum number of connections.
     */
    int max_connections() const {
        return max_connections_;
    }

    int initial_timeout() const {
        return initial_timeout_;
    }

    /**
     * Returns <code>true</code> if file logging is enabled.
     */
    bool file_logging() const {
        return file_logging_ == 1;
    }

    /**
     * Returns <code>true</code> if the client requests are logged.
     */
    bool log_requests() const {
        return log_requests_ == 1;
    }

    /**
     * Returns the connection timeout.
     */
    int connection_timeout() const {
        return connection_timeout_;
    }

};

#endif /* _CONFIG_H_ */
