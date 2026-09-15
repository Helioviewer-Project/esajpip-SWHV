#ifndef _JPIP_DATABIN_SERVER_H_
#define _JPIP_DATABIN_SERVER_H_

//#define SHOW_TRACES

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
        WOI woi;             ///< Current WOI
        int pending;         ///< Number of pending bytes
        std::vector<int> codestreams;
        std::vector<data::File *> files;
        bool has_woi;        ///< <code>true</code> if the last request contained a WOI
        size_t current_idx;  ///< Current codestream index
        size_t meta_idx;
        int meta_offset;
        size_t meta_bin_idx;
        bool meta_bin0_done;

        /**
         * <code>true</code> if the end has been reached and the last write operation
         * could not be completed.
         */
        bool eof;

        CacheModel cache_model;     ///< Cache model of the client
        WOIComposer woi_composer;   ///< WOI composer for determining the packets
        DataBinWriter data_writer;  ///< Data-bin writer for generating the chunks

        enum {
            MINIMUM_SPACE = 60        ///< Minimum space in the chunk
        };

        /**
         * Writes a new data-bin segment or a part of it that is not already cached.
         * @param num_codestream Index number of the codestream.
         * @param id Data-bin identifier.
         * @param segment File segment associated.
         * @param offset Data-bin offset of the data (0 by default).
         * @param last <code>true</code> if this is the last data of the data-bin.
         * @return 1 if the segment content was completely written and/or cached,
         * 0 if it was incompletely written (or not at all, if EOF flag is set),
         * or -1 if an error was generated.
         */
        template<int BIN_CLASS>
        int WriteSegment(data::File *file, int num_codestream, int id,
                         const data::FileSegment &segment, int offset = 0,
                         bool last = true) {
            int cached = cache_model.GetDataBin(BIN_CLASS, num_codestream, id);
            int res = 1, seg_cached = cached - offset;

            if (cached != INT_MAX && seg_cached <= (int) segment.length) {
                if (seg_cached < 0)
                    seg_cached = 0;

                int free = data_writer.GetFree() - MINIMUM_SPACE;

                if (free <= 0) {
                    eof = true;
                    res = 0;
                } else {
                    data::FileSegment part = data::FileSegment(
                            segment.offset + seg_cached, segment.length - seg_cached);
                    if ((int) part.length > free) {
                        part.length = free;
                        last = false;
                        res = 0;
                    }

                    data_writer.Write(BIN_CLASS, num_codestream, id, cached,
                                      *file, part, last);
                    if (!data_writer.IsValid()) res = -1;
                    else cache_model.AddToDataBin(BIN_CLASS, num_codestream, id, part.length, last);
                }
            }
            return res;
        }

        /**
         * Writes a new place-holder segment, only if it is possible to write it completely.
         * @param num_codestream Index number of the codestream.
         * @param id Data-bin identifier.
         * @param place_holder Place-holder information.
         * @param offset Data-bin offset of the data (0 by default).
         * @param last <code>true</code> if this is the last data of the data-bin.
         * @return 1 if the segment content was completely written and/or cached,
         * 0 if it was incompletely written (or not at all, if EOF flag is set),
         * or -1 if an error was generated.
         */
        int WritePlaceHolder(data::File *file, int num_codestream, int id,
                             const jpeg2000::PlaceHolder &place_holder,
                             int offset = 0, bool last = false) {
            int cached = cache_model.GetDataBin(DataBinClass::META_DATA, num_codestream, id);
            int res = 1, seg_cached = cached - offset;

            if (cached != INT_MAX && seg_cached < place_holder.length()) {
                int free = data_writer.GetFree() - MINIMUM_SPACE - place_holder.length();

                if (free <= 0) {
                    eof = true;
                    res = 0;
                } else {
                    data_writer.WritePlaceHolder(DataBinClass::META_DATA,
                                                   num_codestream, id, cached,
                                                   *file, place_holder, last);
                    if (!data_writer.IsValid()) res = -1;
                    else
                        cache_model.AddToDataBin(DataBinClass::META_DATA, num_codestream, id,
                                                 place_holder.length(), last);
                }
            }
            return res;
        }

    public:
        /**
         * Initializes the obect.
         */
        DataBinServer() {
            pending = 0;
            has_woi = false;
            current_idx = 0;
            meta_idx = 0;
            meta_offset = 0;
            meta_bin_idx = 0;
            meta_bin0_done = false;
            eof = false;
        }

        /**
         * Sets the new current request to take into account for
         * generating the chunks of data.
         * @param req Request.
         */
        bool SetRequest(jpeg2000::FileManager &file_manager, const Request &req);

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
