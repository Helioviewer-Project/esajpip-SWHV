#ifndef _JPIP_DATABIN_WRITER_H_
#define _JPIP_DATABIN_WRITER_H_


#include <stdint.h>
#include "jpip.h"
#include "data/file.h"
#include "data/file_segment.h"
#include "jpeg2000/place_holder.h"


namespace jpip
{

  using namespace std;
  using namespace data;
  using namespace jpeg2000;


  /**
   * Class used to generate data-bin segments and write them
   * into a memory buffer.
   *
   * @see DataBinServer
   * @see DataBinClass
   * @see EOR
   */
  class DataBinWriter
  {
  private:
	/**
	 * <code>true</code> if the end of the buffer has been reached and
	 * the last value could not be written.
	 */
    bool eof;
    char *ini;			///< Pointer to the beginning of the buffer
    char *ptr;			///< Current position of the buffer
    char *end;			///< Pointer to the end of the buffer

    int databin_class;			///< Current data-bin class
    int codestream_idx;			///< Current codestream index number
    int prev_databin_class;		///< Previous data-bin class
    int prev_codestream_idx;	///< Previous codestream index number

    /**
     * Writes a value into the buffer.
     * @param value Value to write.
     * @return The object itself.
     */
    template<typename T> DataBinWriter& WriteValue(T value)
    {
      if(!eof) {
        if((ptr + sizeof(T)) >= end) eof = true;
        else {
   		  for (int i = sizeof(T) - 1; i >= 0; i--)
   			  *ptr++ = (value >> (8 * i)) & 0xFF;
        }
      }

      return *this;
    }

    /**
     * Writes a new integer value into the buffer coded as VBAS.
     * @param value Value to write.
     * @return The object itself.
     */
    DataBinWriter& WriteVBAS(uint64_t value);

    /**
     * Writes a data-bin header into the buffer.
     * @param bin_id Data-bin identifier.
     * @param bin_offset Data-bin offset.
     * @param bin_length Data-bin length.
     * @param last_byte <code>true</code> if the data related
     * to this header contains the last byte of the data-bin.
     * @return The object itself.
     */
    DataBinWriter& WriteHeader(uint64_t bin_id, uint64_t bin_offset,
        uint64_t bin_length, bool last_byte = false);

  public:
    /**
     * Initializes the object.
     */
    DataBinWriter()
    {
      eof = true;
      databin_class = -1;
      codestream_idx = -1;
      prev_databin_class = -1;
      prev_codestream_idx = -1;
      ini = ptr = end = NULL;
    }

    /**
     * Sets the associated memory buffer.
     * @param buf Memory buffer.
     * @param buf_len Length of the memory buffer.
     * @return The object itself.
     */
    DataBinWriter& SetBuffer(char *buf, int buf_len)
    {
      eof = false;
      ini = ptr = buf;
      end = ini + buf_len;

      return *this;
    }

    /**
     * Clears the previous identifiers of data-bin
     * class and codestream index numbers.
     * @return The object itself.
     */
    DataBinWriter& ClearPreviousIds()
    {
      databin_class = -1;
      codestream_idx = -1;
      prev_databin_class = -1;
      prev_codestream_idx = -1;

      return *this;
    }

    /**
     * Sets the current codestream.
     * @param value Index number of the codestream.
     * @return The object itself.
     */
    DataBinWriter& SetCodestream(int value)
    {
      if(value < 0) value = 0;
      codestream_idx = value;
      return *this;
    }

    /**
     * Sets the current data-bin class.
     * @param databin_class Data-bin class.
     * @return The object itself.
     */
    DataBinWriter& SetDataBinClass(int databin_class)
    {
      this->databin_class = databin_class;
      return *this;
    }

    /**
     * Writes a data-bin segment into the buffer.
     * @param bin_id Data-bin identifier.
     * @param bin_offset Data-bin offset.
     * @param file File from where to read the data.
     * @param segment File segment of the data.
     * @param last_byte <code>true</code> if the data
     * contains the last byte of the data-bin.
     * @return The object itself.
     */
    DataBinWriter& Write(uint64_t bin_id, uint64_t bin_offset,
        const File& file, const FileSegment& segment,
        bool last_byte = false);

    /**
     * Writes a place-holder segment into the buffer.
     * @param bin_id Data-bin identifier.
     * @param bin_offset Data-bin offset.
     * @param file File from where to read the data.
     * @param place_holder Place-holder information.
     * @param last_byte <code>true</code> if the data
     * contains the last byte of the data-bin.
     * @return The object itself.
     */
    DataBinWriter& WritePlaceHolder(uint64_t bin_id, uint64_t bin_offset,
    	const File& file, const PlaceHolder& place_holder,
    	bool last_byte = false);

    /**
     * Writes an empty segment.
     * @param bin_id Data-bin identifier.
     * @return The object itself.
     */
    DataBinWriter& WriteEmpty(uint64_t bin_id = 0);

    /**
     * Returns the number of bytes written.
     */
    int GetCount() const
    {
    	return (ptr - ini);
    }

    /**
     * Returns the number of bytes available.
     */
    int GetFree() const
    {
      return (end - ptr);
    }

    /**
     * Writes a EOR message into the buffer.
     * @param reason Reason of the message.
     * @return The object itself.
     */
    DataBinWriter& WriteEOR(int reason)
    {
      if((ptr + 3) > end) eof = true;
      else {
        *ptr++ = 0;
        *ptr++ = (char)reason;
        *ptr++ = 0;
      }

    	return *this;
    }

    /**
     * Returns the <code>EOF</code> status of the
     * object.
     */
    operator bool() const
    {
      return !eof;
    }

    virtual ~DataBinWriter()
    {
    }
  };

}


#endif /* _JPIP_DATABIN_WRITER_H_ */
