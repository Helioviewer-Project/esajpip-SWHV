#ifndef _NET_ADDRESS_H_
#define _NET_ADDRESS_H_

#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstdlib>
#include <cstring>
#include <sys/un.h>
#include <unistd.h>

namespace net {
    using namespace std;

    /**
     * Class to identify and handle an Internet address. The
     * used internal address structure is <code>sockaddr_in</code>.
     *
     */
    class InetAddress {
    private:
        sockaddr_in sock_addr;    ///< Internal address structure

    public:
        /**
         * Initializes the address to zero.
         */
        InetAddress() {
            memset(&sock_addr, 0, sizeof sock_addr);
            sock_addr.sin_family = AF_INET;
        }

        /**
         * Initializes the address with given port. The used path
         * is <code>INADDR_ANY</code>.
         * @param port Port number.
         */
        InetAddress(uint16_t port) {
            memset(&sock_addr, 0, sizeof sock_addr);

            sock_addr.sin_family = AF_INET;
            sock_addr.sin_addr.s_addr = INADDR_ANY;
            sock_addr.sin_port = htons(port);
        }

        /**
         * Initializes the address with the given path and port.
         * @param path Address path.
         * @param port Port number.
         */
        InetAddress(const char *path, uint16_t port) {
            memset(&sock_addr, 0, sizeof sock_addr);

            struct in_addr addr;
            bool found = inet_aton(path, &addr);
            if (!found) {
                addrinfo hints;
                memset(&hints, 0, sizeof hints);
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;

                addrinfo *resolved = NULL;
                if (getaddrinfo(path, NULL, &hints, &resolved) == 0 && resolved != NULL) {
                    addr = ((sockaddr_in *) resolved->ai_addr)->sin_addr;
                    found = true;
                }
                if (resolved != NULL)
                    freeaddrinfo(resolved);
            }

            if (found) {
                sock_addr.sin_family = AF_INET;
                sock_addr.sin_port = htons(port);
                sock_addr.sin_addr = addr;
            }
        }

        sockaddr *GetSockAddr() const {
            return (sockaddr *) &sock_addr;
        }

        socklen_t GetSize() const {
            return sizeof sock_addr;
        }

        /**
         * Returns the address path.
         */
        string GetPath() const {
            return inet_ntoa(sock_addr.sin_addr);
        }

        /**
         * Returns the port number.
         */
        uint16_t GetPort() const {
            return ntohs(sock_addr.sin_port);
        }
    };

    /**
     * Class to identify and handle an UNIX address. The
     * used internal address structure is <code>sockaddr_un</code>.
     *
     */
    class UnixAddress {
    private:
        sockaddr_un sock_addr;    ///< Internal address structure

    public:
        /**
         * Initializes the address to zero.
         */
        UnixAddress() {
            memset(&sock_addr, 0, sizeof sock_addr);
            sock_addr.sun_family = AF_UNIX;
        }

        /**
         * Initializes the address with given path.
         * @param path Address path.
         */
        UnixAddress(const char *path) {
            memset(&sock_addr, 0, sizeof sock_addr);
            sock_addr.sun_family = AF_UNIX;
            strncpy(sock_addr.sun_path, path, sizeof sock_addr.sun_path - 1);
        }

        /**
         * Removes the file associated to the UNIX address.
         */
        UnixAddress &Reset() {
            unlink(sock_addr.sun_path);
            return *this;
        }

        sockaddr *GetSockAddr() const {
            return (sockaddr *) &sock_addr;
        }

        socklen_t GetSize() const {
            return sizeof sock_addr;
        }

        /**
         * Returns the address path.
         */
        string GetPath() const {
            return sock_addr.sun_path;
        }
    };
}

#endif /* _NET_ADDRESS_H_ */
