#ifndef _DATA_FILE_H_
#define _DATA_FILE_H_

#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include "tr1_compat.h"

#include "trace.h"

#define CLAMP(a, min, max) ((a) < (min) ? (min) : ((a) > (max) ? (max) : (a)))

namespace data {
    using namespace std;

    /**
     * This is a wrapper class for the <code>FILE</code> functions that
     * provides all the functionality to handle files safely.
     */
    class BaseFile {
    public:
        /**
         * Safe pointer to this class.
         */
        typedef SHARED_PTR<BaseFile> Ptr;

        BaseFile() {
            clear();
        }

        /**
         * @param file_name Path name of the file to open.
         * @return <code>true</code> if successful.
         */
        bool Open(const char *file_name) {
            assert(address == MAP_FAILED);

            int fd;
            if ((fd = open(file_name, O_RDONLY)) == -1) {
                ERROR("Unable to open file: '" << file_name << "': " << strerror(errno));
                return false;
            } else {
                struct stat file_stat;
                if (fstat(fd, &file_stat) != -1) {
                    size = file_stat.st_size;
                    address = (char *) mmap(0, size, PROT_READ, MAP_FILE |  MAP_SHARED, fd, 0);
                }
                close(fd);
                if (address == MAP_FAILED) {
                    Close();
                    return false;
                }
                return true;
            }
        }

        /**
         * @param file_name Path name of the file to open.
         * @return <code>true</code> if successful.
         */
        bool Open(const string &file_name) {
            return Open(file_name.c_str());
        }

        /**
         * Changes the current position of the file.
         * @param offset Offset to add to the current position.
         * @param origin Origin to use for the change (<code>
         * SEEK_SET</code> by default).
         * @return <code>true</code> if successful.
         */
        bool Seek(int _offset, int origin = SEEK_SET) {
            assert(address != MAP_FAILED);

            size_t new_offset;
            if (origin == SEEK_SET)
                new_offset = _offset;
            else
                new_offset = offset + _offset;
            offset = CLAMP(new_offset, 0, size);
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

        /**
         * Reads a value from the file.
         * @param value Pointer to the value where to store.
         * @param num_bytes Number of bytes to read (by default,
         * the size of the value).
         * @return <code>true</code> if successful.
         */
        template<typename T>
        bool Read(T *value, int num_bytes = sizeof(T)) {
            assert(address != MAP_FAILED);
            int to_read = num_bytes;
            if (offset + to_read > size)
                to_read = size - offset;
            memcpy(value, address + offset, to_read);
            offset += to_read;
            return to_read == num_bytes;
        }

        /**
         * Reads a value from the file in reverse order.
         * @param value Pointer to the value where to store.
         * @param num_bytes Number of bytes to read (by default,
         * the size of the value).
         * @return <code>true</code> if successful.
         */
        template<typename T>
        bool ReadReverse(T *value, int num_bytes = sizeof(T)) {
            assert(address != MAP_FAILED);
            for (char *ptr = ((char *) value) + (num_bytes - 1); num_bytes-- > 0; ptr--) {
                if (offset < size) {
                    *ptr = *(address + offset);
                    offset++;
                } else
                    return false;
            }

           return true;
        }

        /**
         * Returns <code>true</code> if the file pointer is not
         * <code>NULL</code>.
         */
        operator bool() const {
            return address != MAP_FAILED;
        }

        /**
         * The destructor closes the file.
         */
        ~BaseFile() {
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

    typedef BaseFile File;
}

#endif /* _DATA_FILE_H_ */
