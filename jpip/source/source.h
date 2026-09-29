#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace jpip {
// Borrowed bytes. Indexes retain offsets, not this object or its buffer.
class Source {
protected:
    const char *address = NULL;
    size_t size = 0;
public:
    Source() = default;
    Source(const void *bytes, size_t length)
        : address(static_cast<const char *>(bytes)), size(length) {}
    const uint8_t *Data() const { return reinterpret_cast<const uint8_t *>(address); }
    size_t GetSize() const { return size; }
    bool Read(uint64_t position, void *value, uint64_t length) const {
        if (position > size || length > size - position) return false;
        if (length) memcpy(value, address + position, static_cast<size_t>(length));
        return true;
    }
};
// Acquiring another source must not invalidate a source already acquired.
// Sources may be released after a response and reacquired for the next one.
class SourceProvider {
public:
    virtual const Source *GetSource(const std::string &path) = 0;
    // The index releases each linked source after extracting its offsets.
    // Providers may retain cheap memory sources; file adapters can unmap them.
    virtual void ReleaseSource(const std::string &path) {}
    virtual ~SourceProvider() = default;
};
}
