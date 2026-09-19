#include "file_manager.h"

#include <cerrno>
#include <climits>
#include <cstring>
#include <utility>

#include <glib.h>

using namespace std;

namespace jpeg2000 {

    using data::File;
    using data::FileSegment;

    static bool HasParentSegment(const string &path) {
        size_t begin = 0;
        while (begin <= path.size()) {
            size_t end = path.find('/', begin);
            if (end == string::npos)
                end = path.size();
            if (end - begin == 2 && path[begin] == '.' && path[begin + 1] == '.')
                return true;
            if (end == path.size())
                break;
            begin = end + 1;
        }
        return false;
    }

    FileManager::OpenResult FileManager::OpenImage(const string &path_image_file) {
        if (path_image_file.empty()) {
            ERROR("The image file name is empty");
            return OpenResult::INVALID_PATH;
        }
        if (path_image_file.find('\0') != string::npos ||
            HasParentSegment(path_image_file)) {
            ERROR("Invalid image file path: '" << path_image_file << "'");
            return OpenResult::INVALID_PATH;
        }
        string path = path_image_file;
        if (path[0] == '/')
            path.erase(0, 1);
        path.insert(0, root_dir_);

        unique_ptr<ImageIndex> image_index(new ImageIndex(path));
        OpenResult result = ReadImage(path, image_index.get());
        ClearFiles();
        if (result != OpenResult::OPENED)
            return result;
        image = std::move(image_index);
        return OpenResult::OPENED;
    }

#define EOC_MARKER 0xFFD9
#define SOC_MARKER 0xFF4F
#define SIZ_MARKER 0xFF51
#define COD_MARKER 0xFF52
#define COC_MARKER 0xFF53
#define QCD_MARKER 0xFF5C
#define POC_MARKER 0xFF5F
#define SOT_MARKER 0xFF90
#define PLT_MARKER 0xFF58
#define SOD_MARKER 0xFF93

#define JP2C_BOX_ID 0x6A703263
#define FILE_TYPE_BOX_ID 0x66747970
#define ASOC_BOX_ID 0x61736F63
#define JPCH_BOX_ID 0x6A706368
#define FTBL_BOX_ID 0x6674626C
#define DBTL_BOX_ID 0x6474626C
#define URL__BOX_ID 0x75726C20
#define FLST_BOX_ID 0x666C7374

    static const unsigned char JP2_SIGNATURE_BOX[] = {
        0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50, 0x20, 0x20,
        0x0D, 0x0A, 0x87, 0x0A
    };

    static bool ReadBoxHeader(File *file, uint64_t limit, uint32_t *type_box,
                              uint64_t *length_box);
    static bool ReadSIZMarker(File *file, uint64_t limit,
                              CodingParameters *params, bool *mct_compatible);
    static bool ReadCODMarker(File *file, uint64_t limit,
                              CodingParameters *params, bool mct_compatible);
    static bool ReadSOTMarker(File *file, uint64_t limit,
                              FileSegment &header,
                              size_t num_tile_parts,
                              FileSegment &data,
                              uint8_t *declared_tile_parts);
    static bool ReadPLTMarker(File *file, uint64_t limit,
                              const FileSegment &data,
                              vector<FileSegment> &plt);
    static bool ReadSODMarker(File *file, uint64_t limit,
                              FileSegment &data,
                              const vector<FileSegment> &plt);
    static bool ReadFlstBox(File *file, uint64_t length_box,
                            FileSegment *fragment,
                            uint16_t *data_reference);

    static bool ReadFileType(File *file, uint64_t length,
                             uint32_t expected_brand) {
        if (length < 12 || (length & 3) != 0)
            return false;

        uint32_t brand = 0;
        uint32_t minor_version = 0;
        if (!file->ReadReverse(&brand) || !file->ReadReverse(&minor_version) ||
            brand != expected_brand)
            return false;

        bool compatible = false;
        for (uint64_t offset = 8; offset < length; offset += 4) {
            uint32_t entry = 0;
            if (!file->ReadReverse(&entry))
                return false;
            compatible |= entry == expected_brand;
        }
        return compatible;
    }

    static bool SkipMarker(File *file, uint64_t limit) {
        uint16_t length = 0;
        if (file->GetOffset() > limit || !file->ReadReverse(&length) || length < 2 ||
            static_cast<uint64_t>(length) - 2 > limit - file->GetOffset())
            return false;
        file->Seek(length - 2, SEEK_CUR);
        return true;
    }

    static bool ValidateMetadata(const Metadata &metadata) {
        uint64_t bin0_length = metadata.tail.length;
        for (const Metadata::Part &part : metadata.bin0) {
            uint64_t placeholder_length = part.placeholder.header.length +
                    (part.placeholder.is_jp2c ? 44 : 20);
            if (part.data.length > INT_MAX || placeholder_length > INT_MAX ||
                bin0_length > static_cast<uint64_t>(INT_MAX) - part.data.length ||
                bin0_length + part.data.length >
                        static_cast<uint64_t>(INT_MAX) - placeholder_length)
                return false;
            bin0_length += part.data.length + placeholder_length;
        }
        if (bin0_length > INT_MAX)
            return false;
        for (const FileSegment &bin : metadata.bins)
            if (bin.length > INT_MAX)
                return false;
        return true;
    }

    FileManager::OpenResult FileManager::ReadImage(
            const string &name_image_file, ImageIndex *image_index) {
        // Get file extension
        string extension;
        size_t pos = name_image_file.find_last_of(".");
        if (pos != string::npos) extension = name_image_file.substr(pos);

        if (extension != ".jp2" && extension != ".jpx") {
            ERROR("Unsupported image file type: '" << name_image_file << "'");
            return OpenResult::UNSUPPORTED;
        }

        File file;
        File::OpenResult open_result = file.Open(name_image_file.c_str(), INT_MAX);
        if (open_result != File::OpenResult::OPENED) {
            if (open_result == File::OpenResult::NOT_FOUND)
                return OpenResult::NOT_FOUND;
            if (open_result == File::OpenResult::EMPTY ||
                open_result == File::OpenResult::TOO_LARGE) {
                ERROR("Unsupported JPEG 2000 source size in '"
                      << name_image_file << "'");
                return OpenResult::INVALID;
            }
            return OpenResult::UNREADABLE;
        }

        unsigned char signature_box[sizeof JP2_SIGNATURE_BOX];
        uint32_t second_type = 0;
        uint64_t second_length = 0;
        uint32_t expected_brand = extension == ".jp2"
                                  ? 0x6A703220 : 0x6A707820;
        bool res = file.Read(signature_box, sizeof signature_box) &&
                   memcmp(signature_box, JP2_SIGNATURE_BOX,
                          sizeof signature_box) == 0 &&
                   ReadBoxHeader(&file, file.GetSize(), &second_type,
                                 &second_length) &&
                   second_type == FILE_TYPE_BOX_ID &&
                   ReadFileType(&file, second_length, expected_brand);
        if (!res) {
            ERROR("Invalid JPEG 2000 file preamble in '" << name_image_file << "'");
            return OpenResult::INVALID;
        }
        file.Seek(0);

        if (extension == ".jp2")
            res = ReadJP2(&file, image_index);
        else
            res = ReadJPX(&file, image_index);
        if (!res) {
            ERROR("Could not parse image file '" << name_image_file << "'");
        } else if (!ValidateMetadata(image_index->GetMetadata())) {
            ERROR("JPEG 2000 metadata exceeds the supported offset range in '"
                  << name_image_file << "'");
            res = false;
        }
        return res ? OpenResult::OPENED : OpenResult::INVALID;
    }

    bool FileManager::ReadCodestream(File *file, uint64_t length,
                                     ImageIndex::Codestream &codestream) {
        enum Phase {
            MAIN_HEADER,
            TILE_HEADER,
            BETWEEN_TILE_PARTS
        };

        if (length < 4 ||
            length > file->GetSize() - file->GetOffset())
            return false;

        uint64_t limit = file->GetOffset() + length;
        bool siz = false;
        bool cod = false;
        bool qcd = false;
        bool first_marker = true;
        bool mct_compatible = false;
        Phase phase = MAIN_HEADER;
        uint8_t declared_tile_parts = 0;
        uint16_t value = 0;

        if (!file->ReadReverse(&value) || value != SOC_MARKER)
            return false;
        codestream.header.offset = file->GetOffset() - 2;

        while (file->GetOffset() < limit) {
            if (!file->ReadReverse(&value))
                break;
            if ((value & 0xFF00) != 0xFF00 || value == 0xFFFF ||
                (first_marker && value != SIZ_MARKER))
                return false;
            if (phase == BETWEEN_TILE_PARTS &&
                value != SOT_MARKER && value != EOC_MARKER)
                return false;
            first_marker = false;
            uint64_t marker_limit = limit;
            if (phase == TILE_HEADER &&
                codestream.tile_parts.back().data.length != 0)
                marker_limit = codestream.tile_parts.back().data.offset +
                               codestream.tile_parts.back().data.length;
            switch (value) {
                case SIZ_MARKER: TRACE("SIZ marker...");
                    if (phase != MAIN_HEADER || siz ||
                        !ReadSIZMarker(file, limit, &codestream.parameters,
                                       &mct_compatible))
                        return false;
                    siz = true;
                    break;

                case COD_MARKER: TRACE("COD marker...");
                    if (phase != MAIN_HEADER || cod ||
                        !ReadCODMarker(file, marker_limit,
                                       &codestream.parameters,
                                       mct_compatible))
                        return false;
                    cod = true;
                    break;

                case COC_MARKER:
                case POC_MARKER:
                    // Packet indexing is derived from the main COD marker.
                    // Component-specific coding and progression changes need
                    // their own indexing model and cannot be skipped safely.
                    return false;

                case SOT_MARKER: {
                    TRACE("SOT marker...");
                    FileSegment data;
                    if (phase == TILE_HEADER || !siz || !cod || !qcd ||
                        !ReadSOTMarker(file, limit, codestream.header,
                                       codestream.tile_parts.size(), data,
                                       &declared_tile_parts))
                        return false;
                    codestream.tile_parts.emplace_back(data);
                    phase = TILE_HEADER;
                    break;
                }

                case QCD_MARKER:
                    if (phase != MAIN_HEADER || qcd ||
                        !SkipMarker(file, marker_limit))
                        return false;
                    qcd = true;
                    break;

                case PLT_MARKER: TRACE("PLT marker...");
                    if (phase != TILE_HEADER ||
                        !ReadPLTMarker(file, limit,
                                       codestream.tile_parts.back().data,
                                       codestream.tile_parts.back().plt))
                        return false;
                    break;

                case SOD_MARKER: TRACE("SOD marker...");
                    if (phase != TILE_HEADER ||
                        !ReadSODMarker(file, limit,
                                       codestream.tile_parts.back().data,
                                       codestream.tile_parts.back().plt))
                        return false;
                    phase = BETWEEN_TILE_PARTS;
                    break;

                case EOC_MARKER:
                    if (phase == TILE_HEADER || codestream.tile_parts.empty() ||
                        file->GetOffset() != limit ||
                        (declared_tile_parts != 0 &&
                         codestream.tile_parts.size() != declared_tile_parts))
                        return false;
                    codestream.data_cursor.offset =
                            codestream.tile_parts[0].data.offset;
                    codestream.plt_cursor.offset =
                            codestream.tile_parts[0].plt[0].offset;
                    return true;

                case SOC_MARKER:
                    return false;

                default:
                    // Tile-header contents are not sent: the JPIP tile-header
                    // data-bin is empty. PLT is the only supported tile-header
                    // segment because it is used only to locate packets.
                    if (phase == TILE_HEADER || !SkipMarker(file, marker_limit))
                        return false;
            }
        }

        ERROR("The code-stream does not end with an EOC marker");
        return false;
    }

    static bool ReadSIZMarker(File *file, uint64_t limit,
                              CodingParameters *params, bool *mct_compatible) {
        uint16_t lsiz = 0;
        if (file->GetOffset() > limit || !file->ReadReverse(&lsiz) || lsiz < 41 ||
            static_cast<uint64_t>(lsiz) - 2 > limit - file->GetOffset())
            return false;
        file->Seek(2, SEEK_CUR); // Rsiz

        uint32_t image[4];
        for (int i = 0; i < 4; ++i)
            if (!file->ReadReverse(&image[i]))
                return false;

        uint32_t tiling[4];
        for (int i = 0; i < 4; ++i)
            if (!file->ReadReverse(&tiling[i]))
                return false;

        uint16_t num_components = 0;
        if (!file->ReadReverse(&num_components) || num_components == 0 || num_components > 16384 ||
            lsiz != 38 + 3 * num_components || image[0] <= image[2] || image[1] <= image[3] ||
            image[2] != 0 || image[3] != 0 ||
            image[0] - image[2] > INT_MAX || image[1] - image[3] > INT_MAX ||
            tiling[0] == 0 || tiling[1] == 0)
            return false;
        if (tiling[2] > image[2] || tiling[3] > image[3] ||
            tiling[0] < image[0] - tiling[2] || tiling[1] < image[1] - tiling[3])
            return false;

        params->size = Size(image[0] - image[2], image[1] - image[3]);
        params->origin = Point(image[2], image[3]);
        params->num_components = num_components;
        int first_depth = -1;
        *mct_compatible = num_components >= 3;
        for (uint16_t i = 0; i < num_components; ++i) {
            uint8_t precision = 0;
            uint8_t xrsiz = 0;
            uint8_t yrsiz = 0;
            if (!file->ReadReverse(&precision) || !file->ReadReverse(&xrsiz) || !file->ReadReverse(&yrsiz) ||
                (precision & 0x7F) > 37 || xrsiz != 1 || yrsiz != 1)
                return false;
            int depth = precision & 0x7F;
            if (i == 0)
                first_depth = depth;
            else if (i < 3 && depth != first_depth)
                *mct_compatible = false;
        }
        return true;
    }

    static bool ReadCODMarker(File *file, uint64_t limit,
                              CodingParameters *params, bool mct_compatible) {
        uint16_t lcod = 0;
        uint8_t cs_buf = 0;
        uint8_t progression = 0;
        uint16_t quality_layers = 0;
        uint8_t mct = 0;
        uint8_t transform_levels = 0;
        uint8_t cb_width = 0;
        uint8_t cb_height = 0;
        uint8_t cb_style = 0;
        uint8_t transform = 0;
        if (file->GetOffset() > limit || !file->ReadReverse(&lcod) || lcod < 12 ||
            static_cast<uint64_t>(lcod) - 2 > limit - file->GetOffset() ||
            !file->ReadReverse(&cs_buf) || !file->ReadReverse(&progression) ||
            !file->ReadReverse(&quality_layers) || !file->ReadReverse(&mct) ||
            !file->ReadReverse(&transform_levels) || !file->ReadReverse(&cb_width) ||
            !file->ReadReverse(&cb_height) || !file->ReadReverse(&cb_style) ||
            !file->ReadReverse(&transform))
            return false;

        uint16_t expected_length = 12 + ((cs_buf & 1) ? transform_levels + 1 : 0);
        if (lcod != expected_length || (cs_buf & 0xFA) != 0 || progression > 4 || quality_layers == 0 ||
            mct > 1 || (mct != 0 && !mct_compatible) ||
            transform_levels > 32 || cb_width > 8 || cb_height > 8 ||
            cb_width + cb_height > 8 || cb_style > 63 || transform > 1)
            return false;

        params->progression = progression;
        params->num_layers = quality_layers;
        params->num_levels = transform_levels;
        params->resolutions.clear();
        for (int i = 0; i <= params->num_levels; ++i) {
            uint8_t size_precinct = 0xFF;
            if (cs_buf & 1) {
                if (!file->ReadReverse(&size_precinct))
                    return false;
                if (i > 0 && ((size_precinct & 0x0F) == 0 ||
                              (size_precinct & 0xF0) == 0))
                    return false;

            }
            params->resolutions.emplace_back(
                    1 << (size_precinct & 0x0F),
                    1 << ((size_precinct & 0xF0) >> 4));
        }
        return true;
    }

    static bool ReadSOTMarker(File *file, uint64_t limit,
                              FileSegment &header,
                              size_t num_tile_parts,
                              FileSegment &data,
                              uint8_t *declared_tile_parts) {
        uint64_t marker_offset = file->GetOffset() - 2;
        uint16_t lsot = 0;
        uint16_t isot = 0;
        uint32_t psot = 0;
        uint8_t tpsot = 0;
        uint8_t tnsot = 0;
        if (!file->ReadReverse(&lsot) || !file->ReadReverse(&isot) || !file->ReadReverse(&psot) ||
            !file->ReadReverse(&tpsot) || !file->ReadReverse(&tnsot) || lsot != 10 || isot != 0 ||
            tpsot != num_tile_parts || (tnsot != 0 && tpsot >= tnsot) ||
            (tnsot != 0 && *declared_tile_parts != 0 &&
             tnsot != *declared_tile_parts) ||
            (psot != 0 && psot < 14) ||
            (psot != 0 && psot > limit - marker_offset) ||
            num_tile_parts >= PacketIndex::MAX_SEGMENTS)
            return false;

        if (tnsot != 0 && *declared_tile_parts == 0)
            *declared_tile_parts = tnsot;

        if (header.length == 0)
            header.length = marker_offset - header.offset;
        data = FileSegment(file->GetOffset(), psot == 0 ? 0 : psot - 12);
        return true;
    }

    static bool ReadPLTMarker(File *file, uint64_t limit,
                              const FileSegment &data,
                              vector<FileSegment> &plt) {
        if (data.length != 0)
            limit = data.offset + data.length;

        uint16_t lplt = 0;
        uint8_t zplt = 0;
        if (file->GetOffset() > limit || !file->ReadReverse(&lplt) || lplt < 4 ||
            static_cast<uint64_t>(lplt) - 2 > limit - file->GetOffset() ||
            !file->ReadReverse(&zplt) || zplt != plt.size())
            return false;

        plt.emplace_back(file->GetOffset(), lplt - 3);
        file->Seek(lplt - 3, SEEK_CUR);
        return true;
    }

    static bool ReadSODMarker(File *file, uint64_t limit,
                              FileSegment &data,
                              const vector<FileSegment> &plt) {
        if (plt.empty())
            return false;

        if (data.length == 0) {
            data.offset = file->GetOffset();
            // JPEG 2000 bit stuffing prevents an EOC marker from appearing in
            // packet data, so the first FF D9 terminates the final tile-part.
            while (file->Find(0xFF, limit)) {
                uint64_t marker_offset = file->GetOffset() - 1;
                if (file->GetOffset() >= limit)
                    return false;
                uint8_t value;
                if (!file->Read(&value))
                    return false;
                if (value == (EOC_MARKER & 0xFF)) {
                    data.length = file->GetOffset() - 2 - data.offset;
                    file->Seek(file->GetOffset() - 2);
                    return true;
                }
                if (value == (SOT_MARKER & 0xFF))
                    return false;
                if (value == 0xFF)
                    file->Seek(marker_offset + 1);
            }
            return false;
        }

        uint64_t header_length = file->GetOffset() - data.offset;
        if (header_length > data.length)
            return false;
        data.length -= header_length;
        data.offset = file->GetOffset();
        file->Seek(data.length, SEEK_CUR);
        return true;
    }

    static bool ReadBoxHeader(File *file, uint64_t limit,
                              uint32_t *type_box, uint64_t *length_box) {
        if (limit > file->GetSize() || file->GetOffset() > limit || limit - file->GetOffset() < 8)
            return false;

        // Get L, if it is not 0 or 1, then box length is L
        uint32_t L = 0;
        // Get T (box type)
        if (!file->ReadReverse(&L) || !file->ReadReverse(type_box))
            return false;

        // XL indicates the box length
        if (L == 1) {
            uint64_t XL = 0;
            if (!file->ReadReverse(&XL) || XL < 16)
                return false;
            *length_box = XL - 16;
        } else if (L == 0) {
            // T.800 defines LBox=0 as extending to the end of the file. The
            // limit check below rejects this form inside an earlier-ending
            // superbox.
            *length_box = file->GetSize() - file->GetOffset();
        } else {
            if (L < 8)
                return false;
            *length_box = L - 8;
        }
        return *length_box <= limit - file->GetOffset();
    }

    bool FileManager::ReadJP2(File *file, ImageIndex *image_index) {
        bool found_codestream = false;
        uint64_t meta_start = 0;
        while (file->GetOffset() != file->GetSize()) {
            uint64_t box_start = file->GetOffset();
            uint64_t prefix_length = box_start - meta_start;
            uint32_t type_box;
            uint64_t length_box;
            if (!ReadBoxHeader(file, file->GetSize(), &type_box, &length_box))
                return false;
            uint64_t header_length = file->GetOffset() - box_start;
            switch (type_box) {
                case JP2C_BOX_ID: TRACE("JP2C box...");
                    if (found_codestream)
                        return false;
                    image_index->codestreams.emplace_back(image_index->path_name);
                    if (!ReadCodestream(file, length_box,
                                        image_index->codestreams.back()))
                        return false;
                    found_codestream = true;
                    image_index->meta_data.bin0.emplace_back(
                            FileSegment(meta_start, prefix_length),
                            PlaceHolder(0, true,
                                        FileSegment(box_start, header_length)));
                    meta_start = file->GetOffset();
                    break;

                default:
                    file->Seek(length_box, SEEK_CUR);
            }
        }
        image_index->meta_data.tail =
                FileSegment(meta_start, file->GetOffset() - meta_start);
        if (!found_codestream)
            return false;

        return image_index->codestreams.back().parameters.FillPrecinctCounts();
    }

    bool FileManager::ReadJPX(File *file, ImageIndex *image_index) {
        struct Container {
            uint32_t type;
            uint64_t end;
        };
        struct Link {
            uint16_t reference;
            FileSegment fragment;
        };

        vector<string> references;
        uint64_t meta_start = 0;
        FileSegment ftbl_prefix;
        FileSegment ftbl_header;
        int num_flst = 0;
        uint16_t num_data_references = 0;
        bool has_data_reference_box = false;
        vector<ImageIndex::Codestream> codestreams;
        size_t num_codestream_headers = 0;
        size_t num_codestreams = 0;
        size_t ftbl_codestream = 0;
        vector<Link> links;
        vector<Container> containers{{0, file->GetSize()}};

        while (true) {
            while (containers.size() > 1 && file->GetOffset() == containers.back().end) {
                if (containers.back().type == FTBL_BOX_ID && num_flst != 1)
                    return false;
                containers.pop_back();
            }
            if (file->GetOffset() == file->GetSize())
                break;
            if (file->GetOffset() > containers.back().end)
                return false;

            uint64_t box_start = file->GetOffset();
            uint64_t prefix_length = box_start - meta_start;
            uint32_t type_box;
            uint64_t length_box;
            if (!ReadBoxHeader(file, containers.back().end, &type_box, &length_box))
                return false;
            uint64_t header_length = file->GetOffset() - box_start;
            uint64_t box_end = file->GetOffset() + length_box;
            switch (type_box) {
                case JPCH_BOX_ID: TRACE("JPCH box...");
                    if (containers.size() != 1)
                        return false;
                    num_codestream_headers++;
                    if (length_box != 0)
                        containers.push_back({type_box, box_end});
                    break;
                case JP2C_BOX_ID: {
                    TRACE("JP2C box...");
                    if (containers.size() != 1)
                        return false;
                    size_t codestream_id = num_codestreams++;
                    codestreams.emplace_back(image_index->path_name);
                    ImageIndex::Codestream &codestream = codestreams.back();
                    if (!ReadCodestream(file, length_box, codestream))
                        return false;
                    if (!codestream.parameters.FillPrecinctCounts())
                        return false;
                    image_index->meta_data.bin0.emplace_back(
                            FileSegment(meta_start, prefix_length),
                            PlaceHolder(codestream_id, true,
                                        FileSegment(box_start, header_length)));
                    meta_start = file->GetOffset();
                    break;
                }
                case ASOC_BOX_ID: TRACE("ASOC box...");
                    file->Seek(length_box, SEEK_CUR);
                    image_index->meta_data.bins.emplace_back(box_start + header_length,
                                                             length_box);
                    image_index->meta_data.bin0.emplace_back(
                            FileSegment(meta_start, prefix_length),
                            PlaceHolder(image_index->meta_data.bins.size(), false,
                                        FileSegment(box_start, header_length)));
                    meta_start = file->GetOffset();
                    break;
                    // 'ftbl' superbox contains a 'flst'
                case FTBL_BOX_ID: TRACE("FTBL box...");
                    if (containers.size() != 1)
                        return false;
                    num_flst = 0;
                    ftbl_codestream = num_codestreams++;
                    ftbl_prefix = FileSegment(meta_start, prefix_length);
                    ftbl_header = FileSegment(box_start, header_length);
                    if (length_box < 8)
                        return false;
                    containers.push_back({type_box, box_end});
                    break;
                    // 'flst' box assumed to be contained within a 'ftbl' superbox
                case FLST_BOX_ID: {
                    TRACE("FLST box...");
                    FileSegment fragment;
                    uint16_t data_reference;
                    if (containers.back().type != FTBL_BOX_ID || num_flst != 0 ||
                        !ReadFlstBox(file, length_box, &fragment, &data_reference))
                        return false;
                    num_flst++;
                    image_index->meta_data.bin0.emplace_back(
                            ftbl_prefix,
                            PlaceHolder(ftbl_codestream, true, ftbl_header));
                    links.push_back({data_reference, fragment});
                    meta_start = file->GetOffset();
                    break;
                }
                case DBTL_BOX_ID: TRACE("DBTL box...");
                    if (containers.size() != 1 || has_data_reference_box || length_box < 2)
                        return false;
                    if (!file->ReadReverse(&num_data_references))
                        return false;
                    has_data_reference_box = true;
                    if (file->GetOffset() != box_end)
                        containers.push_back({type_box, box_end});
                    break;
                case URL__BOX_ID: {
                    TRACE("URL box...");
                    string path_file;
                    if (containers.back().type != DBTL_BOX_ID ||
                        !ReadUrlBox(file, length_box, &path_file))
                        return false;
                    if (path_file[0] != '/') {
                        size_t separator = image_index->path_name.find_last_of('/');
                        if (separator != string::npos)
                            path_file.insert(0, image_index->path_name, 0,
                                             separator + 1);
                    }
                    references.push_back(std::move(path_file));
                    break;
                }
                default:
                    file->Seek(length_box, SEEK_CUR);
            }
        }
        image_index->meta_data.tail =
                FileSegment(meta_start, file->GetOffset() - meta_start);

        if (containers.size() != 1 || num_codestream_headers == 0 ||
            num_codestreams != num_codestream_headers ||
            references.size() != num_data_references)
            return false;

        // This profile supports embedded or linked codestreams, not a mixture.
        if (!codestreams.empty() && !links.empty())
            return false;

        bool sequential_references = links.size() == references.size();
        for (size_t i = 0; i < links.size(); ++i) {
            if (links[i].reference == 0 || links[i].reference > references.size())
                return false;
            if (links[i].reference != i + 1)
                sequential_references = false;
        }
        vector<string> paths;
        if (sequential_references)
            paths = std::move(references);
        else {
            paths.reserve(links.size());
            for (const Link &link : links)
                paths.push_back(references[link.reference - 1]);
        }

        // Resolve the linked codestreams.
        if (paths.empty()) {
            if (codestreams.size() != num_codestreams)
                return false;
            image_index->codestreams = std::move(codestreams);
            return true;
        }

        if (paths.size() != num_codestreams)
            return false;
        image_index->codestreams.reserve(paths.size());
        for (size_t i = 0; i < paths.size(); ++i) {
            if (paths[i].size() < 4 ||
                paths[i].compare(paths[i].size() - 4, 4, ".jp2") != 0) {
                ERROR("Unsupported linked codestream file '" << paths[i] << "'");
                return false;
            }
            ImageIndex linked_image(paths[i]);
            if (ReadImage(paths[i], &linked_image) != OpenResult::OPENED)
                return false;

            if (linked_image.codestreams.empty() ||
                linked_image.codestreams.back().tile_parts.empty())
                return false;
            ImageIndex::Codestream &codestream = linked_image.codestreams.back();
            const FileSegment &last_packet_data =
                    codestream.tile_parts.back().data;
            uint64_t codestream_length =
                    last_packet_data.offset + last_packet_data.length + 2 - codestream.header.offset;
            if (links[i].fragment != FileSegment(codestream.header.offset,
                                                  codestream_length))
                return false;

            codestream.path = std::move(paths[i]);
            image_index->codestreams.push_back(
                    std::move(codestream));
        }
        return true;
    }

    static bool ReadFlstBox(File *file, uint64_t length_box,
                            FileSegment *fragment,
                            uint16_t *data_reference) {
        if (length_box != 16)
            return false;

        uint16_t num_fragments = 0;
        if (!file->ReadReverse(&num_fragments) || num_fragments != 1)
            return false;

        uint32_t fragment_length = 0;
        if (!file->ReadReverse(&fragment->offset) || !file->ReadReverse(&fragment_length) ||
            !file->ReadReverse(data_reference))
            return false;
        fragment->length = fragment_length;
        return true;
    }

    bool FileManager::ReadUrlBox(File *file, uint64_t length_box, string *path_file) {
        if (length_box < 5)
            return false;

        uint32_t version_flags = 0;
        if (!file->ReadReverse(&version_flags) || version_flags != 0)
            return false;

        string local_path(length_box - 4, '\0');
        if (!file->Read(&local_path[0], local_path.size()) || local_path.back() != '\0' ||
            local_path.find('\0') != local_path.size() - 1 || local_path.compare(0, 7, "file://") != 0)
            return false;
        local_path.resize(local_path.size() - 1);
        local_path.erase(0, 7);

        // undo possible URI character substitutions
        char *unescaped = g_uri_unescape_string(local_path.c_str(), NULL);
        if (!unescaped)
            return false;
        *path_file = unescaped;
        g_free(unescaped);
        return !path_file->empty();
    }

}
