#ifndef _DATA_FILE_H_
#define _DATA_FILE_H_

#include <algorithm>
#include <cstdio>
#include <cassert>
#include <errno.h>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "trace.h"

namespace data {

    class File {
    public:
        File() {
            clear();
        }

        File(const File &) = delete;
        File &operator=(const File &) = delete;

        /**
         * @param file_name Path name of the file to open.
         * @return <code>true</code> if successful.
         */
        bool Open(const char *file_name) {
            assert(address == MAP_FAILED);

            int fd;
            if ((fd = open(file_name, O_RDONLY)) == -1) {
                int open_error = errno;
                ERROR("Unable to open file: '" << file_name << "': " << strerror(open_error));
                errno = open_error;
                return false;
            } else {
                struct stat file_stat;
                if (fstat(fd, &file_stat) != -1) {
                    size = file_stat.st_size;
                    address = (char *) mmap(0, size, PROT_READ, MAP_FILE | MAP_SHARED, fd, 0);
                }
                close(fd);
                if (address == MAP_FAILED) {
                    Close();
                    return false;
                }
                return true;
            }
        }

        bool Open(const std::string &file_name) {
            return Open(file_name.c_str());
        }

        bool Seek(uint64_t _offset, int origin = SEEK_SET) {
            assert(address != MAP_FAILED);

            size_t new_offset;
            if (origin == SEEK_SET)
                new_offset = _offset;
            else // SEEK_CUR
                new_offset = offset + _offset;
            offset = std::min(new_offset, size);
            return true;
        }

        void Close() {
            if (address != MAP_FAILED) {
                munmap(address, size);
                clear();
            }
        }

        size_t GetOffset() const {
            assert(address != MAP_FAILED);
            return offset;
        }

        size_t GetSize() const {
            assert(address != MAP_FAILED);
            return size;
        }

        bool Find(unsigned char value, uint64_t limit) {
            assert(address != MAP_FAILED);
            if (limit > size || offset > limit)
                return false;
            const void *found = memchr(address + offset, value, limit - offset);
            if (found == NULL) {
                offset = limit;
                return false;
            }
            offset = static_cast<const char *>(found) - address + 1;
            return true;
        }

        /**
         * Reads a value from the file.
         * @param value Pointer to the value where to store.
         * @param num_bytes Number of bytes to read (by default,
         * the size of the value).
         * @return <code>true</code> if successful.
         */
        template<typename T>
        bool Read(T *value, size_t num_bytes = sizeof(T)) {
            assert(address != MAP_FAILED);
            if (num_bytes > size - offset)
                return false;
            memcpy(value, address + offset, num_bytes);
            offset += num_bytes;
            return true;
        }

        /**
         * Reads a value from the file in reverse order.
         * @param value Pointer to the value where to store.
         * @param num_bytes Number of bytes to read (by default,
         * the size of the value).
         * @return <code>true</code> if successful.
         */
        template<typename T>
        bool ReadReverse(T *value, size_t num_bytes = sizeof(T)) {
            assert(address != MAP_FAILED);
            if (num_bytes > size - offset)
                return false;
            char *bytes = (char *) value;
            for (size_t i = 0; i < num_bytes; ++i)
                bytes[num_bytes - i - 1] = address[offset + i];
            offset += num_bytes;
            return true;
        }

        ~File() {
            Close();
        }

    private:
        char *address;
        size_t size;
        size_t offset;

        void clear() {
            address = (char *) MAP_FAILED;
            size = 0;
            offset = 0;
        }
    };
}

#endif /* _DATA_FILE_H_ */
