#pragma once

#include <algorithm>
#include <cstdio>
#include <errno.h>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>

#include "server/trace.h"
#include "jpip/source/source.h"

namespace server {

    class File : public jpip::Source {
    public:
        enum class OpenResult {
            OPENED,
            NOT_FOUND,
            EMPTY,
            TOO_LARGE,
            FAILED
        };

        File() {
            clear();
        }

        File(const File &) = delete;
        File &operator=(const File &) = delete;

        /**
         * @param file_name Path name of the file to open.
         * @param maximum_size Largest file size accepted.
         * @return Detailed open result.
         */
        OpenResult Open(const char *file_name, uint64_t maximum_size) {
            if (address != MAP_FAILED) {
                ERROR("File already open, not opening '"
                      << EscapeForLog(file_name) << "'");
                return OpenResult::FAILED;
            }

            int fd = open(file_name, O_RDONLY);
            if (fd == -1) {
                int open_error = errno;
                ERROR("Unable to open file: '" << EscapeForLog(file_name) << "': "
                      << g_strerror(open_error));
                return open_error == ENOENT || open_error == ENOTDIR
                        ? OpenResult::NOT_FOUND : OpenResult::FAILED;
            }

            struct stat file_stat;
            if (fstat(fd, &file_stat) == -1) {
                int stat_error = errno;
                close(fd);
                ERROR("Unable to inspect file: '" << EscapeForLog(file_name) << "': "
                      << g_strerror(stat_error));
                return OpenResult::FAILED;
            }
            if (file_stat.st_size == 0) {
                close(fd);
                return OpenResult::EMPTY;
            }
            if (file_stat.st_size < 0 ||
                static_cast<uint64_t>(file_stat.st_size) > maximum_size ||
                static_cast<uint64_t>(file_stat.st_size) >
                        std::numeric_limits<size_t>::max()) {
                close(fd);
                return OpenResult::TOO_LARGE;
            }

            size_t file_size = static_cast<size_t>(file_stat.st_size);
            void *mapped_address = mmap(0, file_size, PROT_READ,
                                        MAP_FILE | MAP_SHARED, fd, 0);
            if (mapped_address == MAP_FAILED) {
                int map_error = errno;
                close(fd);
                ERROR("Unable to map file: '" << EscapeForLog(file_name) << "': "
                      << g_strerror(map_error));
                return OpenResult::FAILED;
            }
            close(fd);
            address = static_cast<char *>(mapped_address);
            size = file_size;
            return OpenResult::OPENED;
        }

        bool Open(const char *file_name) {
            return Open(file_name, std::numeric_limits<uint64_t>::max()) ==
                   OpenResult::OPENED;
        }

        bool Open(const std::string &file_name) {
            return Open(file_name.c_str());
        }

        void Close() {
            if (address != MAP_FAILED) {
                munmap(const_cast<char *>(address), size);
                clear();
            }
        }

        ~File() {
            Close();
        }

    private:

        void clear() {
            address = (char *) MAP_FAILED;
            size = 0;
        }
    };
}
