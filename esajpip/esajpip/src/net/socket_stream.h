#ifndef _NET_SOCKET_STREAM_H_
#define _NET_SOCKET_STREAM_H_

#include <cstdio>
#include <cstring>
#include <iostream>
#include "socket.h"

namespace net {
    /**
     * Class derived from the STL <code>std::streambuf</code> to allow
     * streaming with sockets. See the documentation related to this
     * STL base class to understand the behaviour of the class <code>
     * SocketBuffer</code>.
     *
     * @see std::streambuf
     * @see Socket
     */
    class SocketBuffer : public std::streambuf {
    protected:
        Socket *socket;
        size_t in_len;
        char *in_buf;

    public:
        SocketBuffer(Socket *socket, size_t in_len) {
            this->socket = socket;
            this->in_len = in_len;
            in_buf = new char[in_len];
        }

        virtual int_type underflow() {
            ssize_t len = socket->Receive(in_buf, in_len);

            if (len <= 0) return traits_type::eof();
            else {
                setg(in_buf, in_buf, in_buf + len);
                return traits_type::to_int_type(*gptr());
            }
        }

        virtual ~SocketBuffer() {
            delete[] in_buf;
        }
    };


    /**
     * Class derived from <code>std::iostream</code> and <code>
     * SocketBuffer</code> that represents a socket stream.
     *
     * @see std::iostream
     * @see SocketBuffer
     */
    class SocketStream : public SocketBuffer, public std::iostream {
    public:
        SocketStream(Socket *socket, size_t in_len) : SocketBuffer(socket, in_len), std::iostream(this) {
        }
    };
}

#endif /* _NET_SOCKET_STREAM_H_ */
