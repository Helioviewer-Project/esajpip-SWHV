#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>
#include "jpip/source/source.h"
#include "woi.h"
#include "jpip/request/response_request.h"
#include "cache_model.h"
#include "woi_composer.h"
#include "databin_writer.h"
#include "jpip/index/image_index.h"

namespace jpip {

    /**
     * Contains the core functionality of a (JPIP) data-bin server,
     * which maintains a cache model and is capable of generating
     * data chunks of variable length;
     */
    class DataBinServer {
    private:
        struct Stream {
            const Source *file;
            WOIComposer composer;
            WOI woi;
            // WOIComposer visits the selected precinct bins in the same order
            // in every layer. Keep each bin's cumulative packet length so the
            // next layer's data-bin offset is available in constant time.
            std::vector<int> bin_offsets;
            size_t bin_idx;
            int id;
            bool empty;

            explicit Stream(int _id)
                    : file(NULL), bin_idx(0), id(_id), empty(true) {
            }
        };

        ImageIndex *target = NULL;
        std::string error;

        int pending;         ///< Number of pending bytes
        EOR limit_reason = EOR::RESPONSE_LIMIT_REACHED;
        std::vector<Stream> streams;
        std::deque<size_t> active_streams;
        bool has_woi;
        size_t header_idx;
        size_t meta_idx;
        int meta_offset;
        size_t meta_bin_idx;

        CacheModel cache_model;     ///< Cache model of the client
        DataBinWriter data_writer;  ///< Data-bin writer for generating the chunks

        enum {
            // Allows the current message header to grow to its maximum encoded
            // size and still leaves room for the three-byte EOR message.
            CHUNK_RESERVE = 60
        };

        enum class SegmentResult {
            FAILED,
            FULL,
            COMPLETE
        };

        template<DataBinClass BIN_CLASS>
        bool GetCachedRemainder(int num_codestream, int id, int offset,
                                int *remainder) {
            int cached = cache_model.GetDataBin(BIN_CLASS, num_codestream, id);
            if (cached < offset) {
                error = "Invalid cache-model offset";
                return false;
            }
            *remainder = cached == INT_MAX ? INT_MAX : cached - offset;
            return true;
        }

        int ContributionLength(uint64_t remaining) const {
            int available = data_writer.GetFree() - CHUNK_RESERVE;
            return available > 0
                    ? static_cast<int>(std::min(remaining, static_cast<uint64_t>(available)))
                    : -1;
        }

        template<DataBinClass BIN_CLASS>
        SegmentResult FinishContribution(DataBinWriter::Result result,
                int num_codestream, int id, int length, bool last, bool complete) {
            if (result == DataBinWriter::Result::FULL)
                return SegmentResult::FULL;
            if (result == DataBinWriter::Result::FAILED)
                return SegmentResult::FAILED;
            cache_model.AddToDataBin(BIN_CLASS, num_codestream, id, length, last);
            return complete ? SegmentResult::COMPLETE : SegmentResult::FULL;
        }

        // Write the uncached suffix, including an empty final contribution.
        template<DataBinClass BIN_CLASS>
        SegmentResult WriteSegment(const Source *file, int num_codestream, int id,
                                   const FileSegment &segment, int offset = 0,
                                   bool last = true) {
            int cached;
            if (!GetCachedRemainder<BIN_CLASS>(num_codestream, id, offset, &cached))
                return SegmentResult::FAILED;
            if (cached == INT_MAX || static_cast<uint64_t>(cached) > segment.length ||
                (static_cast<uint64_t>(cached) == segment.length && !last))
                return SegmentResult::COMPLETE;

            uint64_t remaining = segment.length - cached;
            int length = ContributionLength(remaining);
            if (length < 0)
                return SegmentResult::FULL;
            bool complete = static_cast<uint64_t>(length) == remaining;
            last = last && complete;
            FileSegment part(segment.offset + cached, length);
            DataBinWriter::Result result = data_writer.Write(
                    BIN_CLASS, num_codestream, id, offset + cached, *file, part, last);
            return FinishContribution<BIN_CLASS>(result, num_codestream, id,
                                                 length, last, complete);
        }

        // Placeholders contribute only to metadata bin 0 and never finish it.
        SegmentResult WritePlaceHolder(const Source *file,
                                       const PlaceHolder &place_holder, int offset) {
            int cached;
            if (!GetCachedRemainder<DataBinClass::META_DATA>(0, 0, offset, &cached))
                return SegmentResult::FAILED;
            // ValidateMetadata bounds the place-holder length to INT_MAX.
            int remaining = static_cast<int>(place_holder.length()) - cached;
            if (cached == INT_MAX || remaining <= 0)
                return SegmentResult::COMPLETE;
            int length = ContributionLength(remaining);
            if (length < 0)
                return SegmentResult::FULL;
            DataBinWriter::Result result = data_writer.WritePlaceHolder(
                    DataBinClass::META_DATA, 0, 0, offset, *file, place_holder,
                    cached, false, length);
            return FinishContribution<DataBinClass::META_DATA>(
                    result, 0, 0, length, false, length == remaining);
        }

        const Source *OpenSource(SourceProvider &sources, const std::string &path);

        SegmentResult WriteMetadata(SourceProvider &sources,
                                    ImageIndex *image_index);
        SegmentResult WriteHeaders(SourceProvider &sources,
                                   ImageIndex *image_index);
        SegmentResult WritePackets(SourceProvider &sources,
                                   ImageIndex *image_index);

    public:
        const std::string &GetError() const { return error; }
        /**
         * Initializes the object.
         */
        DataBinServer() {
            pending = 0;
            has_woi = false;
            header_idx = 0;
            meta_idx = 0;
            meta_offset = 0;
            meta_bin_idx = 0;
        }

        DataBinServer(const DataBinServer &) = delete;
        DataBinServer &operator=(const DataBinServer &) = delete;

        /**
         * Sets the new current request to take into account for
         * generating the chunks of data.
         * @param image_index Index of the selected image.
         * @param req ResponseRequest.
         */
        bool SetRequest(ImageIndex &image_index,
                        const ResponseRequest &req,
                        std::string *error_message = NULL);

        /**
         * Generates a new chunk of data for the current image and
         * WOI, according to the last indicated request.
         * @param buff Pointer to the memory buffer.
         * @param len Length of the memory buffer. It is modified
         * by the method to indicate how many bytes have been
         * written to the buffer.
         * @param last Output parameter to indicates if this is
         * the last chunk of data associated to the last request.
         * @return <code>true</code> if successful.
         */
        // Success produces bytes or completes the response. Failure sets len
        // to zero and last to false, supplies a nonempty GetError() diagnostic,
        // and terminates use of this session.
        bool GenerateChunk(SourceProvider &sources, char *buf, int *len,
                           bool *last);

    };
}
