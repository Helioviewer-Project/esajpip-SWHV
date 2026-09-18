#include "channel_id.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

using namespace std;

bool GenerateChannelId(string *id) {
    uint8_t random[16];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return false;

    size_t offset = 0;
    while (offset < sizeof random) {
        ssize_t count = read(fd, random + offset, sizeof random - offset);
        if (count > 0) {
            offset += count;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            int read_error = count < 0 ? errno : EIO;
            close(fd);
            errno = read_error;
            return false;
        }
    }
    close(fd);

    static const char hex[] = "0123456789abcdef";
    id->resize(sizeof random * 2);
    for (size_t i = 0; i < sizeof random; ++i) {
        (*id)[i * 2] = hex[random[i] >> 4];
        (*id)[i * 2 + 1] = hex[random[i] & 15];
    }
    return true;
}

bool IsChannelId(const string &id) {
    if (id.size() != 32)
        return false;
    for (char c : id)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}
