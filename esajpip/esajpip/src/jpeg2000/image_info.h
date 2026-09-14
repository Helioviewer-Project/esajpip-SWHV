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
        struct Link {
            string path_name;
            CodingParameters coding_parameters;
            CodestreamIndex codestream;
        };

        Metadata meta_data;                        ///< Meta-data information
        CodingParameters coding_parameters;        ///< Coding parameters
        vector<CodestreamIndex> codestreams;    ///< Codestreams information
        vector<Link> links;                     ///< Hyperlinked codestreams

        friend ostream &operator<<(ostream &out, const ImageInfo &info) {
            out << "Coding parameters: " << endl
                << "---------------------- " << endl
                << info.coding_parameters << endl << endl;
            if (!info.links.empty()) {
                for (size_t i = 0; i < info.links.size(); ++i) {
                    out << "Codestream index " << i + 1 << ":" << endl;
                    out << "------------------------" << endl;
                    out << "Path: " << info.links[i].path_name << endl;
                    out << info.links[i].codestream << endl << endl;
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
