#ifndef _HTTP_PROTOCOL_H_
#define _HTTP_PROTOCOL_H_


#include <string>
#include <iostream>
#include <assert.h>


namespace http
{

  using namespace std;


  /**
   * Class used to identify the HTTP protocol. It is possible
   * to use this class with standard streams.
   */
  class Protocol
  {
   private:
     int mayorVersion_;	///< Mayor protocol version
     int minorVersion_;	///< Minor protocol version

   public:
     /**
      * String with the characters 13 (CR) and 10 (LF),
      * the line separator used in the HTTP protocol.
      */
     static const char CRLF[];


     /**
      * Initialized the protocl with the given version. By
      * default the version is 1.1.
      * @param mayorVersion Mayor protocol version
      * @param minorVersion Minor protocol version
      */
     Protocol(int mayorVersion = 1, int minorVersion = 1)
     {
       assert(((mayorVersion == 1) && (minorVersion == 0)) ||
           ((mayorVersion == 1) && (minorVersion == 1)));

       mayorVersion_ = mayorVersion;
       minorVersion_ = minorVersion;
     }

     /**
      * Copy constructor.
      */
     Protocol(const Protocol& protocol)
     {
       mayorVersion_ = protocol.mayorVersion_;
       minorVersion_ = protocol.minorVersion_;
     }

     friend ostream& operator << (ostream &out, const Protocol &protocol)
     {
       return out << "HTTP/" << protocol.mayorVersion_ << "." << protocol.minorVersion_;
     }

     friend istream& operator >> (istream &in, Protocol &protocol)
     {
       string cad;

       in >> cad;
       if(!cad.compare(0, 8, "HTTP/1.0")) protocol = Protocol(1, 0);
       else if(!cad.compare(0, 8, "HTTP/1.1")) protocol = Protocol(1, 1);
       else in.setstate(istream::failbit);

       return in;
     }

     /**
      * Returns the mayor number of the protocol version.
      */
     int mayorVersion() const
     {
       return mayorVersion_;
     }

     /**
      * Returns the minor number of the protocol version.
      */
     int minorVersion() const
     {
       return minorVersion_;
     }
   };

}

#endif /* _HTTP_PROTOCOL_H_ */
