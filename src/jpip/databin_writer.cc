#include "databin_writer.h"

#include <cstring>

namespace jpip {

    using data::File;
    using data::FileSegment;
    using jpeg2000::PlaceHolder;

    static size_t VBASLength(uint64_t value) {
        size_t length = 1;
        while (value >>= 7)
            length++;
        return length;
    }

    static size_t BinIDLength(uint64_t value) {
        size_t length = 1;
        while (value >= 16) {
            value >>= 7;
            length++;
        }
        return length;
    }

    size_t DataBinWriter::HeaderLength(uint64_t bin_id, uint64_t bin_offset,
                                       uint64_t bin_length) const {
        int pres = 1;
        if (prev_databin_class != msg_databin_class)
            pres = 2;
        if (prev_codestream_idx != msg_codestream_idx)
            pres = 3;

        size_t length = BinIDLength(bin_id);
        if (pres >= 2)
            length += VBASLength(msg_databin_class);
        if (pres == 3)
            length += VBASLength(msg_codestream_idx);
        return length + VBASLength(bin_offset) + VBASLength(bin_length);
    }

    DataBinWriter::Result DataBinWriter::BeginMessage(
            int databin_class, int codestream_idx, uint64_t bin_id,
            uint64_t bin_offset, uint64_t bin_length, bool last_byte) {
        if (codestream_idx < 0)
            codestream_idx = 0;
        if (msg_start != NULL && msg_databin_class == databin_class &&
            msg_codestream_idx == codestream_idx && !msg_last &&
            msg_bin == bin_id && msg_len <= UINT64_MAX - msg_offset &&
            msg_offset + msg_len == bin_offset) {
            if (bin_length > UINT64_MAX - msg_len) {
                FinishMessage();
                return Result::FAILED;
            }

            size_t header_len = HeaderLength(msg_bin, msg_offset,
                                             msg_len + bin_length);
            size_t header_growth = 0;
            if (header_len > msg_header_len)
                header_growth = header_len - msg_header_len;
            uint64_t available = end - ptr;
            if (bin_length <= available &&
                header_growth <= available - bin_length)
                return Result::WRITTEN;

            FinishMessage();
            return Result::FULL;
        }

        FinishMessage();
        msg_databin_class = databin_class;
        msg_codestream_idx = codestream_idx;
        size_t header_len = HeaderLength(bin_id, bin_offset, bin_length);
        uint64_t available = end - ptr;
        if (bin_length > available || header_len > available - bin_length)
            return Result::FULL;

        msg_start = ptr;
        WriteHeader(bin_id, bin_offset, bin_length, last_byte);
        msg_header_len = ptr - msg_start;
        msg_bin = bin_id;
        msg_offset = bin_offset;
        msg_len = 0;
        msg_last = false;
        return Result::WRITTEN;
    }

    void DataBinWriter::FinishMessage() {
        if (msg_start == NULL)
            return;

        char *payload = msg_start + msg_header_len;
        size_t payload_len = ptr - payload;
        size_t header_len = HeaderLength(msg_bin, msg_offset, msg_len);
        if (header_len != msg_header_len)
            memmove(msg_start + header_len, payload, payload_len);
        ptr = msg_start;
        WriteHeader(msg_bin, msg_offset, msg_len, msg_last);
        ptr += payload_len;

        prev_databin_class = msg_databin_class;
        prev_codestream_idx = msg_codestream_idx;
        msg_start = NULL;
    }

    void DataBinWriter::WriteVBAS(uint64_t value) {
        int num = 0;
        uint8_t bytes[10];

        do {
            bytes[num++] = (uint8_t) (value & 0x0000007F);
            value >>= 7;
        } while (value);

        while (num-- > 1) *ptr++ = bytes[num] | (uint8_t) 0x80;
        *ptr++ = bytes[num];
    }

    void DataBinWriter::WriteHeader(uint64_t bin_id, uint64_t bin_offset,
                                    uint64_t bin_length, bool last_byte) {
        int pres = 1;
        if (prev_databin_class != msg_databin_class) pres = 2;
        if (prev_codestream_idx != msg_codestream_idx) pres = 3;

        uint8_t first_b = (uint8_t) (pres << 5);
        if (last_byte) first_b |= (uint8_t) (1 << 4);

        size_t bin_id_length = BinIDLength(bin_id);
        unsigned shift = 7 * (bin_id_length - 1);
        first_b |= (uint8_t) ((bin_id >> shift) & 0x0F);
        if (shift != 0)
            first_b |= (uint8_t) 0x80;
        *ptr++ = first_b;
        while (shift != 0) {
            shift -= 7;
            uint8_t byte = (uint8_t) ((bin_id >> shift) & 0x7F);
            if (shift != 0)
                byte |= (uint8_t) 0x80;
            *ptr++ = byte;
        }

        if (pres >= 2) {
            WriteVBAS((uint64_t) msg_databin_class);
            if (pres == 3) WriteVBAS((uint64_t) msg_codestream_idx);
        }

        WriteVBAS(bin_offset);
        WriteVBAS(bin_length);
    }

    DataBinWriter::Result DataBinWriter::Write(
            int databin_class, int codestream_idx, uint64_t bin_id,
            uint64_t bin_offset, File &file, const FileSegment &segment,
            bool last_byte) {
        Result result = BeginMessage(databin_class, codestream_idx, bin_id,
                                     bin_offset, segment.length, last_byte);
        if (result != Result::WRITTEN)
            return result;

        char *aux_ptr = ptr;
        if (segment.length > 0) {
            file.Seek(segment.offset);
            if (!file.Read(ptr, segment.length)) {
                ptr = aux_ptr;
                return Result::FAILED;
            }
            ptr += segment.length;
        }

        msg_len += segment.length;
        msg_last = last_byte;
        return Result::WRITTEN;
    }

    DataBinWriter::Result DataBinWriter::WritePlaceHolder(
            int databin_class, int codestream_idx, uint64_t bin_id,
            uint64_t bin_offset, File &file, const PlaceHolder &place_holder,
            uint64_t skip, bool last_byte) {
        unsigned char encoded[60];
        const uint64_t placeholder_length = place_holder.length();
        if (placeholder_length > sizeof encoded || skip > placeholder_length)
            return Result::FAILED;

        unsigned char *out = encoded;
        auto write_value = [&out](uint64_t value, size_t size) {
            for (size_t i = size; i > 0; --i)
                *out++ = (value >> (8 * (i - 1))) & 0xFF;
        };
        write_value(placeholder_length, 4); // LBox
        write_value(0x70686c64, 4);            // TBox
        write_value(place_holder.is_jp2c ? 4 : 1, 4); // Flags
        write_value(place_holder.is_jp2c ? 0 : place_holder.id, 8); // OrigID
        if (place_holder.header.length > 0) {
            file.Seek(place_holder.header.offset);
            if (!file.Read(out, place_holder.header.length))
                return Result::FAILED;
            out += place_holder.header.length;
        }
        if (place_holder.is_jp2c) {
            write_value(0, 8);                 // EquivID
            write_value(0, 8);                 // EquivBH
            write_value(place_holder.id, 8);   // CSID
        }

        const uint64_t remaining = placeholder_length - skip;
        Result result = BeginMessage(databin_class, codestream_idx, bin_id,
                                     bin_offset + skip, remaining, last_byte);
        if (result != Result::WRITTEN)
            return result;

        memcpy(ptr, encoded + skip, remaining);
        ptr += remaining;
        msg_len += remaining;
        msg_last = last_byte;
        return Result::WRITTEN;
    }

}
