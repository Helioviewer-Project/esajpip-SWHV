#ifndef _NET_SOCKET_STREAM_H_
#define _NET_SOCKET_STREAM_H_

#include <iostream>
#include <vector>
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
        std::vector<char> in_buf;

    public:
        SocketBuffer(Socket *s, size_t len) : socket(s), in_buf(len) {
        }

        int_type underflow() override {
            ssize_t len = socket->Receive(in_buf.data(), in_buf.size());

            if (len <= 0) return traits_type::eof();
            else {
                setg(in_buf.data(), in_buf.data(), in_buf.data() + len);
                return traits_type::to_int_type(*gptr());
            }
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
