#ifndef _CLIENT_INFO_H_
#define _CLIENT_INFO_H_


#include <time.h>


/**
 * Contains information of a connected client.
 */
class ClientInfo
{
private:
  int sock_;			///< Client socket
  int base_id_;			///< Base identifier
  time_t tm_start;		///< When the connection started
  int father_sock_;		///< Father socket
  long bytes_sent_;		///< Total bytes sent

public:
  /**
   * Initializes the object.
   * @param base_id Base identifier.
   * @param sock Client socket.
   * @param father_sock Father socket.
   */
  ClientInfo(int base_id, int sock, int father_sock)
  {
    sock_ = sock;
    bytes_sent_ = 0;
    base_id_ = base_id;
    tm_start = ::time(NULL);
    father_sock_ = father_sock;
  }

  /**
   * Returns the client socket.
   */
  int sock() const
  {
    return sock_;
  }

  /**
   * Returns the base identifier.
   */
  int base_id() const
  {
    return base_id_;
  }

  /**
   * Returns the father socket.
   */
  int father_sock() const
  {
    return father_sock_;
  }

  /**
   * Returns the total bytes sent.
   */
  long bytes_sent() const
  {
    return bytes_sent_;
  }

  /**
   * Returns the time spent from the starting
   * of the connection.
   */
  long time() const
  {
    time_t now = ::time(NULL);
    return (long)(tm_start - now);
  }

  virtual ~ClientInfo()
  {
  }
};


#endif /* _CLIENT_INFO_H_ */
