#ifndef _HTTP_RESPONSE_H_
#define _HTTP_RESPONSE_H_


#include <map>
#include <string>
#include <sstream>
#include <iostream>
#include "protocol.h"


namespace http
{

  using namespace std;


  /**
   * Class used to identify a HTTP response. It is possible to
   * use this class with standard streams.
   *
   * @see Request
   */
  class Response
  {
  private:
	/**
	 * Class used for the initializer.
	 */
    class StatusCodesInitializer
    {
    public:
      StatusCodesInitializer();
    };

    /**
     * The initializer of the <code>StatusCodes</code> member.
     */
    static StatusCodesInitializer statusCodesInitializer;

  public:
    int code;			///< Status code
    Protocol protocol;	///< Protocol version

    /**
     * Map with the strings associated to the most commonly
     * used status codes. In order to use a new user defined
     * status code, it is necessary to include in this map
     * the associated string.
     */
    static map<int, string> StatusCodes;


    /**
     * Initializes the response.
     * @param code Status code (200 by default).
     * @param protocol Protocol version (1.1 by default).
     */
    Response(int code = 200, const Protocol& protocol = Protocol(1, 1))
    {
      this->code = code;
      this->protocol = protocol;
    }

    friend ostream& operator << (ostream &out, const Response &response)
    {
      return out
          << response.protocol << " "
          << response.code << " "
          << Response::StatusCodes[response.code] << Protocol::CRLF;
    }

    friend istream& operator >> (istream &in, Response &response)
    {
      string line, cad;

      if(getline(in, line)) {
        if(line.size() <= 0) in.setstate(istream::failbit);
        else {
          istringstream in_str(line);

          if(!(in_str >> response.protocol >> response.code >> cad))
            in.setstate(istream::failbit);
        }
      }

      return in;
    }
  };

}


#endif /* _HTTP_RESPONSE_H_ */
