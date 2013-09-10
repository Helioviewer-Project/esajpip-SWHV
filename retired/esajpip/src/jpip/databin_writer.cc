#include "databin_writer.h"


namespace jpip
{

  DataBinWriter& DataBinWriter::WriteVBAS(uint64_t value)
  {
    if(!eof) {
      if(ptr >= end) eof = true;
      else {
        int num = 0;
        uint8_t bytes[10];

        do {
          bytes[num++] = (uint8_t)(value & 0x0000007F);
          value >>= 7;
        } while(value);

        if((ptr + num) > end) eof = true;
        else {
          while(num-- > 1) *ptr++ = bytes[num] | 0x80;
          *ptr++ = bytes[num];
        }
      }
    }

    return *this;
  }

  DataBinWriter& DataBinWriter::WriteHeader(uint64_t bin_id, uint64_t bin_offset,
      uint64_t bin_length, bool last_byte)
  {
    if(!eof) {
      if(ptr >= end) eof = true;
      else {
        char *aux_ptr = ptr;

        int pres = 1;
        if(prev_databin_class != databin_class) pres = 2;
        if(prev_codestream_idx != codestream_idx) pres = 3;

        uint8_t first_b = (uint8_t)(pres << 5);
        if(last_byte) first_b |= (uint8_t)(1 << 4);

        if(!(bin_id >> 4)) {
          first_b |= (uint8_t)(bin_id & 0x0F);
          *ptr++ = first_b;

        } else {
          *ptr++ = (first_b | (uint8_t)0x80);
          WriteVBAS(bin_id);
        }

        if(pres >= 2)
        {
          WriteVBAS((uint64_t)databin_class);
          if(pres == 3) WriteVBAS((uint64_t)codestream_idx);
        }

        WriteVBAS(bin_offset);
        WriteVBAS(bin_length);

        if(eof) ptr = aux_ptr;
      }
    }

    return *this;
  }

  DataBinWriter& DataBinWriter::Write(uint64_t bin_id, uint64_t bin_offset,
      const File& file, const FileSegment& segment, bool last_byte)
  {
    char *aux_ptr = ptr;

    if(WriteHeader(bin_id, bin_offset, segment.length, last_byte))
    {
      if(segment.length > 0) {
        file.Seek(segment.offset);
        if((ptr + segment.length) >= end) eof = true;
        else if(!file.Read(ptr, segment.length)) eof = true;
        else ptr += segment.length;
      }

      if(eof) ptr = aux_ptr;
      else {
        prev_databin_class = databin_class;
        prev_codestream_idx = codestream_idx;
      }
    }

    return *this;
  }

  DataBinWriter& DataBinWriter::WritePlaceHolder(uint64_t bin_id, uint64_t bin_offset,
	  const File& file, const PlaceHolder& place_holder, bool last_byte)
  {
    char *aux_ptr = ptr;

    if(WriteHeader(bin_id, bin_offset, place_holder.length(), last_byte))
    {
      if((ptr + place_holder.length()) >= end) eof = true;
      else {
        /* LBox   */  WriteValue<uint32_t>(place_holder.length());
        /* TBox   */  WriteValue<uint32_t>(0x70686c64);
        /* Flags  */  WriteValue<uint32_t>(place_holder.is_jp2c ? 4 : 1);
        /* OrigID */  WriteValue<uint64_t>(place_holder.is_jp2c ? 0 : place_holder.id);

        /* OrigBH */
        if(place_holder.header.length > 0) {
          file.Seek(place_holder.header.offset);
          if((ptr + place_holder.header.length) >= end) eof = true;
          else if(!file.Read(ptr, place_holder.header.length)) eof = true;
          else ptr += place_holder.header.length;
        }

        /* EquivID */ WriteValue<uint64_t>(0);
        /* EquivBH */ WriteValue<uint64_t>(0);
        /* CSID    */ WriteValue<uint64_t>(place_holder.is_jp2c ? place_holder.id : 0);

        if(eof) ptr = aux_ptr;
        else {
          prev_databin_class = databin_class;
          prev_codestream_idx = codestream_idx;
        }
      }
    }

    return *this;
  }

  DataBinWriter& DataBinWriter::WriteEmpty(uint64_t bin_id)
  {
    File aux_file;
    return this->Write(bin_id, 0, aux_file, FileSegment::Null, true);
  }

}
