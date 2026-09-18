#ifndef _SERVER_CHANNEL_H_
#define _SERVER_CHANNEL_H_

#include <cstdint>
#include <memory>
#include <string>
#include "config.h"
#include "connection_queue.h"

typedef void (*ConnectionClosed)();

extern const char JPIP_HANDLED_HEADER[];

void RunChannel(const Config &cfg, const std::string &channel,
                uint64_t channel_number,
                const std::shared_ptr<ConnectionQueue> &queue,
                ConnectionClosed connection_closed);

#endif /* _SERVER_CHANNEL_H_ */
