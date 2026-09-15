#ifndef _APP_CONFIG_H_
#define _APP_CONFIG_H_

#include <cstdint>
#include <string>
#include <iostream>

using namespace std;

/**
 * Contains the configuration parameters of the
 * application. It is possible to load these
 * parameters from a configuration file. This
 * class can be printed.
 */
class AppConfig {
private:
    int port_;                ///< Listening port
    int file_logging_;                ///< <code>true</code> if file logging is enabled
    int log_requests_;  ///< <code>true</code> if the client requests are logged
    string address_;            ///< Listening address
    string image_directory_;    ///< Directory containing the images
    string log_directory_;    ///< Directory for log files
    int max_chunk_size_;        ///< Maximum chunk size
    int max_connections_;        ///< Maximum number of connections
    int initial_timeout_; ///< Initial request timeout
    int connection_timeout_;        ///< Connection timeout

public:
    /**
     * Initializes the object with zero and empty values.
     */
    AppConfig() {
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
    bool Load(const char *file_name, string &error_message);

    friend ostream &operator<<(ostream &out, const AppConfig &cfg) {
        out << "Configuration:" << endl;
        out << "\tListen at: " << cfg.address_ << ":" << cfg.port_ << endl;
        out << "\tDirectories:" << endl;
        out << "\t\tImages: " << cfg.image_directory_ << endl;
        out << "\t\tLogging: " << cfg.log_directory_ << endl;
        out << "\tConnections: " << endl;
        out << "\t\tLimit: " << cfg.max_connections_ << endl;
        out << "\t\tInitial timeout: " << cfg.initial_timeout_ << endl;
        out << "\t\tTimeout: " << cfg.connection_timeout() << endl;
        out << "\tGeneral:" << endl;
        out << "\t\tFile: " << (cfg.file_logging_ == 1 ? "yes" : "no") << endl;
        out << "\t\tLog. requests: " << (cfg.log_requests_ == 1 ? "yes" : "no") << endl;
        out << "\t\tChunk size: " << cfg.max_chunk_size_ << endl;
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
    const string &address() const {
        return address_;
    }

    /**
     * Returns the image directory.
     */
    const string &image_directory() const {
        return image_directory_;
    }

    /**
     * Returns the log directory.
     */
    const string &log_directory() const {
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

#endif /* _APP_CONFIG_H_ */
