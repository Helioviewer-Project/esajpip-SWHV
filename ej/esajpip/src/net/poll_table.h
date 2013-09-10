#ifndef _NET_POLL_TABLE_H_
#define _NET_POLL_TABLE_H_


#include <vector>
#include <poll.h>
#include <algorithm>


namespace net
{
  using namespace std;


  /**
   * Wrapper structure for the structure <code>pollfd</code>
   * used by the kernel <code>poll</code> functions.
   *
   * @see PollTable
   */
  struct PollFD: pollfd
  {
	/**
	 * Initializes the structure.
	 * @param vfd File descriptor.
	 * @param mask Poll mask.
	 */
    PollFD(int vfd, int mask)
    {
      fd = vfd;
      events = mask;
      revents = 0;
    }

    /**
     * Returns <code>true</code> if the file descriptor
     * is the same as the given value.
     */
    bool operator==(int n)
    {
      return (fd == n);
    }
  };


  /**
   * This class allows to perfom polls easily over a vector of
   * descriptors. It uses an internal STL vector of <code>PollFD
   * </code> objects to handle dinamically the file descriptors
   * and masks.
   *
   * @see PollFD
   */
  class PollTable
  {
  private:
	/**
	 * Vector with the file descriptors and masks for polling.
	 */
    vector<PollFD> fds;

  public:
    PollTable()
    {
    }

    /**
     * Adds a new file descriptor and mask to the vector.
     * @param fd File descriptor.
     * @param mask Polling mask.
     */
    void Add(int fd, int mask)
    {
      fds.push_back(PollFD(fd, mask));
    }

    /**
     * Peforms a poll over all the descriptors using the
     * associated masks.
     * @param timeout Time out of the poll (infinite by default).
     * @return The value given by the kernel function <code>poll</code>.
     */
    int Poll(int timeout = -1)
    {
      return poll(&(fds[0]), (int)fds.size(), timeout);
    }

    /**
     * Returns the size of the internal vector.
     */
    int GetSize() const
    {
      return fds.size();
    }

    /**
     * Removes an item of the internal vector giving its
     * file descriptor.
     * @param fd File descriptor to remove.
     */
    void Remove(int fd)
    {
      vector<PollFD>::iterator i = find(fds.begin(), fds.end(), fd);
      if(i != fds.end()) fds.erase(i);
    }

    /**
     * Remove an item of the internal vector giving its
     * index position.
     * @param n Position of the item to remove.
     */
    void RemoveAt(int n)
    {
      fds.erase(fds.begin() + n);
    }

    /**
     * Indexing operator.
     */
    PollFD& operator[](int n)
    {
      return fds[n];
    }

    virtual ~PollTable()
    {
    }
  };

}

#endif /* _NET_POLL_TABLE_H_ */
