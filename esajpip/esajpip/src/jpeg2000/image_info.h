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

        /**
         * Empty constructor.
         */
        ImageInfo() {
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

            return out;
        }

    };
}

#endif /* _JPEG2000_IMAGE_INFO_H_ */
