#include "trace.h"
#include "client_manager.h"
#include "http/header.h"
#include "jpeg2000/file_manager.h"
#include "jpip/request.h"
#include "jpip/databin_server.h"
#include "http/response.h"
#include "net/socket_stream.h"

#include "z/zfilter.h"
#include <glib.h>

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

static int SendChunk(Socket &socket, const void *buf, size_t len) {
    if (len > 0) {
        char header[2 * sizeof(size_t) + 3];
        int header_len = snprintf(header, sizeof header, "%zx\r\n", len);
        char trailer[] = "\r\n";
        iovec buffers[] = {
            {header, static_cast<size_t>(header_len)},
            {const_cast<void *>(buf), len},
            {trailer, 2}
        };

        if (SendAll(socket, buffers, 3))
            return -1;
    }
    return 0;
}

static const int true_val = 1;
// static const int false_val = 0;
static const int sndbuf_val = 524288;

void RunClient(const AppConfig &cfg, int fd, int base_id) {
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
        close(fd);
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

    ostringstream head_data, head_data_gzip;
    head_data << "Access-Control-Allow-Origin: " << CORS << Protocol::CRLF
              << "Strict-Transport-Security: " << STS << Protocol::CRLF
              << "Cache-Control: " << NOCACHE << Protocol::CRLF
              << "Transfer-Encoding: chunked" << Protocol::CRLF
              << "Content-Type: image/jpp-stream" << Protocol::CRLF;
    head_data_gzip << head_data.str() << "Content-Encoding: gzip" << Protocol::CRLF;

    Socket socket(fd);
    SocketStream sock_stream(&socket, 1024);
    string channel = to_string(base_id);

    int chunk_len = 0;
    int log_requests = cfg.log_requests();
    size_t buf_len = cfg.max_chunk_size();
    vector<char> buf(buf_len);

    while (!pclose) {
        bool accept_gzip = false;
        bool send_gzip = false;

        if (log_requests)
            LOGC(_BLUE, "Waiting for a request ...");

        req_line_raw.clear();
        errno = 0;
        if (!getline(sock_stream, req_line_raw).good()) {
            if (req_line_raw.empty() && errno == 0)
                break;

            if (errno != 0)
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
        while ((sock_stream >> header).good()) {
            if (header.Is("Accept-Encoding") &&
                header.value.find("gzip") != string::npos)
                accept_gzip = true;
        }
        sock_stream.clear();

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
            } else if (req.parameters["cclose"] != "*" && req.parameters["cclose"] != channel) {
                err_msg = "Close request received related to another channel";
                LOG(err_msg);
            } else {
                req.cache_model.Clear();
                LOG("The channel " << channel << " has been closed");

                ostringstream msg;
                msg << http::Response(200, "OK")
                        << "Access-Control-Allow-Origin: " << CORS << Protocol::CRLF
                        << "Strict-Transport-Security: " << STS << Protocol::CRLF
                        << "Cache-Control: " << NOCACHE << Protocol::CRLF
                        << "Content-Length: 0" << Protocol::CRLF
                        << http::Protocol::CRLF;
                SendStream(socket, msg);
                break; // break connection
            }
        } else if (req.mask.items.cnew) {
            if (is_opened) {
                err_msg = "There already is a channel opened. Only one channel per client is supported";
                LOG(err_msg);
            } else {
                string file_name = req.mask.items.target ? req.parameters["target"] : req.object;

                if (!file_manager.OpenImage(file_name)) {
                    ERROR("The image file '" << file_name << "' can not be read");
                } else {
                    is_opened = true;
                    data_server.SetRequest(file_manager, req);
                    LOG("The channel " << channel << " has been opened for the image '" << file_name << "'");

                    ostringstream msg;
                    msg << http::Response(200, "OK")
                            << http::Header("JPIP-cnew", "cid=" + channel + ",path=jpip,transport=http")
                            << http::Header("JPIP-tid", file_name)
                            << "Access-Control-Expose-Headers: JPIP-cnew,JPIP-tid" << Protocol::CRLF
                            << (send_gzip ? head_data_gzip.str() : head_data.str())
                            << http::Protocol::CRLF;
                    SendStream(socket, msg);
                    send_data = true;
                }
            }
        } else if (req.mask.items.cid) {
            if (!is_opened) {
                err_msg = "Request received but no channel is opened";
                LOG(err_msg);
            } else {
                if (req.parameters["cid"] != channel) {
                    err_msg = "Request related to another channel";
                    LOG(err_msg);
                } else {
                    data_server.SetRequest(file_manager, req);
                    ostringstream msg;
                    msg << http::Response(200, "OK")
                            << (send_gzip ? head_data_gzip.str() : head_data.str())
                            << http::Protocol::CRLF;
                    SendStream(socket, msg);
                    send_data = true;
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
                    << "Access-Control-Allow-Origin: " << CORS << Protocol::CRLF
                    << "Strict-Transport-Security: " << STS << Protocol::CRLF
                    << "Cache-Control: " << NOCACHE << Protocol::CRLF
                    << "Content-Length: " << err_msg_len << Protocol::CRLF
                    << http::Protocol::CRLF;
            if (err_msg_len)
                msg << err_msg;
            SendStream(socket, msg);
        } else if (send_data) {
            if (!send_gzip) {
                for (bool last = false; !last;) {
                    chunk_len = buf_len;

                    if (!data_server.GenerateChunk(file_manager, buf.data(), &chunk_len, &last)) {
                        ERROR("A new data chunk could not be generated");
                        pclose = true;
                        break;
                    }
                    if (chunk_len <= 0 && !last) {
                        ERROR("No JPIP data chunk was generated before response completion");
                        pclose = true;
                        break;
                    }
                    if (SendChunk(socket, buf.data(), chunk_len)) {
                        pclose = true;
                        break;
                    }
                }
            } else {
                void *obj = zfilter_new();

                for (bool last = false; !last;) {
                    chunk_len = buf_len;

                    if (!data_server.GenerateChunk(file_manager, buf.data(), &chunk_len, &last)) {
                        ERROR("A new data chunk could not be generated");
                        pclose = true;
                        break;
                    }
                    if (chunk_len <= 0 && !last) {
                        ERROR("No JPIP data chunk was generated before response completion");
                        pclose = true;
                        break;
                    }

                    if (chunk_len > 0)
                        zfilter_write(obj, buf.data(), chunk_len);
                }

                size_t nbytes;
                const uint8_t *out = (uint8_t *) zfilter_bytes(obj, &nbytes);

                while (nbytes > buf_len) {
                    if (SendChunk(socket, out, buf_len)) {
                        pclose = true;
                        goto zend;
                    }
                    nbytes -= buf_len;
                    out += buf_len;
                }
                if (nbytes > 0 && SendChunk(socket, out, nbytes))
                    pclose = true;

            zend:
                zfilter_del(obj);
            }

            if (pclose || SendAll(socket, ZERO, sizeof ZERO - 1))
                break;
            file_manager.ClearFiles();
        }
    }

    socket.Close(); // closes fd
}
