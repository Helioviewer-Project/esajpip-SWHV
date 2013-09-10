#ifndef _JPEG2000_IMAGE_INFO_H_
#define _JPEG2000_IMAGE_INFO_H_


#include <map>
#include "base.h"
#include "data/file.h"
#include "codestream_index.h"
#include "coding_parameters.h"
#include "meta_data.h"


namespace jpeg2000
{
  using namespace std::tr1;


  /**
   * Contains the indexing information of a JPEG2000 image.
   * This class can be serialized and printed.
   *
   * @see CodingParameters
   * @see CodestreamIndex
   * @see Metadata
   */
  class ImageInfo
  {
  public:
	Metadata meta_data;						///< Meta-data information
    multimap<string, int> paths;			///< Paths of the hyperlinks (if any)
    CodingParameters coding_parameters;		///< Coding parameters
    vector<CodestreamIndex> codestreams;	///< Codestreams information
    vector<Metadata> meta_data_hyperlinks;	///< Meta-data of the hyperlinks

    /**
     * Empty constructor.
     */
    ImageInfo()
    {
    }

    /**
     * Copy constructor.
     */
    ImageInfo(const ImageInfo& info)
    {
      *this = info;
    }

    /**
     * Copy assignment.
     */
    const ImageInfo& operator=(const ImageInfo& info)
    {
      base::copy(paths, info.paths);
      coding_parameters = info.coding_parameters;
      base::copy(codestreams, info.codestreams);
      meta_data=info.meta_data;
      base::copy(meta_data_hyperlinks, info.meta_data_hyperlinks);

      return *this;
    }

    template<typename T> T& SerializeWith(T& stream)
    {
      return (stream & paths & coding_parameters & codestreams & meta_data & meta_data_hyperlinks);
    }

    friend ostream& operator <<(ostream &out, const ImageInfo &info)
    {
      out << "Coding parameters: " << endl
    		  << "---------------------- " << endl
    		  << info.coding_parameters << endl << endl;

      if (info.paths.size() > 0)
      {
        for (multimap<string, int>::const_iterator i = info.paths.begin(); i != info.paths.end(); i++)
        {
          out << "Codestream index " << (*i).second + 1 << ":" << endl;
          out << "------------------------" << endl;
          out << "Path: " << (*i).first << endl;
          out << info.codestreams[(*i).second] << endl << endl;
        }
      }
      else
      {
        int ind = 0;
        for (vector<CodestreamIndex>::const_iterator i = info.codestreams.begin(); i != info.codestreams.end(); i++, ind++)
        {
          out << "Codestream index " << ind << ":" << endl;
          out << "------------------------" << endl << *i << endl << endl;
        }
      }

      out << endl << "Meta-data: ";
      out << info.meta_data << endl << endl;

      out << endl << "Meta-data-hyperlinks: ";
      for(vector<Metadata>::const_iterator i = info.meta_data_hyperlinks.begin(); i != info.meta_data_hyperlinks.end(); i++)
    	  out << *i << " ";

      return out;
    }

    virtual ~ImageInfo()
    {
    }
  };

}

#endif /* _JPEG2000_IMAGE_INFO_H_ */
