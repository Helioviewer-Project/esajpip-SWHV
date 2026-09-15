#include "trace.h"
#include "client_manager.h"
#include "http/header.h"
#include "http/protocol.h"
#include "jpeg2000/file_manager.h"
#include "jpip/request.h"
#include "jpip/databin_server.h"
#include "http/response.h"
#include "net/socket.h"

#include <glib.h>
#include <zlib.h>

#include <cstdio>
#include <vector>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>

static const char ZERO[] = "0\r\n\r\n";

static const char *CORS = "*";
static const char *NOCACHE = "no-cache";
static const char *STS = "max-age=31536000; includeSubDomains;";

using namespace std;
using namespace net;
using namespace http;
using namespace jpip;
using namespace jpeg2000;

class SocketReader {
private:
    Socket &socket;
    char buf[1024];
    size_t pos = 0;
    size_t len = 0;

public:
    enum Result {
        LINE,
        CLOSED,
        INCOMPLETE,
        ERROR
    };

    explicit SocketReader(Socket &_socket) : socket(_socket) {
    }

    Result ReadLine(string &line) {
        line.clear();
        for (;;) {
            const char *newline = static_cast<const char *>(memchr(buf + pos, '\n', len - pos));
            if (newline != NULL) {
                line.append(buf + pos, newline - (buf + pos));
                pos = newline - buf + 1;
                return LINE;
            }

            line.append(buf + pos, len - pos);
            ssize_t received = socket.Receive(buf, sizeof buf);
            if (received <= 0) {
                if (received < 0)
                    return ERROR;
                return line.empty() ? CLOSED : INCOMPLETE;
            }
            pos = 0;
            len = received;
        }
    }
};

static int SendAll(Socket &socket, iovec *buffers, int count) {
    while (count > 0) {
        ssize_t sent = writev(socket, buffers, count);
        if (sent < 0) {
            if (errno == EINTR)
                continue;

            ERROR("Could not send: " << strerror(errno));
            return -1;
        }
        if (sent == 0) {
            ERROR("Could not send: connection closed");
            return -1;
        }

        while (count > 0 && sent >= static_cast<ssize_t>(buffers->iov_len)) {
            sent -= buffers->iov_len;
            buffers++;
            count--;
        }
        if (sent > 0) {
            buffers->iov_base = static_cast<char *>(buffers->iov_base) + sent;
            buffers->iov_len -= sent;
        }
    }

    return 0;
}

static int SendAll(Socket &socket, const void *buf, size_t len) {
    iovec buffer = {const_cast<void *>(buf), len};
    return SendAll(socket, &buffer, 1);
}

static int SendStream(Socket &socket, const ostringstream &stream) {
    string str = stream.str();
    return SendAll(socket, str.data(), str.size());
}

static int SendOK(Socket &socket, const string &headers) {
    static const char status[] = "HTTP/1.1 200 OK\r\n";
    iovec buffers[] = {
        {const_cast<char *>(status), sizeof status - 1},
        {const_cast<char *>(headers.data()), headers.size()},
        {const_cast<char *>(CRLF), sizeof CRLF - 1}
    };
    return SendAll(socket, buffers, 3);
}

static int SendChunk(Socket &socket, const void *buf, size_t len) {
    if (len > 0) {
        char header[2 * sizeof(size_t) + 3];
        int header_len = snprintf(header, sizeof header, "%zx\r\n", len);
        iovec buffers[] = {
            {header, static_cast<size_t>(header_len)},
            {const_cast<void *>(buf), len},
            {const_cast<char *>(CRLF), sizeof CRLF - 1}
        };

        if (SendAll(socket, buffers, 3))
            return -1;
    }
    return 0;
}

static bool SendData(Socket &socket, DataBinServer &data_server,
                     FileManager &file_manager, vector<char> &buf, bool gzip) {
    z_stream zstream = {};
    vector<unsigned char> zbuf;
    if (gzip) {
        if (deflateInit2(&zstream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                         MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            ERROR("Could not initialize gzip compression");
            return false;
        }
        zbuf.resize(buf.size());
        zstream.next_out = zbuf.data();
        zstream.avail_out = zbuf.size();
    }

    bool success = true;
    for (bool last = false; !last && success;) {
        int chunk_len = buf.size();
        if (!data_server.GenerateChunk(file_manager, buf.data(), &chunk_len, &last)) {
            ERROR("A new data chunk could not be generated");
            success = false;
            break;
        }
        if (chunk_len <= 0 && !last) {
            ERROR("No JPIP data chunk was generated before response completion");
            success = false;
            break;
        }

        if (!gzip) {
            success = SendChunk(socket, buf.data(), chunk_len) == 0;
            continue;
        }

        zstream.next_in = reinterpret_cast<Bytef *>(buf.data());
        zstream.avail_in = chunk_len;
        int flush = last ? Z_FINISH : Z_NO_FLUSH;
        int result;
        do {
            result = deflate(&zstream, flush);
            if (result != Z_OK && result != Z_STREAM_END) {
                ERROR("Could not compress JPIP data");
                success = false;
                break;
            }

            if (zstream.avail_out == 0 || result == Z_STREAM_END) {
                size_t length = zbuf.size() - zstream.avail_out;
                if (length > 0 && SendChunk(socket, zbuf.data(), length)) {
                    success = false;
                    break;
                }
                zstream.next_out = zbuf.data();
                zstream.avail_out = zbuf.size();
            }
        } while (zstream.avail_in > 0 || (last && result != Z_STREAM_END));
    }

    if (gzip)
        deflateEnd(&zstream);
    return success;
}

static const int true_val = 1;
// static const int false_val = 0;
static const int sndbuf_val = 524288;

void RunClient(const AppConfig &cfg, Socket &socket, uint64_t connection_id) {
    int fd = socket;
    int sockopt_ret = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf_val, sizeof sndbuf_val) |
                      // setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &false_val, sizeof false_val) |
                      setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &true_val, sizeof true_val);
    int time_out;
    if (sockopt_ret == 0 && (time_out = cfg.com_time_out()) > 0) {
        timeval tv;
        tv.tv_sec = time_out;
        tv.tv_usec = 0;
        sockopt_ret |= setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) |
                setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    }
    if (sockopt_ret != 0) {
        LOG("setsockopt failed: " << strerror(errno));
        return;
    }

    ///

    string req_line, req_line_raw;
    jpip::Request req;
    bool pclose = false;
    bool is_opened = false;
    bool send_data = false;
    DataBinServer data_server;

    FileManager file_manager;
    if (!file_manager.Init(cfg.images_folder())) {
        ERROR("The file manager can not be initialized");
        return;
    }

    ostringstream header_stream;
    header_stream << "Access-Control-Allow-Origin: " << CORS << CRLF
                  << "Strict-Transport-Security: " << STS << CRLF
                  << "Cache-Control: " << NOCACHE << CRLF
                  << "Transfer-Encoding: chunked" << CRLF
                  << "Content-Type: image/jpp-stream" << CRLF;
    const string head_data = header_stream.str();
    const string head_data_gzip = head_data + "Content-Encoding: gzip" + CRLF;

    SocketReader reader(socket);
    string channel = to_string(connection_id);

    int log_requests = cfg.log_requests();
    size_t buf_len = cfg.max_chunk_size();
    vector<char> buf(buf_len);

    while (!pclose) {
        bool accept_gzip = false;
        bool send_gzip = false;

        if (log_requests)
            LOGC(_BLUE, "Waiting for a request ...");

        req_line_raw.clear();
        SocketReader::Result read_result = reader.ReadLine(req_line_raw);
        if (read_result != SocketReader::LINE) {
            if (read_result == SocketReader::CLOSED)
                break;

            if (read_result == SocketReader::ERROR)
                LOG("Request read error: " << strerror(errno));
            else {
                char *escaped = g_strescape(req_line_raw.c_str(), NULL);
                LOG("Incomplete request line: " << escaped);
                g_free(escaped);
            }
            break;
        }

        char *escaped = g_strescape(req_line_raw.c_str(), NULL);
        req_line.assign(escaped);
        g_free(escaped);

        if (!req.Parse(req_line)) {
            LOG("Bad request: " << req_line);
            break;
        }

        if (log_requests)
            LOGC(_BLUE, "Request: " << req_line);

        http::Header header;
        string header_line;
        bool headers_complete = false;
        for (;;) {
            SocketReader::Result header_result = reader.ReadLine(header_line);
            if (header_result != SocketReader::LINE) {
                if (header_result == SocketReader::ERROR)
                    LOG("Header read error: " << strerror(errno));
                else if (header_result == SocketReader::INCOMPLETE)
                    LOG("Incomplete HTTP header");
                break;
            }
            if (!header_line.empty() && header_line.back() == '\r')
                header_line.pop_back();
            if (header_line.empty()) {
                headers_complete = true;
                break;
            }
            if (!header.Parse(header_line)) {
                LOG("Invalid HTTP header");
                break;
            }
            if (header.Is("Accept-Encoding") &&
                header.value.find("gzip") != string::npos)
                accept_gzip = true;
        }
        if (!headers_complete)
            break;

        const char *err_msg = "";
        pclose = true;
        send_data = false;

        if (req.mask.items.metareq && accept_gzip)
            send_gzip = true;

        if (req.mask.items.cclose) {
            if (!is_opened) {
                err_msg = "Close request received but there is not any channel opened";
                LOG(err_msg);
                /* Only one channel per client supported */
            } else if (req.channel != "*" && req.channel != channel) {
                err_msg = "Close request received related to another channel";
                LOG(err_msg);
            } else {
                req.cache_model.Clear();
                LOG("The channel " << channel << " has been closed");

                ostringstream msg;
                msg << http::Response(200, "OK")
                        << "Access-Control-Allow-Origin: " << CORS << CRLF
                        << "Strict-Transport-Security: " << STS << CRLF
                        << "Cache-Control: " << NOCACHE << CRLF
                        << "Content-Length: 0" << CRLF << CRLF;
                SendStream(socket, msg);
                break; // break connection
            }
        } else if (req.mask.items.cnew) {
            if (is_opened) {
                err_msg = "There already is a channel opened. Only one channel per client is supported";
                LOG(err_msg);
            } else {
                string file_name = req.mask.items.target ? req.target : req.object;

                if (!file_manager.OpenImage(file_name)) {
                    ERROR("The image file '" << file_name << "' can not be read");
                } else {
                    if (!data_server.SetRequest(file_manager, req)) {
                        err_msg = "Invalid JPIP request for the selected image";
                        LOG(err_msg);
                    } else {
                        is_opened = true;
                        LOG("The channel " << channel << " has been opened for the image '" << file_name << "'");

                        ostringstream msg;
                        msg << http::Response(200, "OK")
                                << http::Header("JPIP-cnew", "cid=" + channel + ",path=jpip,transport=http")
                                << http::Header("JPIP-tid", file_name)
                                << "Access-Control-Expose-Headers: JPIP-cnew,JPIP-tid" << CRLF
                                << (send_gzip ? head_data_gzip : head_data)
                                << CRLF;
                        SendStream(socket, msg);
                        send_data = true;
                    }
                }
            }
        } else if (req.mask.items.cid) {
            if (!is_opened) {
                err_msg = "Request received but no channel is opened";
                LOG(err_msg);
            } else {
                if (req.channel != channel) {
                    err_msg = "Request related to another channel";
                    LOG(err_msg);
                } else {
                    if (!data_server.SetRequest(file_manager, req)) {
                        err_msg = "Invalid JPIP request for the selected image";
                        LOG(err_msg);
                    } else {
                        SendOK(socket, send_gzip ? head_data_gzip : head_data);
                        send_data = true;
                    }
                }
            }
        } else {
            err_msg = "Invalid request (channel parameter not found)";
            LOG(err_msg);
        }

        pclose = pclose && !send_data;

        if (pclose) {
            size_t err_msg_len = strlen(err_msg);
            ostringstream msg;
            msg << http::Response(500, "Internal Server Error")
                    << "Access-Control-Allow-Origin: " << CORS << CRLF
                    << "Strict-Transport-Security: " << STS << CRLF
                    << "Cache-Control: " << NOCACHE << CRLF
                    << "Content-Length: " << err_msg_len << CRLF << CRLF;
            if (err_msg_len)
                msg << err_msg;
            SendStream(socket, msg);
        } else if (send_data) {
            if (!SendData(socket, data_server, file_manager, buf, send_gzip) ||
                SendAll(socket, ZERO, sizeof ZERO - 1))
                break;
            file_manager.ClearFiles();
        }
    }
}
