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
    using namespace std;
    using namespace data;
    using namespace jpeg2000;

    /**
     * Contains the core functionality of a (JPIP) data-bin server,
     * which maintains a cache model and is capable of generating
     * data chunks of variable length;
     */
    class DataBinServer {
    private:
        WOI woi;             ///< Current WOI
        int pending;         ///< Number of pending bytes
        vector<int> codestreams;
        bool has_woi;        ///< <code>true</code> if the last request contained a WOI
        bool metareq;        ///< <code>true</code> if the last request contained a "metareq"
        bool end_woi_;       ///< <code>true</code> if the WOI has been completely sent
        size_t current_idx;  ///< Current codestream index

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
        int WriteSegment(File::Ptr &file, int num_codestream, int id, const FileSegment &segment, int offset = 0, bool last = true) {
            int cached = cache_model.GetDataBin<BIN_CLASS>(num_codestream, id);
            int res = 1, seg_cached = cached - offset;

            if (cached != INT_MAX && (int) segment.length >= seg_cached) {
                int free = data_writer.GetFree() - MINIMUM_SPACE;

                if (free <= 0) {
                    eof = true;
                    res = 0;
                } else {
                    FileSegment part = FileSegment(segment.offset + seg_cached, segment.length - seg_cached);
                    if ((int) part.length > free) {
                        part.length = free;
                        last = false;
                        res = 0;
                    }

                    data_writer.SetCodestream(num_codestream);
                    data_writer.SetDataBinClass(BIN_CLASS);

                    if (!data_writer.Write(id, cached, *file, part, last)) res = -1;
                    else cache_model.AddToDataBin<BIN_CLASS>(num_codestream, id, part.length, last);
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
        int WritePlaceHolder(File::Ptr &file, int num_codestream, int id, const PlaceHolder &place_holder, int offset = 0, bool last = false) {
            int cached = cache_model.GetDataBin<DataBinClass::META_DATA>(num_codestream, id);
            int res = 1, seg_cached = cached - offset;

            if (cached != INT_MAX && place_holder.length() > seg_cached) {
                int free = data_writer.GetFree() - MINIMUM_SPACE - place_holder.length();

                if (free <= 0) {
                    eof = true;
                    res = 0;
                } else {
                    data_writer.SetCodestream(num_codestream);
                    data_writer.SetDataBinClass(DataBinClass::META_DATA);

                    if (!data_writer.WritePlaceHolder(id, cached, *file, place_holder, last)) res = -1;
                    else
                        cache_model.AddToDataBin<DataBinClass::META_DATA>(num_codestream, id, place_holder.length(), last);
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
            end_woi_ = false;
            metareq = false;
            current_idx = 0;
            eof = false;
        }

        /**
         * Resets the server assigning a new image to serve. It
         * also resets the maintained cache model.
         * @param image_index Pointer to the new image index to use.
         * @return <code>true</code> if successful.
         */
        void Reset();

        /**
         * Sets the new current request to take into account for
         * generating the chunks of data.
         * @param req Request.
         * @return <code>true</code> if successful.
         */
        bool SetRequest(FileManager &file_manager, const Request &req);

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
        bool GenerateChunk(FileManager &file_manager, char *buf, int *len, bool *last);

        virtual ~DataBinServer() {
        }
    };
}

#endif /* _JPIP_DATABIN_SERVER_H_ */
