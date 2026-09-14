#include "databin_writer.h"

#include <cstring>

namespace jpip {

    static size_t VBASLength(uint64_t value) {
        size_t length = 1;
        while (value >>= 7)
            length++;
        return length;
    }

    size_t DataBinWriter::HeaderLength(uint64_t bin_id, uint64_t bin_offset,
                                       uint64_t bin_length) const {
        int pres = 1;
        if (prev_databin_class != databin_class)
            pres = 2;
        if (prev_codestream_idx != codestream_idx)
            pres = 3;

        size_t length = 1;
        if (bin_id >= 16)
            length += VBASLength(bin_id);
        if (pres >= 2)
            length += VBASLength(databin_class);
        if (pres == 3)
            length += VBASLength(codestream_idx);
        return length + VBASLength(bin_offset) + VBASLength(bin_length);
    }

    bool DataBinWriter::BeginMessage(uint64_t bin_id, uint64_t bin_offset,
                                     uint64_t bin_length, bool last_byte) {
        if (msg_start != NULL && !msg_last && msg_bin == bin_id &&
            msg_offset + msg_len == bin_offset)
            return true;

        FinishMessage();
        size_t header_len = HeaderLength(bin_id, bin_offset, bin_length);
        if ((ptr + header_len) > end) {
            eof = true;
            return false;
        }

        msg_start = ptr;
        WriteHeader(bin_id, bin_offset, bin_length, last_byte);
        msg_header_len = ptr - msg_start;
        msg_bin = bin_id;
        msg_offset = bin_offset;
        msg_len = 0;
        msg_last = false;
        return true;
    }

    void DataBinWriter::FinishMessage() {
        if (msg_start == NULL)
            return;

        char *payload = msg_start + msg_header_len;
        size_t payload_len = ptr - payload;
        size_t header_len = HeaderLength(msg_bin, msg_offset, msg_len);
        if (header_len > msg_header_len &&
            ptr + header_len - msg_header_len > end) {
            eof = true;
            return;
        }
        if (header_len != msg_header_len)
            memmove(msg_start + header_len, payload, payload_len);
        ptr = msg_start;
        WriteHeader(msg_bin, msg_offset, msg_len, msg_last);
        ptr += payload_len;

        prev_databin_class = databin_class;
        prev_codestream_idx = codestream_idx;
        msg_start = NULL;
    }

    void DataBinWriter::WriteVBAS(uint64_t value) {
        if (!eof) {
            if (ptr >= end) eof = true;
            else {
                int num = 0;
                uint8_t bytes[10];

                do {
                    bytes[num++] = (uint8_t) (value & 0x0000007F);
                    value >>= 7;
                } while (value);

                if ((ptr + num) > end) eof = true;
                else {
                    while (num-- > 1) *ptr++ = bytes[num] | (uint8_t) 0x80;
                    *ptr++ = bytes[num];
                }
            }
        }
    }

    void DataBinWriter::WriteHeader(uint64_t bin_id, uint64_t bin_offset,
                                    uint64_t bin_length, bool last_byte) {
        if (!eof) {
            if (ptr >= end) eof = true;
            else {
                char *aux_ptr = ptr;

                int pres = 1;
                if (prev_databin_class != databin_class) pres = 2;
                if (prev_codestream_idx != codestream_idx) pres = 3;

                uint8_t first_b = (uint8_t) (pres << 5);
                if (last_byte) first_b |= (uint8_t) (1 << 4);

                if (!(bin_id >> 4)) {
                    first_b |= (uint8_t) (bin_id & 0x0F);
                    *ptr++ = first_b;

                } else {
                    *ptr++ = (first_b | (uint8_t) 0x80);
                    WriteVBAS(bin_id);
                }

                if (pres >= 2) {
                    WriteVBAS((uint64_t) databin_class);
                    if (pres == 3) WriteVBAS((uint64_t) codestream_idx);
                }

                WriteVBAS(bin_offset);
                WriteVBAS(bin_length);

                if (eof) ptr = aux_ptr;
            }
        }
    }

    void DataBinWriter::Write(uint64_t bin_id, uint64_t bin_offset, File &file,
                              const FileSegment &segment, bool last_byte) {
        if (BeginMessage(bin_id, bin_offset, segment.length, last_byte)) {
            char *aux_ptr = ptr;
            if (segment.length > 0) {
                file.Seek(segment.offset);
                if ((ptr + segment.length) > end) eof = true;
                else if (!file.Read(ptr, segment.length)) eof = true;
                else ptr += segment.length;
            }

            if (eof) ptr = aux_ptr;
            else {
                msg_len += segment.length;
                msg_last = last_byte;
            }
        }

    }

    void DataBinWriter::WritePlaceHolder(uint64_t bin_id, uint64_t bin_offset,
                                         File &file, const PlaceHolder &place_holder,
                                         bool last_byte) {
        if (BeginMessage(bin_id, bin_offset, place_holder.length(), last_byte)) {
            char *aux_ptr = ptr;
            if ((ptr + place_holder.length()) > end) eof = true;
            else {
                /* LBox   */  WriteValue<uint32_t>(place_holder.length());
                /* TBox   */  WriteValue<uint32_t>(0x70686c64);
                /* Flags  */  WriteValue<uint32_t>(place_holder.is_jp2c ? 4 : 1);
                /* OrigID */  WriteValue<uint64_t>(place_holder.is_jp2c ? 0 : place_holder.id);

                /* OrigBH */
                if (place_holder.header.length > 0) {
                    file.Seek(place_holder.header.offset);
                    if (ptr + place_holder.header.length > end) eof = true;
                    else if (!file.Read(ptr, place_holder.header.length)) eof = true;
                    else ptr += place_holder.header.length;
                }

                if (place_holder.is_jp2c) {
                    /* EquivID */ WriteValue<uint64_t>(0);
                    /* EquivBH */ WriteValue<uint64_t>(0);
                    /* CSID    */ WriteValue<uint64_t>(place_holder.id);
                }

                if (eof) ptr = aux_ptr;
                else {
                    msg_len += place_holder.length();
                    msg_last = last_byte;
                }
            }
        }
    }

}
