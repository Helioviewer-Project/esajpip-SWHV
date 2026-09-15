#ifndef _NET_SOCKET_H__
#define _NET_SOCKET_H__

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/times.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstdint>
#include <string>
#include "address.h"

namespace net {

    /**
      This class has been designed to work with UNIX
      sockets in an easy and object oriented way.
    */
    class Socket {
    protected:
        int sid;    ///< Socket id

    public:
        /**
          Initializes the socket id with an invalid value.
        */
        Socket() {
            sid = -1;
        }

        /**
          Initializes the socket id with an integer value.
        */
        Socket(int s) {
            sid = s;
        }

        /**
         * Copy constructor.
        */
        Socket(const Socket &xs) {
            sid = xs.sid;
        }

        /**
          This operator allows to work directly with UNIX socket API.
        */
        operator int() const {
            return sid;
        }

        /**
          Copy asignment.
        */
        Socket &operator=(int nsid) {
            sid = nsid;
            return *this;
        }

        /**
          This method creates a new Internet socket, storing its identifier
          in the object.
          @param type Socket type, <code>SOCK_STREAM</code> by default.
          @return <code>true</code> if successful.
        */
        bool OpenInet(int type = SOCK_STREAM) {
            return (sid = socket(PF_INET, type, 0)) != -1;
        }

        /**
          Configures the socket for listening incoming connections.
          @param address Address used to listen.
          @param nstack Maximum number of clients in listening stack.
          @return <code>true</code> if successful.
        */
        bool ListenAt(const InetAddress &address, int nstack = 10) {
            int flags = 1;
            if (setsockopt(sid, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags)) != 0) return false;
            if (::bind(sid, address.GetSockAddr(), address.GetSize()) != 0) return false;
            return listen(sid, nstack) == 0;
        }

        /**
          If it is a server socket, it accepts a new connection.
          @param from_address Pointer to store the client address.
          @return The integer identifier (file descriptor) of the new socket.
        */
        int Accept(InetAddress *from_address) {
            socklen_t len = from_address->GetSize();
            return accept(sid, from_address->GetSockAddr(), &len);
        }

        /**
          Receives a number of bytes.
          @param buf Buffer where to store the received bytes.
          @param len Length of the buffer.
          @return The number of received bytes.
        */
        ssize_t Receive(void *buf, size_t len);

        /**
          Sends a number of bytes.
          @param buf Buffer with the bytes to sent.
          @param len Number of bytes to sent.
          @return The number of sent bytes.
        */
        ssize_t Send(const void *buf, size_t len);

        /**
         * Sends a descriptor through the socket.
         * @param fd File descriptor.
         * @param connection_id Connection identifier to send with the descriptor.
         * @return true if successful.
         */
        bool SendDescriptor(int fd, uint64_t connection_id);

        /**
         * Receives a descriptor from a socket.
         * @param fd Variable to store the received descriptor.
         * @param connection_id Variable to store the attached connection identifier.
         * @return <code>true</code> if successful.
         */
        bool ReceiveDescriptor(int *fd, uint64_t *connection_id);

        /**
          Closes the socket.
        */
        void Close() {
            if (sid != -1) close(sid);
            sid = -1;
        }

        /**
          The destructor does not closes the socket!.
        */
        ~Socket() = default;
    };
}

#endif
