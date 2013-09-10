#ifndef _DATA_VINT_VECTOR_H_
#define _DATA_VINT_VECTOR_H_


#include <vector>
#include <stdint.h>
#include <assert.h>
#include <algorithm>


namespace data
{
  using namespace std;


  /**
   * This class has been implemented with the same philosophy that the
   * class STL vector, but specifically designed to store integers
   * with a length in bytes that can be not multiple from 2 (e.g. integers
   * of 3 bytes). This class internally handles a vector of 1-byte integers.
   *
   * @see vector
   */
  class vint_vector
  {
  private:
    uint64_t mask;			///< Mask used for accessing the data
    int8_t num_bytes_;		///< Number of bytes used for the integers
    vector<uint8_t> data;	//< Internal vector used for the data

  public:
    /**
     * Initializes the vector to store 64-bit integers.
     */
    vint_vector()
    {
      set_num_bytes(sizeof(uint64_t));
    }

    /**
     * Initializes the vector to store integers with the
     * number of bytes given as parameter.
     * @param num_bytes Number of bytes of each integer.
     */
    vint_vector(int num_bytes)
    {
      set_num_bytes(num_bytes);
    }

    /**
     * Copy constructor.
     */
    vint_vector(const vint_vector& v)
    {
      *this = v;
    }

    /**
     * Copy assignment.
     */
    const vint_vector& operator=(const vint_vector& v)
    {
      mask = v.mask;
      num_bytes_ = v.num_bytes_;

      data.clear();
      for(vector<uint8_t>::const_iterator i = v.data.begin(); i != v.data.end(); i++)
        data.push_back(*i);

      return *this;
    }

    /**
     * Changes the number of bytes of the integer values. All the
     * current content is removed.
     * @param num_bytes New number of bytes to use.
     */
    void set_num_bytes(int num_bytes)
    {
      assert((num_bytes >= 1) && (num_bytes <= (int)sizeof(uint64_t)));

      data.clear();

      num_bytes_ = (int8_t)num_bytes;

      mask = 0xFF;
      while(--num_bytes > 0) mask = ((mask << 8) | 0xFF);
    }

    /**
     * Returns the number of bytes used.
     */
    int num_bytes() const
    {
      return (int)num_bytes_;
    }

    /**
     * Returns the current number of bytes stored.
     */
    int data_bytes() const
    {
      return (int)data.size();
    }

    /**
     * Operator overloading for indexing the integer values.
     * @param index Index of the item to return.
     * @return Value of the item, always as a <code>uint64_t</code>.
     */
    uint64_t operator[](int index) const
    {
      assert(((index * num_bytes_) + sizeof(uint64_t)) <= data.size());
      return (*((uint64_t *)&(data[index * (int)num_bytes_])) & mask);
    }

    /**
     * Adds a new item to the end of the vector.
     * @param value Value to add to the vector.
     */
    void push_back(uint64_t value)
    {
      data.insert(data.end(), data.size() <= 0 ? sizeof(uint64_t) : (int)num_bytes_, 0);
      *((uint64_t *)&(data[data.size() - sizeof(uint64_t)])) = value;
    }

    /**
     * Clears the content.
     */
    void clear()
    {
      data.clear();
    }

    /**
     * Returns the size of the vector, in number of items.
     */
    int size() const
    {
      return max(0, (((int)data.size() - (int)sizeof(uint64_t)) / (int)num_bytes_) + 1);
    }

    /**
     * Return the reference of the last item of the vector.
     */
    uint64_t& back()
    {
      assert(data.size() >= sizeof(uint64_t));
      return *((uint64_t *)&(data[data.size() - sizeof(uint64_t)]));
    }

    virtual ~vint_vector()
    {
    }
  };

}


#endif /* _DATA_VINT_VECTOR_H_ */
