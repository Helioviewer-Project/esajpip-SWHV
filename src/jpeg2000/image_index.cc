#include "trace.h"
#include "image_index.h"

using namespace std;

namespace jpeg2000 {

    using data::File;
    using data::FileSegment;

    ImageIndex::Codestream::Codestream(const string &_path) : path(_path) {
    }

    ImageIndex::ImageIndex(const string &_path) : path_name(_path) {
    }

    bool ImageIndex::BuildIndex(File *file, Codestream &codestream, int max_index) {
        uint64_t length_packet = 0;
        while (codestream.packet_index.Size() <= max_index) {
            if (!GetPLTLength(file, codestream, &length_packet) ||
                !GetOffsetPacket(file, codestream, length_packet))
                return false;
        }
        return true;
    }

    bool ImageIndex::GetPLTLength(File *file, Codestream &codestream,
                                  uint64_t *length_packet) {
        if (codestream.data_cursor.index >= codestream.tile_parts.size())
            return false;
        vector<FileSegment> &plt =
                codestream.tile_parts[codestream.data_cursor.index].plt;
        if (codestream.plt_cursor.index >= plt.size())
            return false;
        const FileSegment &marker = plt[codestream.plt_cursor.index];

        file->Seek(codestream.plt_cursor.offset);

        // Get packet length
        uint8_t buf_packet = 0;

        *length_packet = 0;
        do {
            if (file->GetOffset() >= marker.offset + marker.length ||
                !file->Read(&buf_packet) || *length_packet > (UINT64_MAX >> 7))
                return false;
            *length_packet = (*length_packet << 7) | (buf_packet & (uint8_t) 127);
        } while (buf_packet & (uint8_t) 128);

        codestream.plt_cursor.offset = file->GetOffset();
        if (codestream.plt_cursor.offset == marker.offset + marker.length) {
            codestream.plt_cursor.index++;
            if (codestream.plt_cursor.index < plt.size())
                codestream.plt_cursor.offset =
                        plt[codestream.plt_cursor.index].offset;
        }
        return true;
    }

    bool ImageIndex::GetOffsetPacket(File *file, Codestream &codestream,
                                     uint64_t length_packet) {
        if (codestream.data_cursor.index >= codestream.tile_parts.size())
            return false;
        TilePart &tile_part = codestream.tile_parts[codestream.data_cursor.index];
        const FileSegment &segment = tile_part.data;

        uint64_t offset = codestream.data_cursor.offset;
        uint64_t used = offset - segment.offset;
        if (used > segment.length || length_packet > segment.length - used)
            return false;

        uint64_t next_offset = offset + length_packet;
        bool packet_data_done = next_offset == segment.offset + segment.length;
        bool plt_done = codestream.plt_cursor.index == tile_part.plt.size();
        if (plt_done && !packet_data_done)
            return false;

        bool final_packet = codestream.packet_index.Size() + 1 ==
                            codestream.parameters.GetNumPackets();
        if (!final_packet && packet_data_done && !plt_done)
            return false;

        codestream.data_cursor.offset = next_offset;
        if (final_packet) {
            if (!packet_data_done)
                return false;

            // Some deployed JPEG 2000 files contain zero PLT entries
            // after the logical packet list. T.800 defines one Iplt value per
            // packet, so accept only those zero entries and never expose them
            // as additional packets.
            for (;;) {
                while (!plt_done) {
                    uint64_t padding = 0;
                    if (!GetPLTLength(file, codestream, &padding) || padding != 0)
                        return false;
                    plt_done = codestream.plt_cursor.index ==
                               codestream.tile_parts[
                                       codestream.data_cursor.index].plt.size();
                }

                codestream.data_cursor.index++;
                if (codestream.data_cursor.index == codestream.tile_parts.size())
                    break;
                TilePart &next =
                        codestream.tile_parts[codestream.data_cursor.index];
                if (next.data.length != 0)
                    return false;
                codestream.data_cursor.offset = next.data.offset;
                codestream.plt_cursor.index = 0;
                codestream.plt_cursor.offset = next.plt[0].offset;
                plt_done = false;
            }
        }

        if (!codestream.packet_index.Add(FileSegment(offset, length_packet)))
            return false;
        if (!final_packet && packet_data_done && plt_done) {
            codestream.data_cursor.index++;
            if (codestream.data_cursor.index < codestream.tile_parts.size()) {
                TilePart &next =
                        codestream.tile_parts[codestream.data_cursor.index];
                codestream.data_cursor.offset = next.data.offset;
                codestream.plt_cursor.index = 0;
                codestream.plt_cursor.offset = next.plt[0].offset;
            }
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
