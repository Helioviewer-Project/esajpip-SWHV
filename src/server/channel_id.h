#ifndef _SERVER_CHANNEL_ID_H_
#define _SERVER_CHANNEL_ID_H_

#include <string>

bool GenerateChannelId(std::string *id);
bool IsChannelId(const std::string &id);

#endif /* _SERVER_CHANNEL_ID_H_ */
