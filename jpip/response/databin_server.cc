#include "databin_server.h"

#include <algorithm>
#include <climits>
#include <cstdint>

using namespace std;

namespace jpip {

    namespace {

        bool MapInterval(int selected_size, int requested_size,
                         int *offset, int *length) {
            if (selected_size <= 0 || requested_size <= 0 || *offset < 0 ||
                *length < 0 || *offset > requested_size ||
                *length > requested_size - *offset)
                return false;

            uint64_t end = static_cast<uint64_t>(*offset) + *length;
            int mapped_offset = static_cast<int>(
                    static_cast<uint64_t>(*offset) * selected_size /
                    requested_size);
            uint64_t mapped_end = end * selected_size;
            mapped_end = (mapped_end + requested_size - 1) / requested_size;
            *offset = mapped_offset;
            *length = static_cast<int>(mapped_end) - mapped_offset;
            return true;
        }

        bool MapWindow(const ResponseRequest &request,
                       const CodingParameters &coding_parameters,
                       WOI *woi, Size *resolution_size) {
            WOI mapped = *woi;

            if (request.round_direction == ResponseRequest::CLOSEST)
                mapped.resolution = coding_parameters.GetClosestResolution(
                        request.resolution_size, resolution_size);
            else if (request.round_direction == ResponseRequest::ROUNDUP)
                mapped.resolution = coding_parameters.GetRoundUpResolution(
                        request.resolution_size, resolution_size);
            else
                mapped.resolution = coding_parameters.GetRoundDownResolution(
                        request.resolution_size, resolution_size);

            if (request.resolution_size.x > 0 &&
                request.resolution_size.y > 0 &&
                request.resolution_size != *resolution_size) {
                if (!MapInterval(resolution_size->x,
                                 request.resolution_size.x,
                                 &mapped.position.x, &mapped.size.x) ||
                    !MapInterval(resolution_size->y,
                                 request.resolution_size.y,
                                 &mapped.position.y, &mapped.size.y))
                    return false;
            }
            *woi = mapped;
            return true;
        }

        bool CropWindow(WOI *woi, const Size &bounds,
                        bool extend_to_edge = false) {
            int64_t left = max<int64_t>(0, woi->position.x);
            int64_t top = max<int64_t>(0, woi->position.y);
            int64_t right = extend_to_edge ? bounds.x :
                    min<int64_t>(bounds.x,
                                 (int64_t) woi->position.x + woi->size.x);
            int64_t bottom = extend_to_edge ? bounds.y :
                    min<int64_t>(bounds.y,
                                 (int64_t) woi->position.y + woi->size.y);
            if (right <= left || bottom <= top)
                return false;

            woi->position = Point((int) left, (int) top);
            woi->size = Size((int) (right - left), (int) (bottom - top));
            return true;
        }

        bool Reject(string *error_message, const char *message) {
            if (error_message != NULL)
                *error_message = message;
            return false;
        }

        struct ResolvedWindow {
            WOI woi;
            const CodingParameters *coding_parameters = NULL;
            bool empty = true;
        };

        bool ResolveWindows(const ImageIndex &image_index,
                            const ResponseRequest &request,
                            const vector<int> &codestreams,
                            vector<ResolvedWindow> *resolved,
                            string *error_message) {
            resolved->clear();
            if (!request.HasWOI())
                return true;

            Size requested_size = request.has.rsiz
                    ? request.woi_size : request.resolution_size;
            if (request.resolution_size.x <= 0 ||
                request.resolution_size.y <= 0 ||
                requested_size.x < 0 || requested_size.y < 0)
                return Reject(error_message, "Invalid JPIP window dimensions");

            resolved->reserve(codestreams.size());
            for (int codestream : codestreams) {
                ResolvedWindow window;
                window.woi.size = requested_size;
                window.woi.position = request.has.roff
                        ? request.woi_position : Point();
                if (!CropWindow(&window.woi, request.resolution_size,
                                !request.has.rsiz)) {
                    resolved->push_back(window);
                    continue;
                }
                window.coding_parameters =
                        image_index.GetCodingParameters(codestream);
                window.woi.layers = static_cast<int>(min<uint64_t>(
                        request.layers, window.coding_parameters->num_layers));
                Size resolution_size;
                if (!MapWindow(request, *window.coding_parameters,
                               &window.woi, &resolution_size))
                    return Reject(error_message,
                                  "Invalid JPIP window dimensions");
                window.empty = !CropWindow(&window.woi, resolution_size);
                resolved->push_back(window);
            }
            return true;
        }

        bool ResolveModel(const ImageIndex &image_index, const ResponseRequest &request,
                          vector<ResponseRequest::ModelUpdate> *resolved) {
            *resolved = request.model;
            for (ResponseRequest::ModelUpdate &update : *resolved) {
                if (update.id < 0 || update.amount < 0)
                    return false;
                if (update.bin_class == DataBinClass::META_DATA) {
                    if (static_cast<size_t>(update.id) > image_index.GetMetadata().bins.size())
                        return false;
                    continue;
                }

                if (update.bin_class != DataBinClass::MAIN_HEADER &&
                    update.bin_class != DataBinClass::TILE_HEADER &&
                    update.bin_class != DataBinClass::PRECINCT)
                    return false;
                if (!update.has_codestream_qualifier) {
                    if (!request.GetUnqualifiedModelCodestream(
                                image_index.GetNumCodestreams(),
                                &update.first_codestream))
                        return false;
                    update.last_codestream = update.first_codestream;
                } else if (update.last_codestream == INT_MAX)
                    update.last_codestream =
                            static_cast<int>(image_index.GetNumCodestreams()) - 1;
                if (update.first_codestream < 0 ||
                    update.last_codestream < update.first_codestream ||
                    update.last_codestream >=
                            static_cast<int>(image_index.GetNumCodestreams()))
                    return false;
                if ((update.bin_class == DataBinClass::MAIN_HEADER ||
                     update.bin_class == DataBinClass::TILE_HEADER) && update.id != 0)
                    return false;
                for (int i = update.first_codestream;
                     i <= update.last_codestream; ++i) {
                    if (update.bin_class == DataBinClass::PRECINCT &&
                        update.id >= image_index.GetCodingParameters(i)->GetNumPrecinctDataBins())
                        return false;
                }
            }
            return true;
        }

        void ApplyModel(CacheModel &cache_model,
                        const vector<ResponseRequest::ModelUpdate> &model) {
            for (const ResponseRequest::ModelUpdate &update : model) {
                if (update.bin_class == DataBinClass::META_DATA) {
                    cache_model.AugmentDataBin(update.bin_class, 0, update.id,
                                               update.amount);
                    continue;
                }
                for (int i = update.first_codestream;
                     i <= update.last_codestream; ++i)
                    cache_model.AugmentDataBin(update.bin_class, i, update.id,
                                               update.amount);
            }
        }

    }

    bool DataBinServer::SetRequest(ImageIndex &image_index, const ResponseRequest &req,
                                   string *error_message) {
        if (error_message != NULL)
            error_message->clear();
        if (req.has.len && req.length_response < 0)
            return Reject(error_message, "Invalid JPIP response length");
        if (target != NULL && target != &image_index)
            return Reject(error_message, "A response session belongs to one target");

        vector<ResponseRequest::ModelUpdate> model;
        if (req.has.model && !ResolveModel(image_index, req, &model))
            return Reject(error_message,
                          "JPIP cache model does not match the selected image");

        vector<int> codestreams;
        if (req.HasSelections()) {
            if (!req.SelectCodestreams(image_index.GetNumCodestreams(),
                                       &codestreams))
                return Reject(error_message,
                              "Invalid or oversized JPIP codestream selection");
        } else {
            codestreams.push_back(0);
        }
        vector<ResolvedWindow> windows;
        if (!ResolveWindows(image_index, req, codestreams, &windows,
                            error_message))
            return false;

        target = &image_index;
        error.clear();
        data_writer.StartResponse();
        header_idx = 0;

        bool changed = streams.size() != codestreams.size();
        for (size_t i = 0; i < codestreams.size(); ++i) {
            if (!changed && streams[i].id != codestreams[i])
                changed = true;
        }
        if (changed) {
            streams.clear();
            streams.reserve(codestreams.size());
            for (int codestream : codestreams)
                streams.emplace_back(codestream);
        }

        bool had_woi = has_woi;
        has_woi = req.HasWOI();
        bool traversal_changed = changed || (has_woi && !had_woi);
        if (has_woi) {
            for (size_t i = 0; i < streams.size(); ++i) {
                Stream &stream = streams[i];
                const ResolvedWindow &window = windows[i];
                if (window.empty) {
                    traversal_changed |= !stream.empty;
                    stream.empty = true;
                    stream.woi = WOI();
                    stream.bin_offsets.clear();
                    stream.bin_idx = 0;
                    continue;
                }
                if (stream.empty || window.woi != stream.woi) {
                    traversal_changed = true;
                    stream.empty = false;
                    stream.woi = window.woi;
                    stream.composer.Reset(window.coding_parameters, stream.woi);
                    stream.bin_offsets.clear();
                    stream.bin_idx = 0;
                }
            }
        }

        if (!has_woi) {
            active_streams.clear();
        } else if (traversal_changed) {
            active_streams.clear();
            for (size_t i = 0; i < streams.size(); ++i)
                if (!streams[i].empty && streams[i].composer.HasPacket())
                    active_streams.push_back(i);
        }

        if (req.has.model)
            ApplyModel(cache_model, model);

        pending = req.has.len ? req.length_response : INT_MAX;
        limit_reason = req.has.len ? EOR::BYTE_LIMIT_REACHED : EOR::RESPONSE_LIMIT_REACHED;
        if (pending < DataBinWriter::EOR_LENGTH)
            pending = DataBinWriter::EOR_LENGTH;

        return true;
    }

    const Source *DataBinServer::OpenSource(SourceProvider &sources,
                                           const std::string &path) {
        const Source *file = sources.GetSource(path);
        if (file == NULL)
            error = "Cannot open source '" + path + "'";
        return file;
    }

    DataBinServer::SegmentResult DataBinServer::WriteMetadata(
            SourceProvider &sources, ImageIndex *image_index) {
        if (cache_model.IsFullMetadata())
            return SegmentResult::COMPLETE;

        const Source *file = OpenSource(sources, image_index->GetPathName());
        if (file == NULL)
            return SegmentResult::FAILED;
        const Metadata &metadata = image_index->GetMetadata();

        while (meta_idx < metadata.bin0.size()) {
            const Metadata::Part &part = metadata.bin0[meta_idx];
            SegmentResult result = WriteSegment<DataBinClass::META_DATA>(
                    file, 0, 0, part.data, meta_offset, false);
            if (result != SegmentResult::COMPLETE)
                return result;

            int placeholder_offset = meta_offset + part.data.length;
            result = WritePlaceHolder(file, part.placeholder,
                                      placeholder_offset);
            if (result != SegmentResult::COMPLETE)
                return result;
            meta_offset = placeholder_offset +
                    static_cast<int>(part.placeholder.length());
            meta_idx++;
        }

        SegmentResult result = WriteSegment<DataBinClass::META_DATA>(
                file, 0, 0, metadata.tail, meta_offset);
        if (result != SegmentResult::COMPLETE)
            return result;

        while (meta_bin_idx < metadata.bins.size()) {
            result = WriteSegment<DataBinClass::META_DATA>(
                    file, 0, meta_bin_idx + 1, metadata.bins[meta_bin_idx]);
            if (result != SegmentResult::COMPLETE)
                return result;
            meta_bin_idx++;
        }

        cache_model.SetFullMetadata();
        return SegmentResult::COMPLETE;
    }

    DataBinServer::SegmentResult DataBinServer::WriteHeaders(
            SourceProvider &sources, ImageIndex *image_index) {
        for (; header_idx < streams.size(); ++header_idx) {
            Stream &stream = streams[header_idx];
            if (has_woi && stream.empty)
                continue;
            if (cache_model.GetDataBin(
                        DataBinClass::MAIN_HEADER, stream.id, 0) == INT_MAX &&
                cache_model.GetDataBin(
                        DataBinClass::TILE_HEADER, stream.id, 0) == INT_MAX)
                continue;
            if (stream.file == NULL)
                stream.file = OpenSource(sources, image_index->GetPathName(stream.id));
            if (stream.file == NULL)
                return SegmentResult::FAILED;
            SegmentResult result = WriteSegment<DataBinClass::MAIN_HEADER>(
                    stream.file, stream.id, 0,
                    image_index->GetMainHeader(stream.id));
            if (result != SegmentResult::COMPLETE)
                return result;

            result = WriteSegment<DataBinClass::TILE_HEADER>(
                    stream.file, stream.id, 0, FileSegment::Null);
            if (result != SegmentResult::COMPLETE)
                return result;
        }
        return SegmentResult::COMPLETE;
    }

    DataBinServer::SegmentResult DataBinServer::WritePackets(
            SourceProvider &sources, ImageIndex *image_index) {
        while (!active_streams.empty()) {
            size_t stream_idx = active_streams.front();
            Stream &stream = streams[stream_idx];
            if (stream.file == NULL)
                stream.file = OpenSource(sources, image_index->GetPathName(stream.id));
            if (stream.file == NULL)
                return SegmentResult::FAILED;
            const Packet &packet = stream.composer.GetCurrentPacket();
            const CodingParameters *coding_parameters =
                    image_index->GetCodingParameters(stream.id);
            FileSegment segment;
            if (!image_index->GetPacket(stream.file, stream.id, packet,
                                        &segment)) {
                error = image_index->GetError();
                return SegmentResult::FAILED;
            }
            int bin_id = coding_parameters->GetPrecinctDataBinId(packet);
            bool last_packet =
                    packet.layer >= coding_parameters->num_layers - 1;

            int bin_offset;
            if (packet.layer == 0) {
                if (stream.bin_idx != stream.bin_offsets.size()) {
                    error = "Invalid precinct-bin traversal state";
                    return SegmentResult::FAILED;
                }
                bin_offset = 0;
            } else {
                if (stream.bin_idx >= stream.bin_offsets.size()) {
                    error = "Invalid precinct-bin traversal state";
                    return SegmentResult::FAILED;
                }
                bin_offset = stream.bin_offsets[stream.bin_idx];
            }

            if (segment.offset > stream.file->GetSize() ||
                segment.length > stream.file->GetSize() - segment.offset) {
                error = "Invalid packet source range";
                return SegmentResult::FAILED;
            }
            SegmentResult result = WriteSegment<DataBinClass::PRECINCT>(
                    stream.file, stream.id, bin_id, segment,
                    bin_offset, last_packet);
            if (result == SegmentResult::FAILED) {
                if (error.empty()) error = "Could not write packet contribution";
                return result;
            }
            if (result == SegmentResult::FULL)
                return result;

            active_streams.pop_front();

            if (segment.length > static_cast<uint64_t>(INT_MAX - bin_offset)) {
                error = "Precinct data-bin exceeds the supported offset range";
                return SegmentResult::FAILED;
            }
            int next_offset = bin_offset + static_cast<int>(segment.length);
            if (packet.layer == 0)
                stream.bin_offsets.push_back(next_offset);
            else
                stream.bin_offsets[stream.bin_idx] = next_offset;

            int layer = packet.layer;
            stream.composer.GetNextPacket(coding_parameters);
            if (stream.composer.HasPacket() &&
                stream.composer.GetCurrentPacket().layer == layer)
                stream.bin_idx++;
            else
                stream.bin_idx = 0;
            if (stream.composer.HasPacket())
                active_streams.push_back(stream_idx);
        }
        return SegmentResult::COMPLETE;
    }

    bool DataBinServer::GenerateChunk(SourceProvider &sources, char *buf,
                                      int *len, bool *last) {
        int capacity = *len;
        *len = 0;
        *last = false;
        if (target == NULL || capacity < DataBinWriter::EOR_LENGTH) {
            error = target == NULL ? "No request" : "Output buffer cannot hold EOR";
            return false;
        }
        ImageIndex *image_index = target;
        data_writer.SetBuffer(buf, min(pending, capacity));

        if (pending > 0 && has_woi) {
            bool empty = true;
            for (const Stream &stream : streams)
                empty &= stream.empty;
            if (empty) {
                if (!data_writer.WriteEOR(EOR::WINDOW_DONE)) {
                    error = "Output buffer cannot hold window-completion EOR";
                    return false;
                }
                pending = 0;
            }
        }

        if (pending > 0) {
            SegmentResult result = WriteMetadata(sources, image_index);
            if (result == SegmentResult::COMPLETE)
                result = WriteHeaders(sources, image_index);
            if (result == SegmentResult::COMPLETE && has_woi)
                result = WritePackets(sources, image_index);
            if (result == SegmentResult::FAILED) {
                if (error.empty()) error = "Could not read or encode data-bin contribution";
                return false;
            }

            bool chunk_full = result == SegmentResult::FULL;
            if (!chunk_full) {
                if (data_writer.WriteEOR(EOR::WINDOW_DONE))
                    pending = 0;
                else
                    chunk_full = true;
            }

            if (chunk_full) {
                int written = data_writer.Finalize();
                // A request budget may finish with EOR alone. A smaller caller
                // buffer must allow progress while that budget remains.
                if (written == 0 && capacity < pending) {
                    error = "Output buffer too small for the next contribution";
                    return false;
                }
                pending -= written;
                if (pending <= CHUNK_RESERVE + 100) {
                    if (data_writer.WriteEOR(limit_reason))
                        pending = 0;
                    else {
                        error = "Output buffer cannot hold limit EOR";
                        return false;
                    }
                }
            }
        }

        *len = data_writer.Finalize();
        *last = pending <= 0;
        if (*len == 0 && !*last) {
            error = "Output buffer too small for the next contribution";
            return false;
        }
        if (*last) {
            cache_model.Pack();
            for (Stream &stream : streams)
                stream.file = NULL;
        }
        return true;
    }

}
