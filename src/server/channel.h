#ifndef _SERVER_CHANNEL_H_
#define _SERVER_CHANNEL_H_

#include <memory>
#include <string>
#include "config.h"
#include "connection_queue.h"

typedef void (*ConnectionClosed)();

void RunChannel(const Config &cfg, const std::string &channel,
                const std::shared_ptr<ConnectionQueue> &queue,
                ConnectionClosed connection_closed);

#endif /* _SERVER_CHANNEL_H_ */
