#ifndef _JPEG2000_PLACE_HOLDER_H_
#define _JPEG2000_PLACE_HOLDER_H_


#include "data/file_segment.h"


namespace jpeg2000
{

  using namespace data;


  /**
   * Contains the information of a place-holder. This class
   * can be printed and serialized.
   */
  class PlaceHolder
  {
  public:
    int id;					///< Place-holder identifier.
    bool is_jp2c;			///< <code>true</code> if refers to a codestream.
    FileSegment header;		///< File segment associated to the box header
    uint64_t data_length;	///< Length of the place-holder data


    /**
     * Initializes the object.
     */
    PlaceHolder()
    {
      id = 0;
      is_jp2c = false;
      data_length = 0;
    }

    /**
     * Initializes the object.
     * @param id Place-holder identifier.
     * @param is_jp2c Indicates if is a codestream place-holder.
     * @param header File segment of the associated header.
     * @param data_length Length of the place-holder data.
     */
    PlaceHolder(int id, bool is_jp2c, const FileSegment &header, uint64_t data_length)
    {
      this->id = id;
      this->is_jp2c = is_jp2c;
      this->header = header;
      this->data_length = data_length;
    }

    /**
     * Copy constructor.
     */
    PlaceHolder(const PlaceHolder& place_holder)
    {
      *this = place_holder;
    }

    template<typename T> T& SerializeWith(T& stream)
    {
      return (stream & id & is_jp2c & header & data_length);
    }

    /**
     * Copy assignment.
     */
    PlaceHolder& operator=(const PlaceHolder& place_holder)
    {
      id = place_holder.id;
      is_jp2c = place_holder.is_jp2c;
      header = place_holder.header;
      data_length = place_holder.data_length;

      return *this;
    }

    friend ostream& operator << (ostream &out, const PlaceHolder &place_holder)
    {
      out << "Id: " << place_holder.id << endl;
      out << "JP2C: " << (place_holder.is_jp2c ? "Yes" : "No") << endl;
      out << "Header: " << place_holder.header << endl;
      out << "Data length: " << place_holder.data_length << endl;

      return out;
    }

    /**
     * Returns the length of the place-holder.
     */
    int length() const
    {
    	return (44 + header.length);
    }

    virtual ~PlaceHolder()
    {
    }
  };

}


#endif /* _JPEG2000_PLACE_HOLDER_H_ */
