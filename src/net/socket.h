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
#include <string>
#include "address.h"


namespace net
{

  using namespace std;


  /**
    This class has been designed to work with UNIX
    sockets in an easy and object oriented way.
  */
  class Socket
  {
  protected:
    int sid;	///< Socket id

  public:
    /**
      Initializes the socket id with an invalid value.
    */
    Socket()
    {
      sid = -1;
    }

    /**
      Initializes the socket id with an integer value.
    */
    Socket(int s)
    {
      sid = s;
    }

    /**
     * Copy constructor.
    */
    Socket(const Socket& xs)
    {
      sid = xs.sid;
    }

    /**
      This operator allows to work directly with UNIX socket API.
    */
    operator int() const
    {
      return sid;
    }

    /**
      @return <code>true</code> if the identifier has a valid value.
    */
    bool IsValid() const
    {
      return (sid != -1);
    }

    /**
      Copy asignment.
    */
    Socket& operator=(int nsid)
    {
      sid = nsid;
      return *this;
    }

    /**
      This method creates a new Internet socket, storing its identifier
      in the object.
      @param type Socket type, <code>SOCK_STREAM</code> by default.
      @return <code>true</code> if successful.
    */
    bool OpenInet(int type = SOCK_STREAM)
    {
      return ((sid = socket(PF_INET, type, 0)) != -1);
    }

    /**
      This method creates a new UNIX socket, storing its identifier
      in the object.
      @param type Socket type, <code>SOCK_STREAM</code> by default.
      @return <code>true</code> if successful.
    */
    bool OpenUnix(int type = SOCK_STREAM)
    {
      return ((sid = socket(PF_UNIX, type, 0)) != -1);
    }

    /**
      Configures the socket for listening incoming connections.
      @param address Address used to listen.
      @param nstack Maximum number of clients in listening stack.
      @return <code>true</code> if successful.
    */
    bool ListenAt(const Address& address, int nstack = 10)
    {
      int flags = 1;
      setsockopt(sid, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags));
      if(bind(sid, address.GetSockAddr(), address.GetSize()) != 0) return false;
      if(listen(sid, nstack) != 0) return false;
      return true;
    }

    /**
      Connects the socket to a server.
      @param to_address Address to connect.
      @return <code>true</code> if successful.
    */
    bool ConnectTo(const Address& to_address)
    {
      return (connect(sid, to_address.GetSockAddr(), to_address.GetSize()) == 0);
    }

    /**
     * Binds the socket to the specified address.
     * @param address Address to bind.
     * @return <code>true</code> if successful.
     */
    bool BindTo(const Address& address)
    {
      return !bind(sid, address.GetSockAddr(), address.GetSize());
    }

    /**
      If it is a server socket, it accepts a new connection.
      @param from_address Pointer to store the client address.
      @return The integer identifier (file descriptor) of the new socket.
    */
    int Accept(Address *from_address)
    {
      socklen_t len = from_address->GetSize();
      return accept(sid, from_address->GetSockAddr(), &len);
    }

    /**
      Set the blocking mode of the send/receive operations.
      By default, this mode is true.
      @param state Blocking mode state to set.
      @return A reference of the same object.
    */
    Socket& SetBlockingMode(bool state = true);

    /**
      @return The blocking mode of the send/receive operations.
    */
    bool IsBlockingMode();

    /**
      Receives a number of bytes. This methods allows to prevent
      blocking, without having into account the default blocking
      mode stablished.
      @param buf Buffer where to store the received bytes.
      @param len Length of the buffer.
      @param prevent_block true if blocking will be prevented.
      @return The number of received bytes.
    */
    int Receive(void *buf, int len, bool prevent_block = false);

    /**
      Receives a number of bytes. This methods allows to prevent
      blocking, without having into account the default blocking
      mode stablished.
      @param address Pointer to store the from address.
      @param buf Buffer where to store the received bytes.
      @param len Length of the buffer.
      @param prevent_block true if blocking will be prevented.
      @return The number of received bytes.
    */
    int ReceiveFrom(Address *address, void *buf, int len,
        bool prevent_block = false);

    /**
      Sends a number of bytes. This methods allows to prevent
      blocking, without having into account the default blocking
      mode stablished.
      @param buf Buffer with the bytes to sent.
      @param len Number of bytes to sent.
      @param prevent_block true if blocking will be prevented.
      @return The number of sent bytes.
    */
    int Send(void *buf, int len, bool prevent_block = false);

    /**
      Sends a number of bytes to a specific address. This methods
      allows to prevent blocking, without having into account the
      default blocking mode established.
      @param address Address to send the bytes.
      @param buf Buffer with the bytes to sent.
      @param len Number of bytes to sent.
      @param prevent_block true if blocking will be prevented.
      @return The number of sent bytes.
    */
    int SendTo(const Address& address, void *buf, int len,
        bool prevent_block = false);

    /**
     * Sends a descriptor through the socket.
     * @param address Address of the socket to send the descriptor.
     * @param fd File descriptor.
     * @param aux Auxiliary information to send attached.
     * @return true if successful.
     */
    bool SendDescriptor(const Address& address, int fd, int aux = 0);

    /**
     * Returns <code>true</code> if the sockets is valid, that is,
     * if after a polling regarding error status is not successful.
     */
    bool IsValid();

    /**
     * Waits until input data is available (<code>POLLIN</code>).
     * @param time_out Time out (infinite by default).
     * @return The value returned by the <code>poll</code> function.
     */
    int WaitForInput(int time_out = -1);

    /**
     * Waits until output data can be sent (<code>POLLOUT</code>).
     * @param time_out Time out (infinite by default).
     * @return The value returned by the <code>poll</code> function.
     */
    int WaitForOutput(int time_out = -1);

    /**
     * Configures the parameter <code>TCP_NODELAY</code> of the
     * socket.
     * @param val New value for the parameter (1 by default).
     * @return <code>true</code> if successful.
     */
    bool SetNoDelay(int val = 1)
    {
      return !setsockopt(sid, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
    }

    /**
     * Receives a descriptor from a socket.
     * @param fd Variable to store the received descriptor.
     * @param aux Auxiliary information received attached.
     * @return <code>true</code> if successful.
     */
    bool ReceiveDescriptor(int *fd, int *aux = NULL);

    /**
      Closes the socket.
    */
    void Close()
    {
      if(sid != -1) close(sid);
      sid = -1;
    }

    /**
      The destructor does not closes the socket!.
    */
    ~Socket()
    {
    }
  };

}


#endif	
	
