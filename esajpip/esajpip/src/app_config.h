#ifndef _APP_CONFIG_H_
#define _APP_CONFIG_H_

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
    int logging_;                ///< <code>true</code> if logs messages are allowed
    int log_requests_;  ///< <code>true</code> if the client requests are logged
    string address_;            ///< Listening address
    string images_folder_;    ///< Directory for the images
    string caching_folder_;    ///< Directory for the caching files
    string logging_folder_;    ///< Directory for the logging files
    int max_chunk_size_;        ///< Maximum chunk size
    int max_connections_;        ///< Maximum number of connections
    int com_time_out_;        ///< Connection time-out
    int cache_max_time_;        ///< Maximum time for the cache files

public:
    /**
     * Initializes the object with zero and empty values.
     */
    AppConfig() {
        port_ = 0;
        logging_ = 0;
        address_ = "";
        log_requests_ = 0;
        images_folder_ = "";
        caching_folder_ = "";
        logging_folder_ = "";
        max_chunk_size_ = 0;
        max_connections_ = 0;
        cache_max_time_ = 0;
        com_time_out_ = -1;
    }

    /**
     * Loads the parameters from a configuration file.
     * @param file_name Configuration file.
     * @return <code>true</code> if successful.
     */
    bool Load(const char *file_name);

    friend ostream &operator<<(ostream &out, const AppConfig &cfg) {
        out << "Configuration:" << endl;
        out << "\tListen at: " << cfg.address_ << ":" << cfg.port_ << endl;
        out << "\tFolders:" << endl;
        out << "\t\tImages: " << cfg.images_folder_ << endl;
        out << "\t\tCaching: " << cfg.caching_folder_ << endl;
        out << "\t\tLogging: " << cfg.logging_folder_ << endl;
        out << "\tConnections: " << endl;
        out << "\t\tMax. number: " << cfg.max_connections_ << endl;
        out << "\t\tMax. time-out: " << cfg.com_time_out() << endl;
        out << "\tGeneral:" << endl;
        out << "\t\tLogging: " << (cfg.logging_ == 1 ? "yes" : "no") << endl;
        out << "\t\tLog. requests: " << (cfg.log_requests_ == 1 ? "yes" : "no") << endl;
        out << "\t\tChunk max. size: " << cfg.max_chunk_size_ << endl;
        return out;
    }

    /**
     * Returns the listening port.
     */
    int port() const {
        return port_;
    }

    /**
     * Returns the listening address.
     */
    string address() const {
        return address_;
    }

    /**
     * Returns the folder of the images.
     */
    string images_folder() const {
        return images_folder_;
    }

    /**
     * Returns the folder used for caching.
     */
    string caching_folder() const {
        return caching_folder_;
    }

    /**
     * Returns the folder used for the logging files.
     */
    string logging_folder() const {
        return logging_folder_;
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

    /**
     * Returns <code>true</code> if the logging messages are allowed.
     */
    bool logging() const {
        return (logging_ == 1);
    }

    /**
     * Returns <code>true</code> if the client requests are logged.
     */
    bool log_requests() const {
        return (log_requests_ == 1);
    }

    /**
     * Returns the connection time-out.
     */
    int com_time_out() const {
        return com_time_out_;
    }

    /**
     * Returns the maximum time for the cache files in
     * seconds.
     */
    int cache_max_time() const {
        return cache_max_time_;
    }

    virtual ~AppConfig() {
    }
};

#endif /* _APP_CONFIG_H_ */
