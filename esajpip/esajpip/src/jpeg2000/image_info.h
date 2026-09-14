#ifndef _JPEG2000_IMAGE_INFO_H_
#define _JPEG2000_IMAGE_INFO_H_

#include <ostream>
#include <string>
#include <vector>
#include "data/file.h"
#include "codestream_index.h"
#include "coding_parameters.h"
#include "meta_data.h"

namespace jpeg2000 {
    /**
     * Contains the indexing information of a JPEG2000 image.
     * This class can be serialized and printed.
     *
     * @see CodingParameters
     * @see CodestreamIndex
     * @see Metadata
     */
    class ImageInfo {
    public:
        Metadata meta_data;                        ///< Meta-data information
        vector<string> paths;                   ///< Paths of the hyperlinks (if any)
        CodingParameters coding_parameters;        ///< Coding parameters
        vector<CodestreamIndex> codestreams;    ///< Codestreams information
        vector<CodingParameters> coding_parameters_hyperlinks; ///< Coding parameters of the hyperlinks
        vector<Metadata> meta_data_hyperlinks;    ///< Meta-data of the hyperlinks

        /**
         * Empty constructor.
         */
        ImageInfo() {
        }

        /**
         * Copy constructor.
         */
        ImageInfo(const ImageInfo &info) {
            *this = info;
        }

        /**
         * Copy assignment.
         */
        const ImageInfo &operator=(const ImageInfo &info) {
            meta_data = info.meta_data;
            paths = info.paths;
            coding_parameters = info.coding_parameters;
            codestreams = info.codestreams;
            coding_parameters_hyperlinks = info.coding_parameters_hyperlinks;
            meta_data_hyperlinks = info.meta_data_hyperlinks;
            return *this;
        }

        friend ostream &operator<<(ostream &out, const ImageInfo &info) {
            out << "Coding parameters: " << endl
                << "---------------------- " << endl
                << info.coding_parameters << endl << endl;
            if (!info.paths.empty()) {
                for (size_t i = 0; i < info.paths.size(); ++i) {
                    out << "Codestream index " << i + 1 << ":" << endl;
                    out << "------------------------" << endl;
                    out << "Path: " << info.paths[i] << endl;
                    out << info.codestreams[i] << endl << endl;
                }
            } else {
                for (size_t i = 0; i < info.codestreams.size(); ++i) {
                    out << "Codestream index " << i << ":" << endl;
                    out << "------------------------" << endl << info.codestreams[i] << endl << endl;
                }
            }
            out << endl << "Meta-data: ";
            out << info.meta_data << endl << endl;
            out << endl << "Meta-data-hyperlinks: ";
            for (size_t i = 0; i < info.meta_data_hyperlinks.size(); ++i)
                out << info.meta_data_hyperlinks[i] << " ";

            return out;
        }

    };
}

#endif /* _JPEG2000_IMAGE_INFO_H_ */
