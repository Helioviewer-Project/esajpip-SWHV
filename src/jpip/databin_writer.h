#ifndef _JPIP_DATABIN_WRITER_H_
#define _JPIP_DATABIN_WRITER_H_

#include <cstddef>
#include <cstdint>
#include "jpip.h"
#include "data/file.h"
#include "data/file_segment.h"
#include "jpeg2000/place_holder.h"

namespace jpip {

    /**
     * Class used to generate data-bin segments and write them
     * into a memory buffer.
     *
     * @see DataBinServer
     * @see DataBinClass
     * @see EOR
     */
    class DataBinWriter {
    public:
        enum class Result {
            WRITTEN, ///< The complete value was written
            FULL,    ///< The complete value does not fit in this buffer
            FAILED   ///< The message could not be encoded
        };

        enum {
            EOR_LENGTH = 3
        };

    private:
        char *ini;            ///< Pointer to the beginning of the buffer
        char *ptr;            ///< Current position of the buffer
        char *end;            ///< Pointer to the end of the buffer

        int prev_databin_class;        ///< Previous data-bin class
        int prev_codestream_idx;    ///< Previous codestream index number

        char *msg_start;
        int msg_databin_class;
        int msg_codestream_idx;
        size_t msg_header_len;
        uint64_t msg_bin;
        uint64_t msg_offset;
        uint64_t msg_len;
        bool msg_last;

        Result BeginMessage(int databin_class, int codestream_idx,
                            uint64_t bin_id, uint64_t bin_offset,
                            uint64_t bin_length, bool last_byte);
        void FinishMessage();
        size_t HeaderLength(uint64_t bin_id, uint64_t bin_offset,
                            uint64_t bin_length) const;

        /**
         * Writes a new integer value into the buffer coded as VBAS.
         * @param value Value to write.
         */
        void WriteVBAS(uint64_t value);

        /**
         * Writes a data-bin header into the buffer.
         * @param bin_id Data-bin identifier.
         * @param bin_offset Data-bin offset.
         * @param bin_length Data-bin length.
         * @param last_byte <code>true</code> if the data related
         * to this header contains the last byte of the data-bin.
         */
        void WriteHeader(uint64_t bin_id, uint64_t bin_offset,
                         uint64_t bin_length, bool last_byte = false);

    public:
        /**
         * Initializes the object.
         */
        DataBinWriter() {
            prev_databin_class = -1;
            prev_codestream_idx = -1;
            msg_start = NULL;
            msg_databin_class = -1;
            msg_codestream_idx = -1;
            msg_header_len = 0;
            msg_bin = 0;
            msg_offset = 0;
            msg_len = 0;
            msg_last = false;
            ini = ptr = end = NULL;
        }

        /**
         * Sets the associated memory buffer.
         * @param buf Memory buffer.
         * @param buf_len Length of the memory buffer.
         */
        void SetBuffer(char *buf, int buf_len) {
            ini = ptr = buf;
            end = ini + buf_len;
            msg_start = NULL;
        }

        /**
         * Starts a new response with no previous data-bin identifiers.
         */
        void StartResponse() {
            prev_databin_class = -1;
            prev_codestream_idx = -1;
        }

        /**
         * Writes a data-bin segment into the buffer.
         * @param databin_class Data-bin class.
         * @param codestream_idx Codestream index number.
         * @param bin_id Data-bin identifier.
         * @param bin_offset Data-bin offset.
         * @param file File from where to read the data.
         * @param segment File segment of the data.
         * @param last_byte <code>true</code> if the data
         * contains the last byte of the data-bin.
         * @return Result of the write operation.
         */
        Result Write(int databin_class, int codestream_idx, uint64_t bin_id,
                     uint64_t bin_offset, data::File &file,
                     const data::FileSegment &segment,
                     bool last_byte = false);

        /**
         * Writes a place-holder segment into the buffer.
         * @param databin_class Data-bin class.
         * @param codestream_idx Codestream index number.
         * @param bin_id Data-bin identifier.
         * @param bin_offset Data-bin offset.
         * @param file File from where to read the data.
         * @param place_holder Place-holder information.
         * @param last_byte <code>true</code> if the data
         * contains the last byte of the data-bin.
         * @return Result of the write operation.
         */
        Result WritePlaceHolder(int databin_class, int codestream_idx,
                                uint64_t bin_id, uint64_t bin_offset,
                                data::File &file,
                                const jpeg2000::PlaceHolder &place_holder,
                                uint64_t skip = 0,
                                bool last_byte = false);

        /**
         * Finishes the current message and returns the number of bytes written.
         */
        ptrdiff_t Finalize() {
            FinishMessage();
            return ptr - ini;
        }

        /**
         * Returns the number of bytes available.
         */
        ptrdiff_t GetFree() const {
            return end - ptr;
        }

        /**
         * Writes an EOR message into the buffer.
         * @param reason Reason of the message.
         * @return <code>true</code> if the message was written.
         */
        bool WriteEOR(int reason) {
            FinishMessage();
            if (end - ptr < EOR_LENGTH)
                return false;
            *ptr++ = 0;
            *ptr++ = (char) reason;
            *ptr++ = 0;
            return true;
        }

    };
}

#endif /* _JPIP_DATABIN_WRITER_H_ */
