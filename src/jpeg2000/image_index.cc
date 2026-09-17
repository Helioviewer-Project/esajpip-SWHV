#include "trace.h"
#include "image_index.h"

using namespace std;

namespace jpeg2000 {

    using data::File;
    using data::FileSegment;

    ImageIndex::Codestream::Codestream(const string &_path)
            : path(_path),
              last_plt(0),
              last_packet(0),
              last_offset_PLT(0),
              last_offset_packet(0) {
    }

    ImageIndex::ImageIndex(const string &_path) : path_name(_path) {
    }

    bool ImageIndex::BuildIndex(File *file, Codestream &codestream, int max_index) {
        uint64_t length_packet = 0;
        while (codestream.packet_index.Size() <= max_index) {
            if (!GetPLTLength(file, codestream, &length_packet) ||
                !GetOffsetPacket(codestream, length_packet))
                return false;
        }
        return true;
    }

    bool ImageIndex::GetPLTLength(File *file, Codestream &codestream,
                                  uint64_t *length_packet) {
        vector<FileSegment> &plt = codestream.plt;
        if (codestream.last_packet >= (int) codestream.plt_ends.size() ||
            codestream.last_plt >= (int) codestream.plt_ends[codestream.last_packet])
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
        vector<FileSegment> &packet_data = codestream.packet_data;
        if (codestream.last_packet >= (int) packet_data.size())
            return false;
        const FileSegment &segment = packet_data[codestream.last_packet];

        uint64_t offset = codestream.last_offset_packet;
        if (offset == 0)
            offset = segment.offset;
        uint64_t used = offset - segment.offset;
        if (used > segment.length || length_packet > segment.length - used)
            return false;

        uint64_t next_offset = offset + length_packet;
        bool packet_data_done = next_offset == segment.offset + segment.length;
        bool plt_done = codestream.last_plt ==
                            (int) codestream.plt_ends[codestream.last_packet] &&
                        codestream.last_offset_PLT == 0;
        if (packet_data_done != plt_done)
            return false;

        codestream.packet_index.Add(FileSegment(offset, length_packet));
        codestream.last_offset_packet = next_offset;
        if (packet_data_done) {
            codestream.last_packet++;
            codestream.last_offset_packet = 0;
        }
        return true;
    }

    bool ImageIndex::GetPacket(File *file, int num_codestream, const Packet &packet, FileSegment *segment, int *offset) {
        Codestream &codestream = codestreams[num_codestream];
        const CodingParameters &coding_parameters = codestream.parameters;
        int idx = coding_parameters.GetProgressionIndex(packet);

        PacketIndex &packet_index = codestream.packet_index;
        if (idx >= packet_index.Size()) {
            if (!BuildIndex(file, codestream, idx)) {
                ERROR("The packet index could not be created");
                return false;
            }
        }

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
