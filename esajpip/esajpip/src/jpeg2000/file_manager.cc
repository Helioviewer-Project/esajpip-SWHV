#include "file_manager.h"

#include <climits>
#include <utility>

#include <glib.h>

namespace jpeg2000 {

    using namespace std;
    using namespace data;

    bool FileManager::OpenImage(string &path_image_file) {
        if (path_image_file.empty())
            return false;
        if (path_image_file[0] == '/') path_image_file = path_image_file.substr(1, path_image_file.size() - 1);
        path_image_file = root_dir_ + path_image_file;

        unique_ptr<ImageIndex> image_index(new ImageIndex(path_image_file));
        if (!ReadImage(path_image_file, image_index.get())) {
            ERROR("The image file '" << path_image_file << "' can not be read");
            ClearFiles();
            return false;
        }
        image = std::move(image_index);
        ClearFiles();
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
#define ASOC_BOX_ID 0x61736F63
#define NLST_BOX_ID 0x6E6C7374
#define JPCH_BOX_ID 0x6A706368
#define FTBL_BOX_ID 0x6674626C
#define DBTL_BOX_ID 0x6474626C
#define URL__BOX_ID 0x75726C20
#define FLST_BOX_ID 0x666C7374

    static bool SkipMarker(File *file, uint64_t limit) {
        uint16_t length = 0;
        if (file->GetOffset() > limit || !file->ReadReverse(&length) || length < 2 ||
            length - 2 > limit - file->GetOffset())
            return false;
        file->Seek(length - 2, SEEK_CUR);
        return true;
    }

    bool FileManager::ReadImage(const string &name_image_file, ImageIndex *image_index) {
        bool res = true;
        // Get file extension
        string extension;
        size_t pos = name_image_file.find_last_of(".");
        if (pos != string::npos) extension = name_image_file.substr(pos);

        if (extension == ".jp2") { // JP2 image
            File *file = GetFile(name_image_file);
            if (!file) {
                ERROR("Unable to open file: '" << name_image_file << "'...");
                return false;
            }
            res = res && ReadJP2(file, image_index);
        } else if (extension == ".jpx") { // JPX image
            File *file = GetFile(name_image_file);
            if (!file) {
                ERROR("Unable to open file: '" << name_image_file << "'...");
                return false;
            }
            res = res && ReadJPX(file, image_index);
        } else {
            ERROR("File type not supported...");
            return false;
        }

        if (res && image_index->hyper_links.empty())
            image_index->coding_parameters.FillPrecinctCounts();

        return res;
    }

    bool FileManager::ReadCodestream(File *file, uint64_t length, CodingParameters *params, CodestreamIndex *index) {
        if (length < 4 || length > file->GetSize() - file->GetOffset())
            return false;

        uint64_t limit = file->GetOffset() + length;
        bool plts = false;
        bool tile_header = false;
        bool siz = false;
        bool cod = false;
        bool qcd = false;
        bool first_marker = true;
        uint16_t value = 0;

        if (!file->ReadReverse(&value) || value != SOC_MARKER)
            return false;
        index->header.offset = file->GetOffset() - 2;

        while (file->GetOffset() < limit) {
            if (!file->ReadReverse(&value))
                break;
            if ((value & 0xFF00) != 0xFF00 || value == 0xFFFF ||
                (first_marker && value != SIZ_MARKER))
                return false;
            first_marker = false;
            uint64_t marker_limit = limit;
            if (tile_header && !index->packets.empty() && index->packets.back().length != 0)
                marker_limit = index->packets.back().offset + index->packets.back().length;
            switch (value) {
                case SIZ_MARKER: TRACE("SIZ marker...");
                    if (tile_header || siz || !ReadSIZMarker(file, limit, params))
                        return false;
                    siz = true;
                    break;

                case COD_MARKER: TRACE("COD marker...");
                    if (!ReadCODMarker(file, marker_limit, params))
                        return false;
                    cod = true;
                    break;

                case SOT_MARKER: TRACE("SOT marker...");
                    if (tile_header || !siz || !cod || !qcd || !ReadSOTMarker(file, limit, index))
                        return false;
                    tile_header = true;
                    break;

                case QCD_MARKER:
                    if (!tile_header)
                        qcd = true;
                    if (!SkipMarker(file, marker_limit))
                        return false;
                    break;

                case PLT_MARKER: TRACE("PLT marker...");
                    if (!tile_header || !ReadPLTMarker(file, limit, index))
                        return false;
                    plts = true;
                    break;

                case SOD_MARKER: TRACE("SOD marker...");
                    if (!tile_header || !ReadSODMarker(file, limit, index))
                        return false;
                    tile_header = false;
                    break;

                case EOC_MARKER:
                    if (tile_header || index->packets.empty() || file->GetOffset() != limit)
                        return false;
                    if (plts)
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

    bool FileManager::ReadSIZMarker(File *file, uint64_t limit, CodingParameters *params) {
        uint16_t lsiz = 0;
        if (file->GetOffset() > limit || !file->ReadReverse(&lsiz) || lsiz < 41 ||
            lsiz - 2 > limit - file->GetOffset())
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
            image[0] - image[2] > INT_MAX || image[1] - image[3] > INT_MAX ||
            tiling[0] == 0 || tiling[1] == 0)
            return false;

        params->size = Size(image[0] - image[2], image[1] - image[3]);
        params->num_components = num_components;
        for (uint16_t i = 0; i < num_components; ++i) {
            uint8_t precision = 0;
            uint8_t xrsiz = 0;
            uint8_t yrsiz = 0;
            if (!file->ReadReverse(&precision) || !file->ReadReverse(&xrsiz) || !file->ReadReverse(&yrsiz) ||
                (precision & 0x7F) > 37 || xrsiz == 0 || yrsiz == 0)
                return false;
        }
        return true;
    }

    bool FileManager::ReadCODMarker(File *file, uint64_t limit, CodingParameters *params) {
        uint16_t lcod = 0;
        uint8_t cs_buf = 0;
        uint8_t progression = 0;
        uint16_t quality_layers = 0;
        uint8_t mct = 0;
        uint8_t transform_levels = 0;
        uint8_t cb_width = 0;
        uint8_t cb_height = 0;
        uint8_t transform = 0;
        if (file->GetOffset() > limit || !file->ReadReverse(&lcod) || lcod < 12 ||
            lcod - 2 > limit - file->GetOffset() ||
            !file->ReadReverse(&cs_buf) || !file->ReadReverse(&progression) ||
            !file->ReadReverse(&quality_layers) || !file->ReadReverse(&mct) ||
            !file->ReadReverse(&transform_levels) || !file->ReadReverse(&cb_width) ||
            !file->ReadReverse(&cb_height) || !file->Seek(1, SEEK_CUR) ||
            !file->ReadReverse(&transform))
            return false;

        uint16_t expected_length = 12 + ((cs_buf & 1) ? transform_levels + 1 : 0);
        if (lcod != expected_length || (cs_buf & 0xF8) != 0 || progression > 4 || quality_layers == 0 ||
            mct > 1 || transform_levels > 32 || cb_width > 8 || cb_height > 8 ||
            cb_width + cb_height > 8 || transform > 1)
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

                height = 1 << ((size_precinct & 0xF0) >> 4);
                width = 1 << (size_precinct & 0x0F);
                params->resolutions.emplace_back(width, height);
            } else {
                height = (int) ceil((double) params->size.y / (1L << i));
                width = (int) ceil((double) params->size.x / (1L << i));
                params->resolutions.insert(params->resolutions.begin(),
                                           CodingParameters::Resolution(width, height));
            }
        }
        return true;
    }

    bool FileManager::ReadSOTMarker(File *file, uint64_t limit, CodestreamIndex *index) {
        uint64_t marker_offset = file->GetOffset() - 2;
        uint16_t lsot = 0;
        uint16_t isot = 0;
        uint32_t psot = 0;
        uint8_t tpsot = 0;
        uint8_t tnsot = 0;
        if (!file->ReadReverse(&lsot) || !file->ReadReverse(&isot) || !file->ReadReverse(&psot) ||
            !file->ReadReverse(&tpsot) || !file->ReadReverse(&tnsot) || lsot != 10 || isot == UINT16_MAX ||
            tpsot == UINT8_MAX || (tnsot != 0 && tpsot >= tnsot) || (psot != 0 && psot < 14) ||
            (psot != 0 && psot > limit - marker_offset))
            return false;

        if (index->header.length == 0)
            index->header.length = marker_offset - index->header.offset;
        index->packets.emplace_back(file->GetOffset(), psot == 0 ? 0 : psot - 12);
        return true;
    }

    bool FileManager::ReadPLTMarker(File *file, uint64_t limit, CodestreamIndex *index) {
        if (!index->packets.empty() && index->packets.back().length != 0)
            limit = index->packets.back().offset + index->packets.back().length;

        // Get Lplt
        uint16_t lplt = 0;
        if (file->GetOffset() > limit || !file->ReadReverse(&lplt) || lplt < 4 ||
            lplt - 2 > limit - file->GetOffset())
            return false;

        // PLT marker length = Lplt - 3 (2 bytes Lplt and 1 byte iplt)
        index->PLT_markers.emplace_back(file->GetOffset() + 1, lplt - 3);
        file->Seek(lplt - 2, SEEK_CUR);
        return true;
    }

    bool FileManager::ReadSODMarker(File *file, uint64_t limit, CodestreamIndex *index) {
        if (index->packets.empty())
            return false;

        FileSegment &fs = index->packets.back();
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

    bool FileManager::ReadBoxHeader(File *file, uint64_t limit, uint32_t *type_box, uint64_t *length_box) {
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
        bool res = true;
        bool codestream = false;
        // Get boxes
        uint32_t type_box;
        uint64_t length_box;
        uint64_t pini = 0, plen = 0, pini_box = 0, plen_box = 0;
        //int metadata_bin=1;

        image_index->streams.emplace_back(CodestreamIndex());
        while (file->GetOffset() != file->GetSize() && res) {
            pini_box = file->GetOffset();
            plen = pini_box - pini;
            if (!ReadBoxHeader(file, file->GetSize(), &type_box, &length_box))
                return false;
            plen_box = file->GetOffset() - pini_box;
            switch (type_box) {
                case JP2C_BOX_ID: TRACE("JP2C box...");
                    if (codestream)
                        return false;
                    codestream = true;
                    res = res && ReadCodestream(file, length_box, &image_index->coding_parameters,
                                                &image_index->streams.back().codestream);
                    image_index->meta_data.bin0.emplace_back(
                            FileSegment(pini, plen),
                            PlaceHolder(image_index->streams.size() - 1, true,
                                        FileSegment(pini_box, plen_box), length_box));
                    pini = file->GetOffset();
                    break;

                default:
                    res = res && file->Seek(length_box, SEEK_CUR);
            }
        }
        image_index->meta_data.tail = FileSegment(pini, file->GetOffset() - pini);
        return res && codestream;
    }

    bool FileManager::ReadJPX(File *file, ImageIndex *image_index) {
        bool res = true;
        // Get boxes
        uint32_t type_box;
        uint64_t length_box;
        string path_file;
        uint16_t data_reference;
        FileSegment fragment;
        vector<uint16_t> v_data_reference;
        vector<FileSegment> fragments;
        vector<string> v_path_file;
        uint64_t pini = 0, plen = 0, pini_box = 0, plen_box = 0;
        uint64_t pini_ftbl = 0, plen_ftbl = 0;
        FileSegment ftbl_metadata;
        int num_flst = 0;
        uint16_t num_data_references = 0;
        bool has_data_reference_box = false;
        vector<CodestreamIndex> codestreams;
        vector<pair<uint32_t, uint64_t>> containers;
        containers.emplace_back(0, file->GetSize());

        while (res) {
            while (containers.size() > 1 && file->GetOffset() == containers.back().second) {
                if (containers.back().first == FTBL_BOX_ID && num_flst != 1) {
                    res = false;
                    break;
                }
                containers.pop_back();
            }
            if (!res || file->GetOffset() == file->GetSize())
                break;
            if (file->GetOffset() > containers.back().second) {
                res = false;
                break;
            }

            pini_box = file->GetOffset();
            plen = pini_box - pini;
            res = ReadBoxHeader(file, containers.back().second, &type_box, &length_box);
            if (!res)
                break;
            plen_box = file->GetOffset() - pini_box;
            uint64_t box_end = file->GetOffset() + length_box;
            switch (type_box) {
                case JPCH_BOX_ID: TRACE("JPCH box...");
                    codestreams.emplace_back();
                    if (length_box != 0)
                        containers.emplace_back(type_box, box_end);
                    break;
                case JP2C_BOX_ID: TRACE("JP2C box...");
                    if (codestreams.empty()) {
                        res = false;
                        break;
                    }
                    res = res && ReadCodestream(file, length_box, &image_index->coding_parameters,
                                                &codestreams.back());
                    image_index->meta_data.bin0.emplace_back(
                            FileSegment(pini, plen),
                            PlaceHolder(codestreams.size() - 1, true,
                                        FileSegment(pini_box, plen_box), length_box));
                    pini = file->GetOffset();
                    break;
                case ASOC_BOX_ID: TRACE("ASOC box...");
                    res = res && file->Seek(length_box, SEEK_CUR);
                    image_index->meta_data.bins.emplace_back(pini_box + plen_box, length_box);
                    image_index->meta_data.bin0.emplace_back(
                            FileSegment(pini, plen),
                            PlaceHolder(image_index->meta_data.bins.size(), false,
                                        FileSegment(pini_box, plen_box), length_box));
                    pini = file->GetOffset();
                    break;
                    // 'ftbl' superbox contains a 'flst'
                case FTBL_BOX_ID: TRACE("FTBL box...");
                    num_flst = 0;
                    pini_ftbl = pini_box;
                    plen_ftbl = plen_box;
                    ftbl_metadata = FileSegment(pini, plen);
                    if (length_box < 8)
                        res = false;
                    else
                        containers.emplace_back(type_box, box_end);
                    break;
                    // 'flst' box assumed to be contained within a 'ftbl' superbox
                case FLST_BOX_ID: TRACE("FLST box...");
                    if (containers.back().first != FTBL_BOX_ID || num_flst != 0 ||
                        !ReadFlstBox(file, length_box, &fragment, &data_reference)) {
                        res = false;
                        break;
                    }
                    num_flst++;
                    image_index->meta_data.bin0.emplace_back(
                            ftbl_metadata,
                            PlaceHolder(v_data_reference.size(), true,
                                        FileSegment(pini_ftbl, plen_ftbl), 0));
                    v_data_reference.push_back(data_reference);
                    fragments.push_back(fragment);
                    pini = file->GetOffset();
                    break;
                case DBTL_BOX_ID: TRACE("DBTL box...");
                    if (containers.size() != 1 || has_data_reference_box || length_box < 2)
                        res = false;
                    else {
                        res = file->ReadReverse(&num_data_references);
                        has_data_reference_box = true;
                        if (res && file->GetOffset() != box_end)
                            containers.emplace_back(type_box, box_end);
                    }
                    break;
                case URL__BOX_ID: TRACE("URL box...");
                    // Add the paths of the hyperlinked images to the paths vector
                    res = containers.back().first == DBTL_BOX_ID &&
                          ReadUrlBox(file, length_box, &path_file);
                    //path_file=root_dir_ + path_file; /// OJOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO
                    if (res)
                        v_path_file.push_back(path_file);
                    break;
                default:
                    res = res && file->Seek(length_box, SEEK_CUR);
            }
        }
        image_index->meta_data.tail = FileSegment(pini, file->GetOffset() - pini);

        if (!res || containers.size() != 1 || codestreams.empty() ||
            v_path_file.size() != num_data_references)
            return false;

        bool sequential_references = v_data_reference.size() == v_path_file.size();
        for (size_t i = 0; i < v_data_reference.size(); ++i) {
            if (v_data_reference[i] == 0 || v_data_reference[i] > v_path_file.size())
                return false;
            if (v_data_reference[i] != i + 1)
                sequential_references = false;
        }
        vector<string> paths;
        if (sequential_references)
            paths = std::move(v_path_file);
        else {
            paths.reserve(v_data_reference.size());
            for (uint16_t reference : v_data_reference)
                paths.push_back(v_path_file[reference - 1]);
        }

        // Resolve the linked codestreams.
        if (paths.empty()) {
            image_index->streams.reserve(codestreams.size());
            for (CodestreamIndex &codestream : codestreams)
                image_index->streams.emplace_back(std::move(codestream));
            return true;
        }

        vector<CodestreamIndex>().swap(codestreams);
        image_index->hyper_links.reserve(paths.size());
        for (size_t i = 0; i < paths.size() && res; ++i) {
            ImageIndex linked_image(paths[i]);
            res = ReadImage(paths[i], &linked_image);
            file_map.erase(paths[i]);
            if (!res)
                break;

            if (linked_image.streams.empty() || linked_image.streams.back().codestream.packets.empty()) {
                res = false;
                break;
            }
            const CodestreamIndex &codestream = linked_image.streams.back().codestream;
            const FileSegment &last_packet_data = codestream.packets.back();
            uint64_t codestream_length =
                    last_packet_data.offset + last_packet_data.length + 2 - codestream.header.offset;
            if (fragments[i] != FileSegment(codestream.header.offset, codestream_length)) {
                res = false;
                break;
            }

            image_index->hyper_links.emplace_back(std::move(paths[i]),
                                                  std::move(linked_image.coding_parameters),
                                                  std::move(linked_image.streams.back().codestream));
        }
        return res;
    }

    bool FileManager::ReadFlstBox(File *file, uint64_t length_box, FileSegment *fragment, uint16_t *data_reference) {
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
