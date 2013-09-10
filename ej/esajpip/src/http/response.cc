#include "response.h"


namespace http
{

  map<int, string> Response::StatusCodes;
  Response::StatusCodesInitializer Response::statusCodesInitializer;


  Response::StatusCodesInitializer::StatusCodesInitializer()
  {
    Response::StatusCodes[100] = "Continue";
    Response::StatusCodes[101] = "Switching Protocols";
    Response::StatusCodes[102] = "Processing (WebDAV) (RFC 2518)";
    Response::StatusCodes[200] = "OK";
    Response::StatusCodes[201] = "Created";
    Response::StatusCodes[202] = "Accepted";
    Response::StatusCodes[203] = "Non-Authoritative Information (since HTTP/1.1)";
    Response::StatusCodes[204] = "No Content";
    Response::StatusCodes[205] = "Reset Content";
    Response::StatusCodes[206] = "Partial Content";
    Response::StatusCodes[207] = "Multi-Status (WebDAV) (RFC 4918)";
    Response::StatusCodes[300] = "Multiple Choices";
    Response::StatusCodes[301] = "Moved Permanently";
    Response::StatusCodes[302] = "Found";
    Response::StatusCodes[303] = "See Other (since HTTP/1.1)";
    Response::StatusCodes[304] = "Not Modified";
    Response::StatusCodes[305] = "Use Proxy (since HTTP/1.1)";
    Response::StatusCodes[306] = "Switch Proxy";
    Response::StatusCodes[307] = "Temporary Redirect (since HTTP/1.1)";
    Response::StatusCodes[400] = "Bad Request";
    Response::StatusCodes[401] = "Unauthorized";
    Response::StatusCodes[402] = "Payment Required";
    Response::StatusCodes[403] = "Forbidden";
    Response::StatusCodes[404] = "Not Found";
    Response::StatusCodes[405] = "Method Not Allowed";
    Response::StatusCodes[406] = "Not Acceptable";
    Response::StatusCodes[407] = "Proxy Authentication Required[2]";
    Response::StatusCodes[408] = "Request Timeout  409 Conflict";
    Response::StatusCodes[410] = "Gone";
    Response::StatusCodes[411] = "Length Required";
    Response::StatusCodes[412] = "Precondition Failed";
    Response::StatusCodes[413] = "Request Entity Too Large";
    Response::StatusCodes[414] = "Request-URI Too Long";
    Response::StatusCodes[415] = "Unsupported Media Type";
    Response::StatusCodes[416] = "Requested Range Not Satisfiable";
    Response::StatusCodes[417] = "Expectation Failed";
    Response::StatusCodes[418] = "I'm a teapot";
    Response::StatusCodes[422] = "Unprocessable Entity (WebDAV) (RFC 4918)";
    Response::StatusCodes[423] = "Locked (WebDAV) (RFC 4918)";
    Response::StatusCodes[424] = "Failed Dependency (WebDAV) (RFC 4918)";
    Response::StatusCodes[425] = "Unordered Collection (RFC 3648)";
    Response::StatusCodes[426] = "Upgrade Required (RFC 2817)";
    Response::StatusCodes[449] = "Retry With";
    Response::StatusCodes[450] = "Blocked by Windows Parental Controls";
    Response::StatusCodes[500] = "Internal Server Error";
    Response::StatusCodes[501] = "Not Implemented";
    Response::StatusCodes[502] = "Bad Gateway";
    Response::StatusCodes[503] = "Service Unavailable";
    Response::StatusCodes[504] = "Gateway Timeout";
    Response::StatusCodes[505] = "HTTP Version Not Supported";
    Response::StatusCodes[506] = "Variant Also Negotiates (RFC 2295)";
    Response::StatusCodes[507] = "Insufficient Storage (WebDAV) (RFC 4918)[4]";
    Response::StatusCodes[509] = "Bandwidth Limit Exceeded (Apache bw/limited extension)";
    Response::StatusCodes[510] = "Not Extended (RFC 2774)";
  }

}
