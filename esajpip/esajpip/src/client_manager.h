#ifndef _CLIENT_MANAGER_H_
#define _CLIENT_MANAGER_H_

#include <cstdint>
#include <memory>
#include <string>
#include "app_config.h"
#include "channel.h"

typedef void (*ConnectionClosed)(uint64_t connection_id);

void RunChannel(const AppConfig &cfg, const std::string &channel,
                const std::shared_ptr<ChannelInbox> &inbox,
                ConnectionClosed connection_closed);

#endif /* _CLIENT_MANAGER_H_ */
