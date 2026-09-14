#include "databin_server.h"

namespace jpip {

    void DataBinServer::SetRequest(FileManager &file_manager, const Request &req) {
        bool reset_woi = false;
        ImageIndex *image_index = file_manager.GetImage();

        data_writer.ClearPreviousIds();

        if (req.mask.items.stream || req.mask.items.context) {
            if (codestreams != req.codestreams) {
                codestreams = req.codestreams;
                current_idx = 0;
                reset_woi = true;
            }
        }

        if ((has_woi = req.mask.HasWOI())) {
            int codestream = codestreams.empty() ? 0 : codestreams[current_idx];
            const CodingParameters *coding_parameters = image_index->GetCodingParameters(codestream);
            WOI new_woi;
            new_woi.size = req.woi_size;
            new_woi.position = req.woi_position;
            req.GetResolution(coding_parameters, &new_woi);

            if (new_woi != woi) {
                reset_woi = true;
                woi = new_woi;
            }
        }

        if (req.mask.items.model)
            cache_model += req.cache_model;

        if (req.mask.items.len)
            pending = req.length_response;

        if (reset_woi) {
            int codestream = codestreams.empty() ? 0 : codestreams[current_idx];
            const CodingParameters *coding_parameters = image_index->GetCodingParameters(codestream);
            woi_composer.Reset(coding_parameters, woi);
        }
    }

    bool DataBinServer::GenerateChunk(FileManager &file_manager, char *buf, int *len, bool *last) {
        int res;
        ImageIndex *image_index = file_manager.GetImage();

        data_writer.SetBuffer(buf, min(pending, *len));

        if (pending > 0) {
            eof = false;

            if (!cache_model.IsFullMetadata()) {
                File *file = file_manager.GetFile(image_index->GetPathName());
                if (!meta_bin0_done && image_index->GetNumMetadatas() <= 0) {
                    if (WriteSegment<DataBinClass::META_DATA>(file, 0, 0, FileSegment::Null) > 0)
                        meta_bin0_done = true;
                } else if (!meta_bin0_done) {
                    size_t num_metadatas = image_index->GetNumMetadatas();
                    while (meta_idx < num_metadatas) {
                        const FileSegment &metadata = image_index->GetMetadata(meta_idx);
                        bool last_metadata = meta_idx == num_metadatas - 1;
                        res = WriteSegment<DataBinClass::META_DATA>(file, 0, 0, metadata, meta_offset, last_metadata);

                        if (last_metadata) {
                            if (res > 0)
                                meta_bin0_done = true;
                            break;
                        }

                        int placeholder_offset = meta_offset + metadata.length;
                        const PlaceHolder &placeholder = image_index->GetPlaceHolder(meta_idx);
                        if (WritePlaceHolder(file, 0, 0, placeholder, placeholder_offset) <= 0)
                            break;
                        meta_offset = placeholder_offset + placeholder.length();
                        meta_idx++;
                    }
                }

                while (meta_bin0_done && !eof && meta_bin_idx < image_index->GetNumMetadataBins()) {
                    res = WriteSegment<DataBinClass::META_DATA>(file, 0, meta_bin_idx + 1,
                                                                image_index->GetMetadataBin(meta_bin_idx));
                    if (res <= 0)
                        break;
                    meta_bin_idx++;
                }

                if (meta_bin0_done && meta_bin_idx == image_index->GetNumMetadataBins())
                    cache_model.SetFullMetadata();
            }

            if (!eof) {
                vector<File *> files(codestreams.size());
                for (size_t i = 0; i < codestreams.size(); ++i) {
                    files[i] = file_manager.GetFile(image_index->GetPathName(codestreams[i]));
                    WriteSegment<DataBinClass::MAIN_HEADER>(files[i], codestreams[i], 0, image_index->GetMainHeader(codestreams[i]));
                    WriteSegment<DataBinClass::TILE_HEADER>(files[i], codestreams[i], 0, FileSegment::Null);
                }

                if (has_woi) {
                    FileSegment segment;
                    int bin_id, bin_offset;
                    bool last_packet;
                    const CodingParameters *composer_parameters = image_index->GetCodingParameters(codestreams.front());

                    while (data_writer && !eof) {
                        const Packet &packet = woi_composer.GetCurrentPacket();
                        const CodingParameters *coding_parameters = image_index->GetCodingParameters(codestreams[current_idx]);

                        File *file = files[current_idx];
                        if (!image_index->GetPacket(file, codestreams[current_idx], packet, &segment, &bin_offset))
                            return false;
                        bin_id = coding_parameters->GetPrecinctDataBinId(packet);
                        last_packet = packet.layer >= coding_parameters->num_layers - 1;

                        if (segment.offset + segment.length > file->GetSize()) {
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

        if (*last) cache_model.Pack();

        return true;
    }

}
