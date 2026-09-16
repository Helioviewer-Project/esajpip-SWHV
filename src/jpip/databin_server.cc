#include "databin_server.h"

#include <algorithm>
#include <cstdint>

using namespace std;

namespace jpip {

    using data::File;
    using data::FileSegment;
    using jpeg2000::CodingParameters;
    using jpeg2000::FileManager;
    using jpeg2000::ImageIndex;
    using jpeg2000::Metadata;
    using jpeg2000::Packet;

    namespace {

        bool CropWindow(WOI *woi, const jpeg2000::Size &bounds) {
            int64_t left = max<int64_t>(0, woi->position.x);
            int64_t top = max<int64_t>(0, woi->position.y);
            int64_t right = min<int64_t>(bounds.x,
                                         (int64_t) woi->position.x + woi->size.x);
            int64_t bottom = min<int64_t>(bounds.y,
                                          (int64_t) woi->position.y + woi->size.y);
            if (right <= left || bottom <= top)
                return false;

            woi->position = jpeg2000::Point((int) left, (int) top);
            woi->size = jpeg2000::Size((int) (right - left), (int) (bottom - top));
            return true;
        }

        bool ValidateModel(const ImageIndex &image_index,
                           const vector<Request::ModelUpdate> &model) {
            for (const Request::ModelUpdate &update : model) {
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
                if (update.first_codestream < 0 ||
                    update.last_codestream < update.first_codestream ||
                    update.last_codestream >= static_cast<int>(image_index.GetNumCodestreams()))
                    return false;
                if (update.bin_class == DataBinClass::TILE_HEADER && update.id != 0)
                    return false;
                if (update.bin_class == DataBinClass::PRECINCT) {
                    for (int i = update.first_codestream; i <= update.last_codestream; ++i)
                        if (update.id >= image_index.GetCodingParameters(i)->GetNumPrecinctDataBins())
                            return false;
                }
            }
            return true;
        }

        void ApplyModel(CacheModel *cache_model,
                        const vector<Request::ModelUpdate> &model) {
            for (const Request::ModelUpdate &update : model) {
                if (update.bin_class == DataBinClass::META_DATA) {
                    cache_model->AugmentDataBin(update.bin_class, 0, update.id,
                                                update.amount);
                    continue;
                }
                for (int i = update.first_codestream; i <= update.last_codestream; ++i)
                    cache_model->AugmentDataBin(update.bin_class, i, update.id,
                                                update.amount);
            }
        }

    }

    bool DataBinServer::SetRequest(FileManager &file_manager, const Request &req) {
        ImageIndex *image_index = file_manager.GetImage();

        data_writer.StartResponse();

        if (req.has.model && !ValidateModel(*image_index, req.model))
            return false;

        if (req.has.stream || req.has.context) {
            for (int codestream : req.codestreams)
                if (codestream < 0 || static_cast<size_t>(codestream) >= image_index->GetNumCodestreams())
                    return false;

            bool changed = streams.size() != req.codestreams.size();
            for (size_t i = 0; !changed && i < streams.size(); ++i)
                changed = streams[i].id != req.codestreams[i];
            if (changed) {
                streams.clear();
                streams.reserve(req.codestreams.size());
                for (int codestream : req.codestreams)
                    streams.emplace_back(codestream);
                current_idx = 0;
            }
        }

        has_woi = req.HasWOI();
        if (has_woi) {
            if (streams.empty()) {
                streams.emplace_back(0);
                current_idx = 0;
            }
            jpeg2000::Size requested_size =
                    req.has.rsiz ? req.woi_size : req.resolution_size;
            if (req.resolution_size.x <= 0 || req.resolution_size.y <= 0 ||
                requested_size.x < 0 || requested_size.y < 0)
                return false;

            for (Stream &stream : streams) {
                WOI new_woi;
                new_woi.size = requested_size;
                new_woi.position = req.has.roff ? req.woi_position
                                                 : jpeg2000::Point();
                if (!CropWindow(&new_woi, req.resolution_size)) {
                    stream.empty = true;
                    stream.woi = WOI();
                    continue;
                }

                const CodingParameters *coding_parameters =
                        image_index->GetCodingParameters(stream.id);
                jpeg2000::Size resolution_size =
                        req.GetResolution(coding_parameters, &new_woi);
                if (!CropWindow(&new_woi, resolution_size)) {
                    stream.empty = true;
                    stream.woi = WOI();
                } else if (stream.empty || new_woi != stream.woi) {
                    stream.empty = false;
                    stream.woi = new_woi;
                    stream.composer.Reset(coding_parameters, stream.woi);
                }
            }
        }

        if (req.has.model)
            ApplyModel(&cache_model, req.model);

        pending = req.has.len ? req.length_response : INT_MAX;
        if (pending < DataBinWriter::EOR_LENGTH)
            pending = DataBinWriter::EOR_LENGTH;

        return true;
    }

    bool DataBinServer::GenerateChunk(FileManager &file_manager, char *buf, int *len, bool *last) {
        SegmentResult res;
        bool chunk_full = false;
        ImageIndex *image_index = file_manager.GetImage();

        data_writer.SetBuffer(buf, min(pending, *len));

        if (pending > 0 && has_woi) {
            bool empty = true;
            for (const Stream &stream : streams)
                empty &= stream.empty;
            if (empty) {
                if (!data_writer.WriteEOR(EOR::WINDOW_DONE))
                    return false;
                pending = 0;
            }
        }

        if (pending > 0) {
            if (!cache_model.IsFullMetadata()) {
                File *file = file_manager.GetFile(image_index->GetPathName());
                if (file == NULL)
                    return false;
                const Metadata &metadata = image_index->GetMetadata();
                if (!meta_bin0_done) {
                    while (meta_idx < metadata.bin0.size()) {
                        const Metadata::Part &part = metadata.bin0[meta_idx];
                        res = WriteSegment<DataBinClass::META_DATA>(file, 0, 0,
                                                                    part.data, meta_offset, false);
                        if (res == SegmentResult::FAILED)
                            return false;
                        if (res == SegmentResult::FULL) {
                            chunk_full = true;
                            break;
                        }

                        int placeholder_offset = meta_offset + part.data.length;
                        res = WritePlaceHolder(file, 0, 0, part.placeholder,
                                               placeholder_offset);
                        if (res == SegmentResult::FAILED)
                            return false;
                        if (res == SegmentResult::FULL) {
                            chunk_full = true;
                            break;
                        }
                        meta_offset = placeholder_offset + part.placeholder.length();
                        meta_idx++;
                    }

                    if (!chunk_full && meta_idx == metadata.bin0.size()) {
                        res = WriteSegment<DataBinClass::META_DATA>(
                                file, 0, 0, metadata.tail, meta_offset);
                        if (res == SegmentResult::FAILED)
                            return false;
                        if (res == SegmentResult::FULL)
                            chunk_full = true;
                        else
                            meta_bin0_done = true;
                    }
                }

                while (meta_bin0_done && !chunk_full &&
                       meta_bin_idx < metadata.bins.size()) {
                    res = WriteSegment<DataBinClass::META_DATA>(file, 0, meta_bin_idx + 1,
                                                                metadata.bins[meta_bin_idx]);
                    if (res == SegmentResult::FAILED)
                        return false;
                    if (res == SegmentResult::FULL) {
                        chunk_full = true;
                        break;
                    }
                    meta_bin_idx++;
                }

                if (meta_bin0_done && meta_bin_idx == metadata.bins.size())
                    cache_model.SetFullMetadata();
            }

            if (!chunk_full) {
                for (Stream &stream : streams) {
                    if (has_woi && stream.empty)
                        continue;
                    if (stream.file == NULL) {
                        stream.file = file_manager.GetFile(
                                image_index->GetPathName(stream.id));
                        if (stream.file == NULL)
                            return false;
                    }
                }
                for (Stream &stream : streams) {
                    if (has_woi && stream.empty)
                        continue;
                    res = WriteSegment<DataBinClass::MAIN_HEADER>(
                            stream.file, stream.id, 0,
                            image_index->GetMainHeader(stream.id));
                    if (res == SegmentResult::FAILED)
                        return false;
                    if (res == SegmentResult::FULL) {
                        chunk_full = true;
                        break;
                    }

                    res = WriteSegment<DataBinClass::TILE_HEADER>(
                            stream.file, stream.id, 0, FileSegment::Null);
                    if (res == SegmentResult::FAILED)
                        return false;
                    if (res == SegmentResult::FULL) {
                        chunk_full = true;
                        break;
                    }
                }

                if (!chunk_full && has_woi) {
                    FileSegment segment;
                    int bin_id, bin_offset;
                    bool last_packet;

                    while (!chunk_full) {
                        size_t checked = 0;
                        while (checked < streams.size() &&
                               (streams[current_idx].empty ||
                                !streams[current_idx].composer.HasPacket())) {
                            current_idx = (current_idx + 1) % streams.size();
                            checked++;
                        }
                        if (checked == streams.size())
                            break;

                        Stream &stream = streams[current_idx];
                        const Packet &packet = stream.composer.GetCurrentPacket();
                        const CodingParameters *coding_parameters =
                                image_index->GetCodingParameters(stream.id);

                        if (!image_index->GetPacket(stream.file, stream.id, packet,
                                                    &segment, &bin_offset))
                            return false;
                        bin_id = coding_parameters->GetPrecinctDataBinId(packet);
                        last_packet = packet.layer >= coding_parameters->num_layers - 1;

                        if (segment.offset > stream.file->GetSize() ||
                            segment.length > stream.file->GetSize() - segment.offset) {
                            ERROR("Invalid packet segment: codestream=" << stream.id
                                  << ", packet=" << packet << ", segment=" << segment
                                  << ", file_size=" << stream.file->GetSize());
                            return false;
                        }
                        res = WriteSegment<DataBinClass::PRECINCT>(
                                stream.file, stream.id, bin_id, segment,
                                bin_offset, last_packet);

                        if (res == SegmentResult::FAILED) {
                            ERROR("Could not write packet segment: codestream=" << stream.id
                                  << ", bin=" << bin_id << ", packet=" << packet << ", segment=" << segment);
                            return false;
                        }
                        if (res == SegmentResult::FULL) {
                            chunk_full = true;
                        } else {
                            stream.composer.GetNextPacket(coding_parameters);
                            current_idx = (current_idx + 1) % streams.size();
                        }
                    }
                }
            }

            if (!chunk_full) {
                if (data_writer.WriteEOR(EOR::WINDOW_DONE))
                    pending = 0;
                else
                    chunk_full = true;
            }

            if (chunk_full) {
                pending -= data_writer.GetCount();
                if (pending <= CHUNK_RESERVE + 100) {
                    if (data_writer.WriteEOR(EOR::BYTE_LIMIT_REACHED))
                        pending = 0;
                    else
                        return false;
                }
            }
        }

        *len = data_writer.GetCount();
        *last = (pending <= 0);

        if (*last) {
            cache_model.Pack();
            for (Stream &stream : streams)
                stream.file = NULL;
        }

        return true;
    }

}
