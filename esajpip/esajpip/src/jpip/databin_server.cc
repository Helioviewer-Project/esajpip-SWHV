#include "databin_server.h"

namespace jpip {

    void DataBinServer::Reset() {
        metareq = false;
        has_woi = false;
    }

    bool DataBinServer::SetRequest(const IndexManager &index_manager, const ImageIndex::Ptr image_index, const Request &req) {
        bool res = true;
        bool reset_woi = false;

        data_writer.ClearPreviousIds();

        if ((has_woi = req.mask.HasWOI())) {
            WOI new_woi;
            new_woi.size = req.woi_size;
            new_woi.position = req.woi_position;
            req.GetResolution(image_index->GetCodingParameters(), &new_woi);

            if (new_woi != woi) {
                reset_woi = true;
                woi = new_woi;
            }
        }

        if (req.mask.items.model)
            cache_model += req.cache_model;

        if (req.mask.items.metareq)
            metareq = true;

        if (req.mask.items.stream || req.mask.items.context) {
            if (codestreams != req.codestreams) {
                codestreams = req.codestreams;
                current_idx = 0;
                reset_woi = true;
            }
        }

        if (req.mask.items.len)
            pending = req.length_response;

        if (reset_woi) {
            end_woi_ = false;
            woi_composer.Reset(image_index->GetCodingParameters(), woi);
        }

        return res;
    }

    bool DataBinServer::GenerateChunk(IndexManager &index_manager, const ImageIndex::Ptr image_index, char *buf, int *len, bool *last) {
        int res;

        data_writer.SetBuffer(buf, min(pending, *len));

        if (pending > 0) {
            eof = false;

            if (!cache_model.IsFullMetadata()) {
                File::Ptr file = index_manager.OpenFile(image_index->GetPathName());
                if (image_index->GetNumMetadatas() <= 0)
                    WriteSegment<DataBinClass::META_DATA>(file, 0, 0, FileSegment::Null);
                else {
                    int bin_offset = 0;
                    bool last_metadata;

                    for (size_t i = 0; i < image_index->GetNumMetadatas(); ++i) {
                        last_metadata = (i == image_index->GetNumMetadatas() - 1);
                        res = WriteSegment<DataBinClass::META_DATA>(file, 0, 0, image_index->GetMetadata(i), bin_offset, last_metadata);
                        bin_offset += image_index->GetMetadata(i).length;

                        if (last_metadata) {
                            if (res > 0) cache_model.SetFullMetadata();
                        } else {
                            if (WritePlaceHolder(file, 0, 0, image_index->GetPlaceHolder(i), bin_offset) <= 0) break;
                            bin_offset += image_index->GetPlaceHolder(i).length();
                        }
                    }
                }
            }

            if (!eof) {
                for (size_t i = 0; i < codestreams.size(); ++i) {
                    File::Ptr file = index_manager.OpenFile(image_index->GetPathName(codestreams[i]));
                    WriteSegment<DataBinClass::MAIN_HEADER>(file, codestreams[i], 0, image_index->GetMainHeader(codestreams[i]));
                    WriteSegment<DataBinClass::TILE_HEADER>(file, codestreams[i], 0, FileSegment::Null);
                }

                if (has_woi) {
                    Packet packet;
                    FileSegment segment;
                    int bin_id, bin_offset;
                    bool last_packet;
                    const CodingParameters *coding_parameters = image_index->GetCodingParameters();

                    while (data_writer && !eof) {
                        packet = woi_composer.GetCurrentPacket();

                        segment = image_index->GetPacket(index_manager, codestreams[current_idx], packet, &bin_offset);
                        bin_id = coding_parameters->GetPrecinctDataBinId(packet);
                        last_packet = packet.layer >= coding_parameters->num_layers - 1;

                        File::Ptr file = index_manager.OpenFile(image_index->GetPathName(codestreams[current_idx]));
                        res = WriteSegment<DataBinClass::PRECINCT>(file, codestreams[current_idx], bin_id, segment, bin_offset, last_packet);

                        if (res < 0) return false;
                        else if (res > 0) {
                            if (current_idx != (int) codestreams.size() - 1) current_idx++;
                            else {
                                if (!woi_composer.GetNextPacket(coding_parameters)) break;
                                else current_idx = 0;
                            }
                        }
                    }
                }
            }

            if (!eof) {
                data_writer.WriteEOR(EOR::WINDOW_DONE);
                end_woi_ = true;
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
