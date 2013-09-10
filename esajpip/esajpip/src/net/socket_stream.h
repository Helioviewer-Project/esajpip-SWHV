#ifndef _NET_SOCKET_STREAM_H_
#define _NET_SOCKET_STREAM_H_


#include <iostream>
#include <stdio.h>
#include <string.h>
#include "socket.h"


namespace net
{

  /**
   * Class derived from the STL <code>std::streambuf</code> to allow
   * streaming with sockets. See the documentation related to this
   * STL base class to understand the behaviour of the class <code>
   * SocketBuffer</code>.
   *
   * @see std::streambuf
   * @see Socket
   */
  class SocketBuffer: public std::streambuf
  {
  protected:
    int sum;
    int in_len;
    int out_len;
    char *in_buf;
    char *out_buf;
    Socket socket;

  public:
    enum {
      INPUT_BUFFER_LENGTH = 500,
      OUTPUT_BUFFER_LENGTH = 500
    };

    SocketBuffer(int sid,
        int in_len = INPUT_BUFFER_LENGTH,
        int out_len = OUTPUT_BUFFER_LENGTH) :socket(sid)
    {
      sum = 0;

      this->in_len = in_len;
      this->out_len = out_len;

      in_buf = new char[in_len];
      out_buf = new char[out_len];

      setp(out_buf, out_buf + out_len);
    }

    virtual int sync()
    {
      if(pptr() != pbase()) {
        int n, len = 0;

        while(pbase() + len < pptr()) {
          n = socket.Send(pbase() + len, pptr() - pbase() - len);
          if(n >= 0) len += n;
          else return -1;
        }

        setp(out_buf, out_buf + out_len);
      }

      return 0;
    }

    virtual int_type underflow()
    {
      sum += (egptr() - eback());
      int len = socket.Receive(in_buf, in_len);

      if(len <= 0) return traits_type::eof();
      else {
        setg(in_buf, in_buf, in_buf + len);
        return traits_type::to_int_type(*gptr());
      }
    }

    virtual int_type overflow(int_type c = EOF)
    {
      if(traits_type::eq_int_type(traits_type::eof(), c)) return sync();
      else {
        if(pptr() == epptr()) sync();

        traits_type::assign(*pptr(), traits_type::to_char_type(c));
        pbump(1);

        return c;
      }
    }

    int GetReadBytes() const
    {
      return (sum + (egptr() - eback()));
    }

    Socket *GetSocket()
    {
      return &socket;
    }

    virtual ~SocketBuffer()
    {
      delete [] in_buf;
      delete [] out_buf;
    }
  };


  /**
   * Class derived from <code>std::iostream</code> and <code>
   * SocketBuffer</code> that represents a socket stream.
   *
   * @see std::iostream
   * @see SocketBuffer
   */
  class SocketStream :public SocketBuffer, public std::iostream
  {
  public:
    SocketStream(int sid,
        int in_len = INPUT_BUFFER_LENGTH,
        int out_len = OUTPUT_BUFFER_LENGTH)
    :SocketBuffer(sid, in_len, out_len), std::iostream(this)
    {
    }

    Socket *operator->()
    {
      return &(socket);
    }

    virtual ~SocketStream()
    {
    }
  };
}


#endif /* _NET_SOCKET_STREAM_H_ */
