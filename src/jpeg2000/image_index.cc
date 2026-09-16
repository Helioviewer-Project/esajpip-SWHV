#include "trace.h"
#include "image_index.h"

#include <utility>

using namespace std;

namespace jpeg2000 {

    using data::File;
    using data::FileSegment;

    ImageIndex::Codestream::Codestream(const string &_path, CodingParameters &&_params,
                                      CodestreamIndex &&_index)
            : path(_path),
              parameters(std::move(_params)),
              last_plt(0),
              last_packet(0),
              last_offset_PLT(0),
              last_offset_packet(0),
              max_resolution(-1),
              index(std::move(_index)) {
    }

    ImageIndex::ImageIndex(const string &_path) : path_name(_path) {
    }

    bool ImageIndex::BuildIndex(File *file, Codestream &codestream,
                                const CodingParameters &coding_parameters, int r) {
        // Check the upper top of the index (to build)
        int max_index;
        if (r < coding_parameters.num_levels && coding_parameters.IsResolutionProgression()) {
            // The max_index is the last packet index of the resolution r
            Packet packet(0, r + 1, 0, Size(0, 0));
            max_index = coding_parameters.GetProgressionIndex(packet) - 1;
        } else {
            max_index = coding_parameters.GetNumPackets() - 1;
        }

        uint64_t length_packet = 0;
        bool res = true;
        while (res && codestream.packet_index.Size() <= max_index) {
            res = GetPLTLength(file, codestream, &length_packet);
            if (!res)
                break;
            res = GetOffsetPacket(codestream, length_packet);
        }

        return res;
    }

    bool ImageIndex::GetPLTLength(File *file, Codestream &codestream,
                                  uint64_t *length_packet) {
        vector<FileSegment> &plt = codestream.index.PLT_markers;
        if (codestream.last_plt >= (int) plt.size())
            return false;
        const FileSegment &marker = plt[codestream.last_plt];

        // Get packet plt offset
        uint64_t offset = codestream.last_offset_PLT;
        if (offset == 0)
            offset = marker.offset;
        file->Seek(offset);

        // Get packet length
        uint8_t buf_packet = 0;

        *length_packet = 0;
        do {
            if (file->GetOffset() >= marker.offset + marker.length ||
                !file->Read(&buf_packet) || *length_packet > (UINT64_MAX >> 7))
                return false;
            *length_packet = (*length_packet << 7) | (buf_packet & (uint8_t) 127);
        } while (buf_packet & (uint8_t) 128);

        codestream.last_offset_PLT = file->GetOffset();
        if (codestream.last_offset_PLT == marker.offset + marker.length) {
            codestream.last_plt++;
            codestream.last_offset_PLT = 0;
        }
        return true;
    }

    bool ImageIndex::GetOffsetPacket(Codestream &codestream, uint64_t length_packet) {
        vector<FileSegment> &packets = codestream.index.packets;
        if (codestream.last_packet >= (int) packets.size())
            return false;
        const FileSegment &packet_data = packets[codestream.last_packet];

        uint64_t offset = codestream.last_offset_packet;
        if (offset == 0)
            offset = packet_data.offset;
        uint64_t used = offset - packet_data.offset;
        if (used > packet_data.length || length_packet > packet_data.length - used)
            return false;

        codestream.packet_index.Add(FileSegment(offset, length_packet));
        codestream.last_offset_packet = offset + length_packet;

        if (codestream.last_offset_packet == packet_data.offset + packet_data.length) {
            codestream.last_packet++;
            codestream.last_offset_packet = 0;
        }
        return true;
    }

    bool ImageIndex::GetPacket(File *file, int num_codestream, const Packet &packet, FileSegment *segment, int *offset) {
        Codestream &codestream = codestreams[num_codestream];
        const CodingParameters &coding_parameters = codestream.parameters;

        if (packet.resolution > codestream.max_resolution) {
            if (!BuildIndex(file, codestream, coding_parameters, packet.resolution)) {
                ERROR("The packet index could not be created");
                return false;
            }
            codestream.max_resolution = packet.resolution;
        }

        int idx = coding_parameters.GetProgressionIndex(packet);
        PacketIndex &packet_index = codestream.packet_index;
        if (!packet_index.Get(idx, segment)) {
            ERROR("Invalid packet index: codestream=" << num_codestream << ", index=" << idx << ", size=" << packet_index.Size() << ", packet=" << packet);
            return false;
        }

        if (offset != NULL) {
            *offset = 0;

            if (coding_parameters.IsLayerLastProgression()) {
                for (int l = packet.layer; l > 0; --l)
                    *offset += packet_index[--idx].length;
            } else {
                Packet p_aux = packet;
                for (int l = 0; l < packet.layer; ++l) {
                    p_aux.layer = l;
                    idx = coding_parameters.GetProgressionIndex(p_aux);
                    *offset += packet_index[idx].length;
                }
            }
        }
        return true;
    }

}
