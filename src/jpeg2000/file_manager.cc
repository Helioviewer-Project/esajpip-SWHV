#include "file_manager.h"

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

    bool FileManager::OpenImage(const string &path_image_file) {
        if (path_image_file.empty()) {
            ERROR("The image file name is empty");
            return false;
        }
        if (path_image_file.find('\0') != string::npos ||
            HasParentSegment(path_image_file)) {
            ERROR("Invalid image file path: '" << path_image_file << "'");
            return false;
        }
        string path = path_image_file;
        if (path[0] == '/')
            path.erase(0, 1);
        path.insert(0, root_dir_);

        unique_ptr<ImageIndex> image_index(new ImageIndex(path));
        bool loaded = ReadImage(path, image_index.get());
        ClearFiles();
        if (!loaded)
            return false;
        image = std::move(image_index);
        return true;
    }

#define EOC_MARKER 0xFFD9
#define SOC_MARKER 0xFF4F
#define SIZ_MARKER 0xFF51
#define COD_MARKER 0xFF52
#define QCD_MARKER 0xFF5C
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
                              CodingParameters *params);
    static bool ReadCODMarker(File *file, uint64_t limit,
                              CodingParameters *params);
    static bool ReadSOTMarker(File *file, uint64_t limit,
                              FileSegment &header,
                              vector<FileSegment> &packet_data,
                              uint8_t *declared_tile_parts);
    static bool ReadPLTMarker(File *file, uint64_t limit,
                              const vector<FileSegment> &packet_data,
                              vector<FileSegment> &plt);
    static bool ReadSODMarker(File *file, uint64_t limit,
                              vector<FileSegment> &packet_data);
    static bool ReadFlstBox(File *file, uint64_t length_box,
                            FileSegment *fragment,
                            uint16_t *data_reference);

    static bool SkipMarker(File *file, uint64_t limit) {
        uint16_t length = 0;
        if (file->GetOffset() > limit || !file->ReadReverse(&length) || length < 2 ||
            static_cast<uint64_t>(length) - 2 > limit - file->GetOffset())
            return false;
        file->Seek(length - 2, SEEK_CUR);
        return true;
    }

    bool FileManager::ReadImage(const string &name_image_file, ImageIndex *image_index) {
        // Get file extension
        string extension;
        size_t pos = name_image_file.find_last_of(".");
        if (pos != string::npos) extension = name_image_file.substr(pos);

        if (extension != ".jp2" && extension != ".jpx") {
            ERROR("Unsupported image file type: '" << name_image_file << "'");
            return false;
        }

        File file;
        if (!file.Open(name_image_file)) {
            ERROR("Could not open image file '" << name_image_file << "'");
            return false;
        }

        unsigned char signature_box[sizeof JP2_SIGNATURE_BOX];
        uint32_t second_type = 0;
        uint64_t second_length = 0;
        bool res = file.Read(signature_box, sizeof signature_box) &&
                   memcmp(signature_box, JP2_SIGNATURE_BOX,
                          sizeof signature_box) == 0 &&
                   ReadBoxHeader(&file, file.GetSize(), &second_type,
                                 &second_length) &&
                   second_type == FILE_TYPE_BOX_ID;
        if (!res) {
            ERROR("Invalid JPEG 2000 file preamble in '" << name_image_file << "'");
            return false;
        }
        file.Seek(0);

        if (extension == ".jp2")
            res = ReadJP2(&file, image_index);
        else
            res = ReadJPX(&file, image_index);
        if (!res)
            ERROR("Could not parse image file '" << name_image_file << "'");
        return res;
    }

    bool FileManager::ReadCodestream(File *file, uint64_t length,
                                     ImageIndex::Codestream &codestream) {
        enum Phase {
            MAIN_HEADER,
            TILE_HEADER,
            BETWEEN_TILE_PARTS
        };

        if (file->GetSize() > INT_MAX || length < 4 ||
            length > file->GetSize() - file->GetOffset())
            return false;

        uint64_t limit = file->GetOffset() + length;
        bool siz = false;
        bool cod = false;
        bool qcd = false;
        bool first_marker = true;
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
            if (phase == TILE_HEADER && !codestream.packet_data.empty() &&
                codestream.packet_data.back().length != 0)
                marker_limit = codestream.packet_data.back().offset +
                               codestream.packet_data.back().length;
            switch (value) {
                case SIZ_MARKER: TRACE("SIZ marker...");
                    if (phase != MAIN_HEADER || siz ||
                        !ReadSIZMarker(file, limit, &codestream.parameters))
                        return false;
                    siz = true;
                    break;

                case COD_MARKER: TRACE("COD marker...");
                    if ((phase == MAIN_HEADER && cod) ||
                        !ReadCODMarker(file, marker_limit,
                                       &codestream.parameters))
                        return false;
                    if (phase == MAIN_HEADER)
                        cod = true;
                    break;

                case SOT_MARKER: TRACE("SOT marker...");
                    if (phase == TILE_HEADER || !siz || !cod || !qcd ||
                        !ReadSOTMarker(file, limit, codestream.header,
                                       codestream.packet_data,
                                       &declared_tile_parts))
                        return false;
                    phase = TILE_HEADER;
                    break;

                case QCD_MARKER:
                    if ((phase == MAIN_HEADER && qcd) ||
                        !SkipMarker(file, marker_limit))
                        return false;
                    if (phase == MAIN_HEADER)
                        qcd = true;
                    break;

                case PLT_MARKER: TRACE("PLT marker...");
                    if (phase != TILE_HEADER ||
                        !ReadPLTMarker(file, limit, codestream.packet_data,
                                       codestream.plt))
                        return false;
                    break;

                case SOD_MARKER: TRACE("SOD marker...");
                    if (phase != TILE_HEADER ||
                        !ReadSODMarker(file, limit, codestream.packet_data))
                        return false;
                    phase = BETWEEN_TILE_PARTS;
                    break;

                case EOC_MARKER:
                    if (phase == TILE_HEADER || codestream.packet_data.empty() ||
                        file->GetOffset() != limit ||
                        (declared_tile_parts != 0 &&
                         codestream.packet_data.size() != declared_tile_parts))
                        return false;
                    if (!codestream.plt.empty())
                        return true;
                    ERROR("The code-stream does not include any PLT marker");
                    return false;

                case SOC_MARKER:
                    return false;

                default:
                    if (!SkipMarker(file, marker_limit))
                        return false;
            }
        }

        ERROR("The code-stream does not end with an EOC marker");
        return false;
    }

    static bool ReadSIZMarker(File *file, uint64_t limit,
                              CodingParameters *params) {
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
        params->num_components = num_components;
        params->position_order_supported = true;
        for (uint16_t i = 0; i < num_components; ++i) {
            uint8_t precision = 0;
            uint8_t xrsiz = 0;
            uint8_t yrsiz = 0;
            if (!file->ReadReverse(&precision) || !file->ReadReverse(&xrsiz) || !file->ReadReverse(&yrsiz) ||
                (precision & 0x7F) > 37 || xrsiz == 0 || yrsiz == 0)
                return false;
            if (xrsiz != 1 || yrsiz != 1)
                params->position_order_supported = false;
        }
        return true;
    }

    static bool ReadCODMarker(File *file, uint64_t limit,
                              CodingParameters *params) {
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
        if (lcod != expected_length || (cs_buf & 0xF8) != 0 || progression > 4 || quality_layers == 0 ||
            mct > 1 || transform_levels > 32 || cb_width > 8 || cb_height > 8 ||
            cb_width + cb_height > 8 || cb_style > 63 || transform > 1 ||
            (progression >= CodingParameters::PCRL_PROGRESSION && !params->position_order_supported))
            return false;

        params->progression = progression;
        params->num_layers = quality_layers;
        params->num_levels = transform_levels;
        int height, width;
        uint8_t size_precinct;
        params->resolutions.clear();
        for (int i = 0; i <= params->num_levels; ++i) {
            if (cs_buf & 1) {
                if (!file->ReadReverse(&size_precinct))
                    return false;
                if (i > 0 && ((size_precinct & 0x0F) == 0 ||
                              (size_precinct & 0xF0) == 0))
                    return false;

                height = 1 << ((size_precinct & 0xF0) >> 4);
                width = 1 << (size_precinct & 0x0F);
                params->resolutions.emplace_back(width, height);
            } else {
                height = width = 1 << 15;
                params->resolutions.insert(params->resolutions.begin(),
                                           CodingParameters::Resolution(width, height));
            }
        }
        return true;
    }

    static bool ReadSOTMarker(File *file, uint64_t limit,
                              FileSegment &header,
                              vector<FileSegment> &packet_data,
                              uint8_t *declared_tile_parts) {
        uint64_t marker_offset = file->GetOffset() - 2;
        uint16_t lsot = 0;
        uint16_t isot = 0;
        uint32_t psot = 0;
        uint8_t tpsot = 0;
        uint8_t tnsot = 0;
        if (!file->ReadReverse(&lsot) || !file->ReadReverse(&isot) || !file->ReadReverse(&psot) ||
            !file->ReadReverse(&tpsot) || !file->ReadReverse(&tnsot) || lsot != 10 || isot != 0 ||
            tpsot != packet_data.size() || (tnsot != 0 && tpsot >= tnsot) ||
            (tnsot != 0 && *declared_tile_parts != 0 &&
             tnsot != *declared_tile_parts) ||
            (psot != 0 && psot < 14) ||
            (psot != 0 && psot > limit - marker_offset) ||
            packet_data.size() >= PacketIndex::MAX_SEGMENTS)
            return false;

        if (tnsot != 0 && *declared_tile_parts == 0)
            *declared_tile_parts = tnsot;

        if (header.length == 0)
            header.length = marker_offset - header.offset;
        packet_data.emplace_back(file->GetOffset(), psot == 0 ? 0 : psot - 12);
        return true;
    }

    static bool ReadPLTMarker(File *file, uint64_t limit,
                              const vector<FileSegment> &packet_data,
                              vector<FileSegment> &plt) {
        if (!packet_data.empty() && packet_data.back().length != 0)
            limit = packet_data.back().offset + packet_data.back().length;

        // Get Lplt
        uint16_t lplt = 0;
        if (file->GetOffset() > limit || !file->ReadReverse(&lplt) || lplt < 4 ||
            static_cast<uint64_t>(lplt) - 2 > limit - file->GetOffset())
            return false;

        // PLT marker length = Lplt - 3 (2 bytes Lplt and 1 byte iplt)
        plt.emplace_back(file->GetOffset() + 1, lplt - 3);
        file->Seek(lplt - 2, SEEK_CUR);
        return true;
    }

    static bool ReadSODMarker(File *file, uint64_t limit,
                              vector<FileSegment> &packet_data) {
        if (packet_data.empty())
            return false;

        FileSegment &fs = packet_data.back();
        if (fs.length == 0) {
            fs.offset = file->GetOffset();
            bool marker_prefix = false;
            uint8_t value = 0;
            while (file->GetOffset() < limit && file->Read(&value)) {
                if (marker_prefix && value == (EOC_MARKER & 0xFF)) {
                    fs.length = file->GetOffset() - 2 - fs.offset;
                    file->Seek(file->GetOffset() - 2);
                    return true;
                }
                if (marker_prefix && value == (SOT_MARKER & 0xFF))
                    return false;
                marker_prefix = value == 0xFF;
            }
            return false;
        }

        uint64_t header_length = file->GetOffset() - fs.offset;
        if (header_length > fs.length)
            return false;
        fs.length -= header_length;
        fs.offset = file->GetOffset();
        file->Seek(fs.length, SEEK_CUR);
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
            *length_box = limit - file->GetOffset();
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
        size_t num_codestreams = 0;
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
                    num_codestreams++;
                    if (length_box != 0)
                        containers.push_back({type_box, box_end});
                    break;
                case JP2C_BOX_ID: {
                    TRACE("JP2C box...");
                    if (num_codestreams == 0 || codestreams.size() >= num_codestreams)
                        return false;
                    codestreams.emplace_back(image_index->path_name);
                    ImageIndex::Codestream &codestream = codestreams.back();
                    if (!ReadCodestream(file, length_box, codestream))
                        return false;
                    if (!codestream.parameters.FillPrecinctCounts())
                        return false;
                    image_index->meta_data.bin0.emplace_back(
                            FileSegment(meta_start, prefix_length),
                            PlaceHolder(num_codestreams - 1, true,
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
                    num_flst = 0;
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
                            PlaceHolder(links.size(), true, ftbl_header));
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
                    references.push_back(std::move(path_file));
                    break;
                }
                default:
                    file->Seek(length_box, SEEK_CUR);
            }
        }
        image_index->meta_data.tail =
                FileSegment(meta_start, file->GetOffset() - meta_start);

        if (containers.size() != 1 || num_codestreams == 0 ||
            references.size() != num_data_references)
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
        vector<ImageIndex::Codestream>().swap(codestreams);
        image_index->codestreams.reserve(paths.size());
        for (size_t i = 0; i < paths.size(); ++i) {
            if (paths[i].size() < 4 ||
                paths[i].compare(paths[i].size() - 4, 4, ".jp2") != 0) {
                ERROR("Unsupported linked codestream file '" << paths[i] << "'");
                return false;
            }
            ImageIndex linked_image(paths[i]);
            if (!ReadImage(paths[i], &linked_image))
                return false;

            if (linked_image.codestreams.empty() ||
                linked_image.codestreams.back().packet_data.empty())
                return false;
            ImageIndex::Codestream &codestream = linked_image.codestreams.back();
            const FileSegment &last_packet_data = codestream.packet_data.back();
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

        // Replace "./" with the root_dir_
        if (local_path.compare(0, 2, "./") == 0)
            local_path = root_dir_ + local_path.substr(2);

        // undo possible URI character substitutions
        char *unescaped = g_uri_unescape_string(local_path.c_str(), NULL);
        if (!unescaped)
            return false;
        *path_file = unescaped;
        g_free(unescaped);
        return true;
    }

}
