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

        // Get packet length: 7-bit groups, at most ten bytes (70 bits hold
        // any 64-bit value; a longer chain only adds leading zero groups or
        // overflows).
        uint8_t buf_packet = 0;
        int bytes = 0;

        *length_packet = 0;
        do {
            if (file->GetOffset() >= marker.offset + marker.length || ++bytes > 10 ||
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
        if (length_packet == 0)
            return false;
        if (codestream.data_cursor.index >= codestream.tile_parts.size())
            return false;
        const TilePart &tile_part = codestream.tile_parts[codestream.data_cursor.index];
        const FileSegment &segment = tile_part.data;

        uint64_t offset = codestream.data_cursor.offset;
        uint64_t used = offset - segment.offset;
        if (used > segment.length || length_packet > segment.length - used)
            return false;

        uint64_t next_offset = offset + length_packet;
        bool packet_data_done = next_offset == segment.offset + segment.length;
        const bool plt_done = codestream.plt_cursor.index == tile_part.plt.size();
        bool final_packet = codestream.packet_index.Size() + 1 ==
                            codestream.parameters.GetNumPackets();
        if (final_packet) {
            if (!packet_data_done)
                return false;
        } else if (plt_done != packet_data_done) {
            return false;
        }

        codestream.data_cursor.offset = next_offset;
        if (final_packet) {
            // Some deployed JPEG 2000 files contain zero PLT entries
            // after the logical packet list. T.800 defines one Iplt value per
            // packet, so accept only those zero entries and never expose them
            // as additional packets. Later tile-parts must have no data.
            while (codestream.data_cursor.index < codestream.tile_parts.size()) {
                const TilePart &current =
                        codestream.tile_parts[codestream.data_cursor.index];
                while (codestream.plt_cursor.index < current.plt.size()) {
                    uint64_t padding = 0;
                    if (!GetPLTLength(file, codestream, &padding) || padding != 0)
                        return false;
                }

                codestream.data_cursor.index++;
                if (codestream.data_cursor.index < codestream.tile_parts.size()) {
                    const TilePart &next =
                            codestream.tile_parts[codestream.data_cursor.index];
                    if (next.data.length != 0)
                        return false;
                    codestream.data_cursor.offset = next.data.offset;
                    codestream.plt_cursor.index = 0;
                    codestream.plt_cursor.offset = next.plt[0].offset;
                }
            }
            return codestream.packet_index.Add(FileSegment(offset, length_packet));
        }

        if (!codestream.packet_index.Add(FileSegment(offset, length_packet)))
            return false;
        if (packet_data_done) {
            codestream.data_cursor.index++;
            if (codestream.data_cursor.index < codestream.tile_parts.size()) {
                const TilePart &next =
                        codestream.tile_parts[codestream.data_cursor.index];
                codestream.data_cursor.offset = next.data.offset;
                codestream.plt_cursor.index = 0;
                codestream.plt_cursor.offset = next.plt[0].offset;
            }
        }
        return true;
    }

    bool ImageIndex::GetPacket(File *file, int num_codestream,
                               const Packet &packet, FileSegment *segment) {
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

        return true;
    }

}
