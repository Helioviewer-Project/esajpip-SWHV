#include "channel_id.h"

#include <cstdint>

#include <uv.h>

using namespace std;

int GenerateChannelId(string *id) {
    uint8_t random[16];
    int result = uv_random(NULL, NULL, random, sizeof random, 0, NULL);
    if (result != 0)
        return result;

    static const char hex[] = "0123456789abcdef";
    id->resize(sizeof random * 2);
    for (size_t i = 0; i < sizeof random; ++i) {
        (*id)[i * 2] = hex[random[i] >> 4];
        (*id)[i * 2 + 1] = hex[random[i] & 15];
    }
    return 0;
}
