#include "trace.h"
#include "image_index.h"
#include "index_manager.h"

namespace jpeg2000 {

    void ImageIndex::Init(const string &path_name, const ImageInfo &image_info) {
        this->path_name = path_name;

        meta_data = image_info.meta_data;

        if (image_info.paths.empty()) {
            codestreams = image_info.codestreams;
            max_resolution.resize(codestreams.size(), -1);

            for (size_t i = 0; i < codestreams.size(); ++i) {
                last_plt.push_back(0);
                last_packet.push_back(0);
                last_offset_PLT.push_back(0);
                last_offset_packet.push_back(0);
                packet_indexes.emplace_back();
            }
        }
    }

    void ImageIndex::Init(const string &path_name, const ImageInfo &image_info, int index) {
        this->path_name = path_name;

        meta_data = image_info.meta_data_hyperlinks[index];
        codestreams.push_back(image_info.codestreams[index]);
        max_resolution.push_back(-1);

        last_plt.push_back(0);
        last_packet.push_back(0);
        last_offset_PLT.push_back(0);
        last_offset_packet.push_back(0);
        packet_indexes.emplace_back();
    }

    bool ImageIndex::BuildIndex(IndexManager &index_manager, int ind_codestream, int r) {
        File::Ptr file = index_manager.OpenFile(path_name);
        // Check if PacketIndex has been created
        if (packet_indexes[ind_codestream].Size() == 0)
            packet_indexes[ind_codestream] = PacketIndex(file->GetSize());

        // Check the upper top of the index (to build)
        int max_index;
        const CodingParameters *coding_parameters = index_manager.GetCodingParameters();
        if (r < coding_parameters->num_levels && coding_parameters->IsResolutionProgression()) {
            // The max_index is the last packet index of the resolution r
            Packet packet(0, r + 1, 0, Size(0, 0));
            max_index = coding_parameters->GetProgressionIndex(packet) - 1;
        } else {
            // The max_index is the last packet of the image file
            Size precinct_point =
                    coding_parameters->GetPrecincts(coding_parameters->num_levels, coding_parameters->size) - 1;
            Packet packet(coding_parameters->num_layers - 1, coding_parameters->num_levels,
                          coding_parameters->num_components - 1, precinct_point);
            max_index = coding_parameters->GetProgressionIndex(packet);
        }

        uint64_t length_packet = 0;
        bool res = true;
        while (packet_indexes[ind_codestream].Size() <= max_index) {
            res = res && GetPLTLength(file, ind_codestream, &length_packet);
            GetOffsetPacket(file, ind_codestream, length_packet);
        }

        return res;
    }

    bool ImageIndex::GetPLTLength(File::Ptr &file, int ind_codestream, uint64_t *length_packet) {
        bool res = true;
        vector<FileSegment> &plt = codestreams[ind_codestream].PLT_markers;
        // Get packet plt offset
        if (last_offset_PLT[ind_codestream] == 0)
            res = res && file->Seek(plt[last_plt[ind_codestream]].offset);
        else res = res && file->Seek(last_offset_PLT[ind_codestream]);

        // Get packet length
        uint8_t buf_packet = 0;
        uint8_t length_packet_partial;
        uint8_t partial = 1;

        *length_packet = 0;
        while (partial) {
            res = res && file->Read(&buf_packet);
            partial = buf_packet & (uint8_t) 128; // To get if the packet is final or not
            length_packet_partial = buf_packet & (uint8_t) 127; // To get the packet length
            *length_packet = (*length_packet << 7) | length_packet_partial;
        }

        last_offset_PLT[ind_codestream] = file->GetOffset();
        if (last_offset_PLT[ind_codestream] == plt[last_plt[ind_codestream]].offset + plt[last_plt[ind_codestream]].length) {
            last_plt[ind_codestream]++;
            last_offset_PLT[ind_codestream] = 0;
        }
        return res;
    }

    void ImageIndex::GetOffsetPacket(File::Ptr &file, int ind_codestream, uint64_t length_packet) {
        uint64_t offset;
        vector<FileSegment> &packets = codestreams[ind_codestream].packets;

        if (last_offset_packet[ind_codestream] == 0) offset = packets[last_packet[ind_codestream]].offset;
        else offset = last_offset_packet[ind_codestream];

        packet_indexes[ind_codestream].Add(FileSegment(offset, length_packet));
        last_offset_packet[ind_codestream] = offset + length_packet;

        if (last_offset_packet[ind_codestream] ==
            (packets[last_packet[ind_codestream]].offset + packets[last_packet[ind_codestream]].length)) {
            last_packet[ind_codestream]++;
            last_offset_packet[ind_codestream] = 0;
        }
    }

    FileSegment ImageIndex::GetPacket(IndexManager &index_manager, int num_codestream, const Packet &packet, int *offset) {
        bool linked = !hyper_links.empty();
        if (linked) {
            if (packet.resolution > hyper_links[num_codestream]->max_resolution.back()) {
                if (!hyper_links[num_codestream]->BuildIndex(index_manager, 0, packet.resolution))
                    ERROR("The packet index could not be created");
                hyper_links[num_codestream]->max_resolution.back() = packet.resolution;
            }
        } else {
            if (packet.resolution > max_resolution[num_codestream]) {
                if (!BuildIndex(index_manager, num_codestream, packet.resolution))
                    ERROR("The packet index could not be created");
                max_resolution[num_codestream] = packet.resolution;
            }
        }

        const CodingParameters *coding_parameters = index_manager.GetCodingParameters();
        int idx = coding_parameters->GetProgressionIndex(packet);
        PacketIndex &packet_index = linked ? hyper_links[num_codestream]->packet_indexes[0] : packet_indexes[num_codestream];
        FileSegment segment = packet_index[idx];

        if (offset != NULL) {
            *offset = 0;

            if (coding_parameters->progression == CodingParameters::RPCL_PROGRESSION) {
                for (int l = packet.layer; l > 0; --l)
                    *offset += packet_index[--idx].length;
            } else {
                Packet p_aux = packet;
                for (int l = 0; l < packet.layer; ++l) {
                    p_aux.layer = l;
                    idx = coding_parameters->GetProgressionIndex(p_aux);
                    *offset += packet_index[idx].length;
                }
            }
        }
        return segment;
    }

}
