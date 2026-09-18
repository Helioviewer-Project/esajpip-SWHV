#ifndef _JPIP_DATABIN_SERVER_H_
#define _JPIP_DATABIN_SERVER_H_

//#define SHOW_TRACES

#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "trace.h"
#include "data/file.h"
#include "woi.h"
#include "request.h"
#include "cache_model.h"
#include "woi_composer.h"
#include "databin_writer.h"
#include "jpeg2000/file_manager.h"
#include "jpeg2000/image_index.h"

namespace jpip {

    /**
     * Contains the core functionality of a (JPIP) data-bin server,
     * which maintains a cache model and is capable of generating
     * data chunks of variable length;
     */
    class DataBinServer {
    private:
        struct Stream {
            data::File *file;
            WOIComposer composer;
            WOI woi;
            int id;
            bool empty;

            explicit Stream(int _id) : file(NULL), id(_id), empty(true) {
            }
        };

        int pending;         ///< Number of pending bytes
        std::vector<Stream> streams;
        bool has_woi;
        size_t current_idx;  ///< Current codestream index
        size_t meta_idx;
        int meta_offset;
        size_t meta_bin_idx;

        CacheModel cache_model;     ///< Cache model of the client
        DataBinWriter data_writer;  ///< Data-bin writer for generating the chunks

        enum {
            CHUNK_RESERVE = 60 ///< Space for message headers and EOR
        };

        enum class SegmentResult {
            FAILED,
            FULL,
            COMPLETE
        };

        /**
         * Writes a new data-bin segment or a part of it that is not already cached.
         * @param num_codestream Index number of the codestream.
         * @param id Data-bin identifier.
         * @param segment File segment associated.
         * @param offset Data-bin offset of the data (0 by default).
         * @param last <code>true</code> if this is the last data of the data-bin.
         * @return Result of writing the uncached part of the segment.
         */
        template<int BIN_CLASS>
        SegmentResult WriteSegment(data::File *file, int num_codestream, int id,
                                   const data::FileSegment &segment, int offset = 0,
                                   bool last = true) {
            int cached = cache_model.GetDataBin(BIN_CLASS, num_codestream, id);
            int seg_cached = cached - offset;

            if (cached != INT_MAX &&
                (seg_cached < 0 ||
                 static_cast<uint64_t>(seg_cached) <= segment.length)) {
                if (seg_cached < 0)
                    seg_cached = 0;

                int free = data_writer.GetFree() - CHUNK_RESERVE;

                if (free <= 0)
                    return SegmentResult::FULL;

                data::FileSegment part(segment.offset + seg_cached,
                                       segment.length - seg_cached);
                bool complete = part.length <= static_cast<uint64_t>(free);
                if (!complete) {
                    part.length = free;
                    last = false;
                }

                DataBinWriter::Result result = data_writer.Write(
                        BIN_CLASS, num_codestream, id, cached, *file, part, last);
                if (result == DataBinWriter::Result::FULL)
                    return SegmentResult::FULL;
                if (result == DataBinWriter::Result::FAILED)
                    return SegmentResult::FAILED;

                cache_model.AddToDataBin(BIN_CLASS, num_codestream, id,
                                         part.length, last);
                return complete ? SegmentResult::COMPLETE : SegmentResult::FULL;
            }
            return SegmentResult::COMPLETE;
        }

        /**
         * Writes a new place-holder segment, only if it is possible to write it completely.
         * @param num_codestream Index number of the codestream.
         * @param id Data-bin identifier.
         * @param place_holder Place-holder information.
         * @param offset Data-bin offset of the data (0 by default).
         * @param last <code>true</code> if this is the last data of the data-bin.
         * @return Result of writing the uncached part of the place-holder.
         */
        SegmentResult WritePlaceHolder(data::File *file, int num_codestream, int id,
                                       const jpeg2000::PlaceHolder &place_holder,
                                       int offset = 0, bool last = false) {
            int cached = cache_model.GetDataBin(DataBinClass::META_DATA, num_codestream, id);
            int seg_cached = cached - offset;

            if (cached != INT_MAX && seg_cached < place_holder.length()) {
                if (seg_cached < 0)
                    seg_cached = 0;
                int remaining = place_holder.length() - seg_cached;
                if (data_writer.GetFree() - CHUNK_RESERVE < remaining)
                    return SegmentResult::FULL;

                DataBinWriter::Result result = data_writer.WritePlaceHolder(
                        DataBinClass::META_DATA, num_codestream, id, offset,
                        *file, place_holder, seg_cached, last);
                if (result == DataBinWriter::Result::FULL)
                    return SegmentResult::FULL;
                if (result == DataBinWriter::Result::FAILED)
                    return SegmentResult::FAILED;

                cache_model.AddToDataBin(DataBinClass::META_DATA, num_codestream, id,
                                         place_holder.length() - seg_cached, last);
            }
            return SegmentResult::COMPLETE;
        }

        SegmentResult WriteMetadata(jpeg2000::FileManager &file_manager,
                                    jpeg2000::ImageIndex *image_index);
        SegmentResult WriteHeaders(jpeg2000::FileManager &file_manager,
                                   jpeg2000::ImageIndex *image_index);
        SegmentResult WritePackets(jpeg2000::ImageIndex *image_index);

    public:
        /**
         * Initializes the object.
         */
        DataBinServer() {
            pending = 0;
            has_woi = false;
            current_idx = 0;
            meta_idx = 0;
            meta_offset = 0;
            meta_bin_idx = 0;
        }

        /**
         * Sets the new current request to take into account for
         * generating the chunks of data.
         * @param image_index Index of the selected image.
         * @param req Request.
         */
        bool SetRequest(const jpeg2000::ImageIndex &image_index,
                        const Request &req,
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
        bool GenerateChunk(jpeg2000::FileManager &file_manager, char *buf, int *len,
                           bool *last);

    };
}

#endif /* _JPIP_DATABIN_SERVER_H_ */
