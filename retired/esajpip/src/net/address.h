#ifndef _NET_ADDRESS_H_
#define _NET_ADDRESS_H_


#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>


namespace net
{
  using namespace std;


  /**
   * Abstract base class to wrap the <code>sockaddr</code>
   * derived structures. This class is the base of the
   * address classes.
   *
   * @see InetAddress
   * @see UnixAddress
   */
  class Address
  {
  public:
	/**
	 * Empty constructor.
	 */
    Address()
    {
    }

    /**
     * Returns a pointer to a <code>sockaddr</code> structure.
     */
    virtual sockaddr *GetSockAddr() const = 0;

    /**
     * Returns the size in bytes of the <code>sockaddr</code>
     * structure returned by the previous method.
     */
    virtual int GetSize() const = 0;

    /**
     * Empty destructor.
     */
    virtual ~Address()
    {
    }
  };


  /**
   * Class to identify and handle an Internet address. The
   * used internal address structure is <code>sockaddr_in</code>.
   *
   * @see Address
   */
  class InetAddress :public Address
  {
  private:
    sockaddr_in sock_addr;	///< Internal address structure

  public:
    /**
     * Initializes the address to zero.
     */
    InetAddress()
    {
      memset(&sock_addr, 0, sizeof(sock_addr));

      sock_addr.sin_family = AF_INET;
    }

    /**
     * Copy constructor.
     */
    InetAddress(const InetAddress& address)
    {
      memcpy(&sock_addr, &(address.sock_addr), sizeof(sock_addr));
    }

    /**
     * Initializes the address with given port. The used path
     * is <code>INADDR_ANY</code>.
     * @param port Port number.
     */
    InetAddress(int port)
    {
      memset(&sock_addr, 0, sizeof(sock_addr));

      sock_addr.sin_family = AF_INET;
      sock_addr.sin_addr.s_addr = INADDR_ANY;
      sock_addr.sin_port = htons((u_short)port);
    }

    /**
     * Initializes the address with the given path and port.
     * @param path Address path.
     * @param port Port number.
     */
    InetAddress(const char *path, int port)
    {
      memset(&sock_addr, 0, sizeof(sock_addr));

      hostent *hp = NULL;
      unsigned long addr;

      if(inet_addr(path) == INADDR_NONE)
        hp = gethostbyname(path);
      else {
        addr = inet_addr(path);
        hp = gethostbyaddr((char *)&addr, sizeof(addr), AF_INET);
      }

      if(hp != NULL) {
        sock_addr.sin_family = AF_INET;
        sock_addr.sin_port = htons(port);
        sock_addr.sin_addr.s_addr = *((unsigned long *)hp->h_addr);
      }
    }

    /**
     * Copy assignment.
     */
    InetAddress& operator=(const InetAddress& address)
    {
      memcpy(&sock_addr, &(address.sock_addr), sizeof(sock_addr));
      return *this;
    }

    /**
     * Overloaded from the base class to use the
     * internal address structure.
     */
    virtual sockaddr *GetSockAddr() const
    {
      return (sockaddr *)&sock_addr;
    }

    /**
     * Overloaded from the base class to use the
     * internal address structure.
     */
    virtual int GetSize() const
    {
      return sizeof(sock_addr);
    }

    /**
     * Returns the address path.
     */
    string GetPath() const
    {
      return inet_ntoa(sock_addr.sin_addr);
    }

    /**
     * Returns the port number.
     */
    int GetPort() const
    {
      return ntohs(sock_addr.sin_port);
    }
  };


  /**
   * Class to identify and handle an UNIX address. The
   * used internal address structure is <code>sockaddr_un</code>.
   *
   * @see Address
   */
  class UnixAddress :public Address
  {
  private:
    sockaddr_un sock_addr;	///< Internal address structure

  public:
    /**
     * Initializes the address to zero.
     */
    UnixAddress()
    {
      memset(&sock_addr, 0, sizeof(sock_addr));

      sock_addr.sun_family = AF_UNIX;
    }

    /**
     * Copy constructor.
     */
    UnixAddress(const UnixAddress& address)
    {
      memcpy(&sock_addr, &(address.sock_addr), sizeof(sock_addr));
    }

    /**
     * Initializes the address with given path.
     * @param path Address path.
     */
    UnixAddress(const char *path)
    {
      memset(&sock_addr, 0, sizeof(sock_addr));

      sock_addr.sun_family = AF_UNIX;
      strncpy(sock_addr.sun_path, path, sizeof(sock_addr.sun_path) - 1);
    }

    /**
     * Copy assignment.
     */
    UnixAddress& operator=(const UnixAddress& address)
    {
      memcpy(&sock_addr, &(address.sock_addr), sizeof(sock_addr));
      return *this;
    }

    /**
     * Removes the file associated to the UNIX address.
     */
    UnixAddress& Reset()
    {
      unlink(sock_addr.sun_path);
      return *this;
    }

    /**
     * Overloaded from the base class to use the
     * internal address structure.
     */
    virtual sockaddr *GetSockAddr() const
    {
      return (sockaddr *)&sock_addr;
    }

    /**
     * Overloaded from the base class to use the
     * internal address structure.
     */
    virtual int GetSize() const
    {
      return sizeof(sock_addr);
    }

    /**
     * Returns the address path.
     */
    string GetPath() const
    {
      return sock_addr.sun_path;
    }
  };

}


#endif /* _NET_ADDRESS_H_ */
