#ifndef _CLIENT_MANAGER_H_
#define _CLIENT_MANAGER_H_

#include "app_info.h"
#include "app_config.h"
#include "client_info.h"

/**
 * Handles a client connection with a dedicated
 * thread.
 */
class ClientManager {
private:
    AppConfig &cfg;                ///< Application configuration
    AppInfo &app_info;            ///< Application run-time information

public:
    /**
     * Initializes the object.
     * @param _cfg Application configuration.
     * @param _app_info Application run-time information.
     * @param _index_manager Index manager.
     */
    ClientManager(
            AppConfig &_cfg,
            AppInfo &_app_info)
            : cfg(_cfg), app_info(_app_info) {
    }

    /**
     * Starts the handling of a client connection.
     * @param client_info Client information.
     */
    void Run(ClientInfo *client_info);

    /**
     * Starts the handling of a client connection
     * but it does not do anything. This method is
     * used for testing the architecture of the
     * server.
     * @param client_info Client information.
     */
    void RunBasic(ClientInfo *client_info);

    virtual ~ClientManager() {
    }
};

#endif /* _CLIENT_MANAGER_H_ */
