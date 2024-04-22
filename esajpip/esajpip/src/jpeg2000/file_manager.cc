#include "file_manager.h"

namespace jpeg2000 {

    using namespace std;
    using namespace data;

    bool FileManager::OpenImage(string &path_image_file) {
        if (path_image_file[0] == '/') path_image_file = path_image_file.substr(1, path_image_file.size() - 1);
        path_image_file = root_dir_ + path_image_file;

        // Get image info
        ImageInfo image_info;
        if (!ReadImage(path_image_file, &image_info)) {
            ERROR("The image file '" << path_image_file << "' can not be read");
            return false;
        }
        coding_parameters = image_info.coding_parameters;

        image = ImageIndex::Ptr(new ImageIndex());
        image->Init(path_image_file, image_info);

        // Repeat the process with the image hyperlinks
        if (!image_info.paths.empty()) {
            image->hyper_links.resize(image_info.paths.size());
            for (multimap<string, int>::const_iterator i = image_info.paths.begin(); i != image_info.paths.end(); ++i) {
                ImageIndex::Ptr linked = ImageIndex::Ptr(new ImageIndex());
                linked->Init(i->first, image_info, i->second);
                image->hyper_links[i->second] = linked;
            }
        }
        return true;
    }

#define EOC_MARKER 0xFFD9
#define SOC_MARKER 0xFF4F
#define SIZ_MARKER 0xFF51
#define COD_MARKER 0xFF52
#define SOT_MARKER 0xFF90
#define PLT_MARKER 0xFF58
#define SOD_MARKER 0xFF93

#define JP2C_BOX_ID 0x6A703263
#define XML__BOX_ID 0x786D6C20
#define ASOC_BOX_ID 0x61736F63
#define NLST_BOX_ID 0x6E6C7374
#define JPCH_BOX_ID 0x6A706368
#define FTBL_BOX_ID 0x6674626C
#define DBTL_BOX_ID 0x6474626C
#define URL__BOX_ID 0x75726C20
#define FLST_BOX_ID 0x666C7374

    bool FileManager::ReadImage(const string &name_image_file, ImageInfo *image_info) {
        bool res = true;
        // Get file extension
        string extension;
        size_t pos = name_image_file.find_last_of(".");
        if (pos != string::npos) extension = name_image_file.substr(pos);

        if (extension == ".jp2") { // JP2 image
            File::Ptr file = GetFile(name_image_file);
            if (file == NULL) {
                ERROR("Unable to open file: '" << name_image_file << "'...");
                return false;
            }
            res = res && ReadJP2(file, image_info);
        } else if (extension == ".jpx") { // JPX image
            File::Ptr file = GetFile(name_image_file);
            if (file == NULL) {
                ERROR("Unable to open file: '" << name_image_file << "'...");
                return false;
            }
            res = res && ReadJPX(file, image_info);
        } else {
            ERROR("File type not supported...");
            return false;
        }

        if (res)
            image_info->coding_parameters.FillTotalPrecinctsVector();

        return res;
    }

    bool FileManager::ReadCodestream(File::Ptr &file, CodingParameters *params, CodestreamIndex *index) {
        bool res = true;

        // Get markers
        bool plts = false;
        uint16_t value = 0;

        while (res = res && file->ReadReverse(&value), res && (value != EOC_MARKER)) {
            switch (value) {
                case SOC_MARKER: TRACE("SOC marker...");
                    index->header.offset = file->GetOffset() - 2;
                    break;

                case SIZ_MARKER: TRACE("SIZ marker...");
                    res = res && ReadSIZMarker(file, params);
                    break;

                case COD_MARKER: TRACE("COD marker...");
                    res = res && ReadCODMarker(file, params);
                    break;

                case SOT_MARKER: TRACE("SOT marker...");
                    res = res && ReadSOTMarker(file, index);
                    break;

                case PLT_MARKER: TRACE("PLT marker...");
                    res = res && ReadPLTMarker(file, index);
                    if (res) plts = true;
                    break;

                case SOD_MARKER: TRACE("SOD marker...");
                    res = res && ReadSODMarker(file, index);
                    break;

                default:
                    res = res && file->ReadReverse(&value) && file->Seek(value - 2, SEEK_CUR);
            }
        }

        // Check if the image has been read in a right way
        if (value == EOC_MARKER) {
            // Check if the image has PLT marker
            if (plts) return true;
            else {
                ERROR("The code-stream does not include any PLT marker");
                return false;
            }
        } else {
            ERROR("The code-stream does not end with an EOC marker");
            return false;
        }
    }

    bool FileManager::ReadSIZMarker(File::Ptr &file, CodingParameters *params) {
        bool res = true;
        // To jump Lsiz, CA
        res = res && file->Seek(4, SEEK_CUR);
        // Get image height and width
        uint32_t FE[4];
        for (int i = 0; i < 4; ++i)
            res = res && file->ReadReverse(&FE[i]);
        // height=F1-E1
        // width=F2-E2
        params->size = Size(FE[0] - FE[2], FE[1] - FE[3]);
        // Get T2, T1, omegaT2, omegaT1
        uint32_t tiling[4];
        for (int i = 0; i < 4; ++i)
            res = res && file->ReadReverse(&tiling[i]);
        // Get number of components
        uint16_t num_components = 0;
        res = res && file->ReadReverse(&num_components);
        params->num_components = num_components;
        // To jump to the end of the marker
        res = res && file->Seek(3 * num_components, SEEK_CUR);

        return res;
    }

    bool FileManager::ReadCODMarker(File::Ptr &file, CodingParameters *params) {
        bool res = true;
        // Get CS0 parameter
        uint8_t cs_buf = 0;
        res = res && file->Seek(2, SEEK_CUR) && file->ReadReverse(&cs_buf);
        // Get progression order
        uint8_t progression = 0;
        res = res && file->ReadReverse(&progression);
        params->progression = progression;
        // Get number of quality layers
        uint16_t quality_layers = 0;
        // To jump MC
        res = res && file->ReadReverse(&quality_layers) && file->Seek(1, SEEK_CUR);
        params->num_layers = quality_layers;
        // Get transform levels
        uint8_t transform_levels = 0;
        // To jump 4 bytes (ECB2,ECB1,MS,WT)
        res = res && file->ReadReverse(&transform_levels) && file->Seek(4, SEEK_CUR);
        params->num_levels = transform_levels;
        // Get precint sizes for each resolution
        int height, width;
        uint8_t size_precinct;
        params->precinct_size.clear();
        for (int i = 0; i <= params->num_levels; ++i) {
            if (cs_buf & 1) {
                res = res && file->ReadReverse(&size_precinct);
                if (!res)
                    break;

                height = 1 << ((size_precinct & 0xF0) >> 4);
                width = 1 << (size_precinct & 0x0F);
                params->precinct_size.emplace_back(width, height);
            } else {
                height = (int) ceil((double) params->size.y / (1L << i));
                width = (int) ceil((double) params->size.x / (1L << i));
                params->precinct_size.insert(params->precinct_size.begin(), Size(width, height));
            }
        }
        return res;
    }

    bool FileManager::ReadSOTMarker(File::Ptr &file, CodestreamIndex *index) {
        bool res = true;
        // Get offset of the codestream header
        if (index->header.length == 0) index->header.length = file->GetOffset() - 2 - index->header.offset;
        // Get Ltp
        uint32_t ltp = 0;
        // To jump Lsot, it, itp, ntp
        res = res && file->Seek(4, SEEK_CUR) && file->ReadReverse(&ltp) && file->Seek(2, SEEK_CUR);
        index->packets.emplace_back(file->GetOffset(), ltp - 12);
        return res;
    }

    bool FileManager::ReadPLTMarker(File::Ptr &file, CodestreamIndex *index) {
        bool res = true;
        // Get PLT offset
        uint64_t PLT_offset = file->GetOffset() + 3;
        // Get Lplt
        uint16_t lplt = 0;
        res = res && file->ReadReverse(&lplt);
        res = res && file->Seek(lplt - 2, SEEK_CUR);
        // PLT marker length = Lplt - 3 (2 bytes Lplt and 1 byte iplt)
        index->PLT_markers.emplace_back(PLT_offset, lplt - 3);
        return res;
    }

    bool FileManager::ReadSODMarker(File::Ptr &file, CodestreamIndex *index) {
        bool res = true;
        // Get packets info
        FileSegment &fs = index->packets.back();
        fs.length = fs.length - (file->GetOffset() - fs.offset);
        fs.offset = file->GetOffset();
        res = res && file->Seek(fs.length, SEEK_CUR);
        return res;
    }

    bool FileManager::ReadBoxHeader(File::Ptr &file, uint32_t *type_box, uint64_t *length_box) {
        bool res = true;
        // Get L, if it is not 0 or 1, then box length is L
        uint32_t L = 0;
        res = res && file->ReadReverse(&L);
        *length_box = L - 8;
        // Get T (box type)
        uint32_t T = 0;
        res = res && file->ReadReverse(&T);
        *type_box = T;
        // XL indicates the box length
        if (L == 1) {
            uint64_t XL = 0;
            res = res && file->ReadReverse(&XL);
            *length_box = XL - 16;
        }
            // Box length = eof_offset - offset
        else if (L == 0) {
            *length_box = file->GetSize() - file->GetOffset();
        }
        return res;
    }

    bool FileManager::ReadJP2(File::Ptr &file, ImageInfo *image_info) {
        bool res = true;
        // Get boxes
        uint32_t type_box;
        uint64_t length_box;
        int pini = 0, plen = 0, pini_box = 0, plen_box = 0;
        //int metadata_bin=1;

        image_info->codestreams.emplace_back();
        while (file->GetOffset() != file->GetSize() && res) {
            pini_box = file->GetOffset();
            plen = pini_box - pini;
            res = res && ReadBoxHeader(file, &type_box, &length_box);
            plen_box = file->GetOffset() - pini_box;
            switch (type_box) {
                case JP2C_BOX_ID: TRACE("JP2C box...");
                    image_info->meta_data.meta_data.emplace_back(pini, plen);
                    res = res && ReadCodestream(file, &image_info->coding_parameters, &image_info->codestreams.back());
                    image_info->meta_data.place_holders.emplace_back(image_info->codestreams.size() - 1, true, FileSegment(pini_box, plen_box), length_box);
                    pini = file->GetOffset();
                    plen = 0;
                    break;

                    /*case XML__BOX_ID:
                      TRACE("XML box...");
                      image_info->meta_data.meta_data.push_back(FileSegment(pini,plen));
                      // Get meta_data info
                      res = res && file->Seek(length_box, SEEK_CUR);
                      image_info->meta_data.place_holders.push_back(PlaceHolder(metadata_bin, false, FileSegment(pini_box, plen_box), length_box));
                      metadata_bin++;
                      pini=file->GetOffset();
                      plen=0;
                      break;*/

                default:
                    res = res && file->Seek(length_box, SEEK_CUR);
            }
        }
        image_info->meta_data.meta_data.emplace_back(pini, file->GetOffset() - pini);
        return res;
    }

    bool FileManager::ReadJPX(File::Ptr &file, ImageInfo *image_info) {
        bool res = true;
        // Get boxes
        uint32_t type_box;
        uint64_t length_box;
        string path_file;
        uint16_t data_reference;
        vector<uint16_t> v_data_reference;
        vector<string> v_path_file;
        int pini = 0, plen = 0, pini_box = 0, plen_box = 0;
        //int metadata_bin=1;
        int num_flst = 0, pini_ftbl = 0, plen_ftbl = 0;

        while (file->GetOffset() != file->GetSize() && res) {
            pini_box = file->GetOffset();
            plen = pini_box - pini;
            res = res && ReadBoxHeader(file, &type_box, &length_box);
            plen_box = file->GetOffset() - pini_box;
            switch (type_box) {
                case JPCH_BOX_ID: TRACE("JPCH box...");
                    image_info->codestreams.emplace_back();
                    break;
                case JP2C_BOX_ID: TRACE("JP2C box...");
                    image_info->meta_data.meta_data.emplace_back(pini, plen);
                    res = res && ReadCodestream(file, &image_info->coding_parameters, &image_info->codestreams.back());
                    image_info->meta_data.place_holders.emplace_back(image_info->codestreams.size() - 1, true, FileSegment(pini_box, plen_box), length_box);
                    pini = file->GetOffset();
                    plen = 0;
                    break;
                    /*case ASOC_BOX_ID:
                      TRACE("ASOC box...");
                      image_info->meta_data.meta_data.push_back(FileSegment(pini,plen));
                      res = res && file->Seek(length_box, SEEK_CUR);
                      image_info->meta_data.place_holders.push_back(PlaceHolder(metadata_bin, false, FileSegment(pini_box, plen_box), length_box));
                      metadata_bin++;
                      pini=file->GetOffset();
                      plen=0;
                      break;
                    case XML__BOX_ID:
                      TRACE("XML box...");
                    image_info->meta_data.meta_data.push_back(FileSegment(pini,plen));
                    res = res && file->Seek(length_box, SEEK_CUR);
                    image_info->meta_data.place_holders.push_back(PlaceHolder(metadata_bin, false, FileSegment(pini_box, plen_box), length_box));
                    metadata_bin++;
                    pini=file->GetOffset();
                    plen=0;
                    break;*/
                    // 'ftbl' superbox contains a 'flst'
                case FTBL_BOX_ID: TRACE("FTBL box...");
                    image_info->meta_data.meta_data.emplace_back(pini, plen);
                    num_flst = 0;
                    pini_ftbl = pini_box;
                    plen_ftbl = plen_box;
                    break;
                    // 'flst' box assumed to be contained within a 'ftbl' superbox
                case FLST_BOX_ID: TRACE("FLST box...");
                    res = res && ReadFlstBox(file, length_box, &data_reference);
                    if (num_flst)
                        image_info->meta_data.meta_data.emplace_back(0, 0);
                    num_flst++;
                    image_info->meta_data.place_holders.emplace_back(v_data_reference.size(), true, FileSegment(pini_ftbl, plen_ftbl), 0);
                    v_data_reference.push_back(data_reference);
                    pini = file->GetOffset();
                    plen = 0;
                    break;
                case DBTL_BOX_ID: TRACE("DBTL box...");
                    res = res && file->Seek(2, SEEK_CUR);
                    break;
                case URL__BOX_ID: TRACE("URL box...");
                    // Add the paths of the hyperlinked images to the paths vector
                    res = res && ReadUrlBox(file, length_box, &path_file);
                    //path_file=root_dir_ + path_file; /// OJOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO
                    v_path_file.push_back(path_file);
                    break;
                default:
                    res = res && file->Seek(length_box, SEEK_CUR);
            }
        }
        image_info->meta_data.meta_data.emplace_back(pini, file->GetOffset() - pini);

        assert(v_data_reference.size() == v_path_file.size());
        for (size_t i = 0; i < v_data_reference.size(); ++i) {
            image_info->paths.insert(pair<string, int>(v_path_file[i], i));
        }

        if (!image_info->paths.empty()) {
            image_info->codestreams.resize(image_info->paths.size());
            image_info->meta_data_hyperlinks.resize(image_info->paths.size());
        }
        // Get image info of the hyperlinked images
        for (multimap<string, int>::const_iterator i = image_info->paths.begin(); i != image_info->paths.end() && res; ++i) {
            ImageInfo image_info_hyperlink;
            res = res && ReadImage(i->first, &image_info_hyperlink);
            if (!res)
                break;

            image_info->coding_parameters = image_info_hyperlink.coding_parameters;
            image_info->codestreams[i->second] = image_info_hyperlink.codestreams.back();
            image_info->meta_data_hyperlinks[i->second] = image_info_hyperlink.meta_data;
        }
        return res;
    }

    bool FileManager::ReadNlstBox(File::Ptr &file, int *num_codestream, int length_box) {
        bool res = true;
        // Get the codestream number
        uint32_t an;
        while (res && (length_box > 0)) {
            res = res && file->ReadReverse(&an);
            if ((an >> 24) == 1) {
                *num_codestream = an & 0x00FFFFFF;
            }
            length_box -= 4;
        }
        return res;
    }

    bool FileManager::ReadFlstBox(File::Ptr &file, uint64_t length_box, uint16_t *data_reference) {
        bool res = true;
        // Get the path of the hyperlinked image
        res = res && file->Seek(14, SEEK_CUR);
        res = res && file->ReadReverse(data_reference);

        return res;
    }

    bool FileManager::ReadUrlBox(File::Ptr &file, uint64_t length_box, string *path_file) {
        bool res = true;
        // Get the path of the hyperlinked image
        res = res && file->Seek(4, SEEK_CUR);

        char path_char[length_box - 3];
        res = res && file->Read(path_char, length_box - 4);
        path_char[length_box - 4] = 0;

        *path_file = path_char;
        size_t found = path_file->find("file://") + 7;
        *path_file = path_file->substr(found);
        //*path_file = path_file->substr(found + 2);
        // Replace "./" with the root_dir_
        size_t pos = path_file->find("./");
        if (pos != string::npos) *path_file = path_file->substr(0, pos) + root_dir_ + path_file->substr(pos + 2);

        path_file->replace(path_file->find("%23"), sizeof("%23") - 1, "#");

        return res;
    }

}
