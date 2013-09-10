#ifndef _JPIP_REQUEST_H_
#define _JPIP_REQUEST_H_


#include <vector>
#include <string>
#include <iostream>
#include "jpip/woi.h"
#include "jpip/cache_model.h"
#include "http/request.h"
#include "jpeg2000/point.h"
#include "jpeg2000/coding_parameters.h"
#include <string.h>


namespace jpip
{

  using namespace std;
  using namespace jpeg2000;


  /**
   * Class derived from the HTTP <code>Request</code> class
   * that contains the required code for properly analyzing
   * a JPIP request, when this protocol is used over the HTTP.
   *
   * @see http::Request
   * @see CacheModel
   */
  class Request :public http::Request
  {
  public:
	/**
	 * Parses a cache model from an input stream.
	 * @param stream Input stream.
	 * @return The same input stream after the parsing.
	 */
    istream& ParseModel(istream& stream);

    /**
     * Gets a coded char from an input stream.
     * @param in Input stream.
     * @param c Reference to store the char.
     * @return The same input stream.
     */
    istream& GetCodedChar(istream& in, char& c);

    /**
     * Parses the parameters of a CGI HTTP request.
     * @param stream Input stream.
     */
    virtual void ParseParameters(istream& stream);

    /**
     * Parses one parameter of a CGI HTTP request.
     * @param stream Input stream.
     * @param param String to store the parameter name.
     * @param value String to store the parameter value.
     */
    virtual void ParseParameter(istream& stream, const string& param, string& value);

    /**
     * Union used to control the presence of the different
     * JPIP parameters in a request.
     */
    union ParametersMask {
      /**
       * Parameters mask.
       */
      struct
      {
        int fsiz    : 1;
        int roff    : 1;
        int rsiz    : 1;
        int metareq : 1;
        int len     : 1;
        int target  : 1;
        int cid     : 1;
        int cnew    : 1;
        int cclose  : 1;
        int model   : 1;
        int stream  : 1;
        int context : 1;
      } items;

      /**
       * Parameters mask as integer.
       */
      int value;

      /**
       * Initializes the mask to zero.
       */
      ParametersMask()
      {
        value = 0;
      }

      /**
       * Returns <code>true</code> if the mask
       * contains the parameters associated to
       * the WOI (fsiz, roff and rsiz).
       */
      bool HasWOI() const
      {
        return (value & 7);
      }

      /**
       * Sets the mask to zero.
       */
      void Clear()
      {
        value = 0;
      }
    };

    /**
     * Enumeration of the possible round directions
     * of a WOI for specifying the resolution levels.
     */
    enum RoundDirection {
      ROUNDUP,		///< Round-up
      ROUNDDOWN,	///< Round-down
      CLOSEST		///< Closest
    };

    Size woi_size;				///< WOI size
    Point woi_position;			///< WOI position
    int min_codestream;			///< Minimum codestream
    int max_codestream;			///< Maximum codestream
    int length_response;		///< Maximum response length
    ParametersMask mask;		///< Parameters mask
    Size resolution_size;		///< Size of the resolution level
    CacheModel cache_model;		///< Cache model

    /**
     * Round direction.
     */
    RoundDirection round_direction;


    /**
     * Empty constructor.
     */
    Request()
    {
    }

    /**
     * Obtains the resolution level and modifies the given WOI to adjust it
     * according to that level.
     * @param coding_parameters Associated coding parameters.
     * @param woi WOI to modify.
     */
    void GetResolution(const CodingParameters::Ptr& coding_parameters, WOI *woi) const
    {
    	Size res_image_size;

    	if(round_direction == Request::CLOSEST)
    		woi->resolution = coding_parameters->GetClosestResolution(resolution_size, &res_image_size);

    	else if(round_direction == Request::ROUNDUP)
    		woi->resolution = coding_parameters->GetRoundUpResolution(resolution_size, &res_image_size);

    	else if(round_direction == Request::ROUNDDOWN)
    		woi->resolution = coding_parameters->GetRoundDownResolution(resolution_size, &res_image_size);

    	if (resolution_size != res_image_size)
    	{
    		woi->position.x = ceil((woi->position.x * res_image_size.x) / resolution_size.x);
    		woi->position.y = ceil((woi->position.y * res_image_size.y) / resolution_size.y);
    		woi->size.x = ceil((woi->size.x * res_image_size.x) / resolution_size.x);
    		woi->size.y = ceil((woi->size.y * res_image_size.y) / resolution_size.y);
    	}
    }

    virtual ~Request()
    {
    }
  };

}

#endif /* _JPIP_REQUEST_H_ */
