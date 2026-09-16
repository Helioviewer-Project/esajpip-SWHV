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
            if (woi->size.x <= 0 || woi->size.y <= 0 ||
                bounds.x <= 0 || bounds.y <= 0)
                return false;

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

    }

    bool DataBinServer::SetRequest(FileManager &file_manager, const Request &req) {
        bool reset_woi = false;
        ImageIndex *image_index = file_manager.GetImage();

        data_writer.StartResponse();

        if (req.has.stream || req.has.context) {
            for (int codestream : req.codestreams)
                if (codestream < 0 || static_cast<size_t>(codestream) >= image_index->GetNumCodestreams())
                    return false;
            if (codestreams != req.codestreams) {
                codestreams = req.codestreams;
                current_idx = 0;
                reset_woi = true;
            }
        }

        if ((has_woi = req.HasWOI())) {
            if (codestreams.empty()) {
                codestreams.push_back(0);
                current_idx = 0;
                reset_woi = true;
            }
            int codestream = codestreams[current_idx];
            const CodingParameters *coding_parameters = image_index->GetCodingParameters(codestream);
            WOI new_woi;
            new_woi.size = req.woi_size;
            new_woi.position = req.woi_position;
            if (!CropWindow(&new_woi, req.resolution_size))
                return false;
            jpeg2000::Size resolution_size = req.GetResolution(coding_parameters, &new_woi);
            if (!CropWindow(&new_woi, resolution_size))
                return false;

            if (new_woi != woi) {
                reset_woi = true;
                woi = new_woi;
            }
        }

        if (req.has.model)
            cache_model += req.cache_model;

        pending = req.has.len ? req.length_response : INT_MAX;

        if (reset_woi) {
            int codestream = codestreams[current_idx];
            const CodingParameters *coding_parameters = image_index->GetCodingParameters(codestream);
            woi_composer.Reset(coding_parameters, woi);
        }
        return true;
    }

    bool DataBinServer::GenerateChunk(FileManager &file_manager, char *buf, int *len, bool *last) {
        int res;
        ImageIndex *image_index = file_manager.GetImage();

        data_writer.SetBuffer(buf, min(pending, *len));

        if (pending > 0) {
            eof = false;

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
                        if (res <= 0)
                            break;

                        int placeholder_offset = meta_offset + part.data.length;
                        if (WritePlaceHolder(file, 0, 0, part.placeholder,
                                             placeholder_offset) <= 0)
                            break;
                        meta_offset = placeholder_offset + part.placeholder.length();
                        meta_idx++;
                    }

                    if (!eof && meta_idx == metadata.bin0.size() &&
                        WriteSegment<DataBinClass::META_DATA>(file, 0, 0,
                                                              metadata.tail, meta_offset) > 0)
                        meta_bin0_done = true;
                }

                while (meta_bin0_done && !eof && meta_bin_idx < metadata.bins.size()) {
                    res = WriteSegment<DataBinClass::META_DATA>(file, 0, meta_bin_idx + 1,
                                                                metadata.bins[meta_bin_idx]);
                    if (res <= 0)
                        break;
                    meta_bin_idx++;
                }

                if (meta_bin0_done && meta_bin_idx == metadata.bins.size())
                    cache_model.SetFullMetadata();
            }

            if (!eof) {
                if (files.empty()) {
                    files.resize(codestreams.size());
                    for (size_t i = 0; i < codestreams.size(); ++i) {
                        files[i] = file_manager.GetFile(image_index->GetPathName(codestreams[i]));
                        if (files[i] == NULL)
                            return false;
                    }
                }
                for (size_t i = 0; i < codestreams.size(); ++i) {
                    WriteSegment<DataBinClass::MAIN_HEADER>(files[i], codestreams[i], 0,
                                                            image_index->GetMainHeader(codestreams[i]));
                    WriteSegment<DataBinClass::TILE_HEADER>(files[i], codestreams[i], 0,
                                                            FileSegment::Null);
                }

                if (has_woi) {
                    FileSegment segment;
                    int bin_id, bin_offset;
                    bool last_packet;
                    const CodingParameters *composer_parameters = image_index->GetCodingParameters(codestreams.front());

                    while (data_writer.IsValid() && !eof) {
                        const Packet &packet = woi_composer.GetCurrentPacket();
                        const CodingParameters *coding_parameters = image_index->GetCodingParameters(codestreams[current_idx]);

                        File *file = files[current_idx];
                        if (!image_index->GetPacket(file, codestreams[current_idx], packet, &segment, &bin_offset))
                            return false;
                        bin_id = coding_parameters->GetPrecinctDataBinId(packet);
                        last_packet = packet.layer >= coding_parameters->num_layers - 1;

                        if (segment.offset > file->GetSize() ||
                            segment.length > file->GetSize() - segment.offset) {
                            ERROR("Invalid packet segment: codestream=" << codestreams[current_idx]
                                  << ", packet=" << packet << ", segment=" << segment << ", file_size=" << file->GetSize());
                            return false;
                        }
                        res = WriteSegment<DataBinClass::PRECINCT>(file, codestreams[current_idx], bin_id, segment, bin_offset, last_packet);

                        if (res < 0) {
                            ERROR("Could not write packet segment: codestream=" << codestreams[current_idx]
                                  << ", bin=" << bin_id << ", packet=" << packet << ", segment=" << segment);
                            return false;
                        }
                        else if (res > 0) {
                            if (current_idx != codestreams.size() - 1) current_idx++;
                            else {
                                if (!woi_composer.GetNextPacket(composer_parameters)) break;
                                else current_idx = 0;
                            }
                        }
                    }
                }
            }

            if (!eof) {
                data_writer.WriteEOR(EOR::WINDOW_DONE);
                pending = 0;
            } else {
                pending -= data_writer.GetCount();
                if (pending <= MINIMUM_SPACE + 100) {
                    data_writer.WriteEOR(EOR::BYTE_LIMIT_REACHED);
                    pending = 0;
                }
            }
        }

        *len = data_writer.GetCount();
        *last = (pending <= 0);

        if (*last) {
            cache_model.Pack();
            vector<File *>().swap(files);
        }

        return true;
    }

}
