#ifndef _CLIENT_MANAGER_H_
#define _CLIENT_MANAGER_H_

#include "app_config.h"
#include "client_info.h"

/**
 * Handles a client connection with a dedicated
 * thread.
 */
class ClientManager {
private:
    AppConfig &cfg;                ///< Application configuration

public:
    /**
     * Initializes the object.
     * @param _cfg Application configuration.
     */
    explicit ClientManager(AppConfig &_cfg) : cfg(_cfg) {
    }

    /**
     * Starts the handling of a client connection.
     * @param client_info Client information.
     */
    void Run(ClientInfo *client_info);

};

#endif /* _CLIENT_MANAGER_H_ */
