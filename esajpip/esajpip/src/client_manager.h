#ifndef _CLIENT_MANAGER_H_
#define _CLIENT_MANAGER_H_

#include "app_config.h"
#include "net/socket.h"

void RunClient(const AppConfig &cfg, net::Socket &socket, uint64_t connection_id);

#endif /* _CLIENT_MANAGER_H_ */
