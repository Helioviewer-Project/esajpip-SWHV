#include "trace.h"
#include "client_manager.h"
#include "jpeg2000/file_manager.h"
#include "jpip/jpip.h"
#include "jpip/request.h"
#include "jpip/databin_server.h"
#include "http/response.h"
#include "net/socket_stream.h"

#include "z/zfilter.h"
#include <glib.h>

#include <sys/socket.h>
#include <sys/time.h>

static const char *CORS = "*";
static const char *NOCACHE = "no-cache";
static const char *STS = "max-age=31536000; includeSubDomains;";

using namespace std;
using namespace net;
using namespace http;
using namespace jpip;
using namespace jpeg2000;

static int send_chunk(SocketStream &strm, const void *buf, size_t len) {
    if (len > 0) {
        strm << hex << len << dec << http::Protocol::CRLF;
        if (strm.Send(buf, len) != (ssize_t) len) {
            ERROR("Could not send");
            return -1;
        }
        strm << http::Protocol::CRLF;
    }
    return 0;
}

static const int true_val = 1;
static const int sndbuf_val = 524288;

void ClientManager::Run(ClientInfo *client_info) {
    int socket = client_info->sock();
    int sockopt_ret = setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &true_val, sizeof true_val) |
                      setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &sndbuf_val, sizeof sndbuf_val);
                   // setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &true_val, sizeof true_val);
    int time_out;
    if (sockopt_ret == 0 && (time_out = cfg.com_time_out()) > 0) {
        struct timeval tv;
        tv.tv_sec = time_out;
        tv.tv_usec = 0;
        sockopt_ret |= setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) |
                       setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    }
    if (sockopt_ret != 0) { // is there a point reporting error?
        close(socket);
        return;
    }

    ///

    bool com_error;
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

    stringstream head_data, head_data_gzip;
    head_data << http::Header::AccessControlAllowOrigin(CORS)
              << http::Header::StrictTransportSecurity(STS)
              << http::Header::CacheControl(NOCACHE)
              << http::Header::TransferEncoding("chunked")
              << http::Header::ContentType("image/jpp-stream");
    head_data_gzip << head_data.str() << http::Header::ContentEncoding("gzip");

    SocketStream sock_stream(socket, 512, 64 * 1024);
    string channel = to_string(client_info->base_id());

    int chunk_len = 0;
    size_t buf_len = cfg.max_chunk_size();
    char *buf = new char[buf_len];

    while (!pclose) {
        bool accept_gzip = false;
        bool send_gzip = false;

        if (cfg.log_requests())
            LOGC(_BLUE, "Waiting for a request ...");

        com_error = true;
        if (getline(sock_stream, req_line_raw).good()) {
            char *req_line_escape = g_strescape(req_line_raw.c_str(), NULL);
            req_line.assign(req_line_escape);
            g_free(req_line_escape);

            com_error = !req.Parse(req_line);
        } else
            req_line.assign(strerror(errno));

        if (com_error) {
            LOG("Bad request or read error: " << req_line);
            break;
        } else {
            if (cfg.log_requests())
                LOGC(_BLUE, "Request: " << req_line);

            http::Header header;

            while ((sock_stream >> header).good()) {
                if (header == http::Header::AcceptEncoding() &&
                    header.value.find("gzip") != string::npos)
                    accept_gzip = true;
            }
            sock_stream.clear();
        }

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
                pclose = false;
                is_opened = false;
                req.cache_model.Clear();
                LOG("The channel " << channel << " has been closed");
                sock_stream << http::Response(200)
                            << http::Header::AccessControlAllowOrigin(CORS)
                            << http::Header::StrictTransportSecurity(STS)
                            << http::Header::CacheControl(NOCACHE)
                            << http::Header::ContentLength("0")
                            << http::Protocol::CRLF << flush;
                break; // break connection
            }
        } else if (req.mask.items.cnew) {
            if (is_opened) {
                err_msg = "There already is a channel opened. Only one channel per client is supported";
                LOG(err_msg);
            } else {
                string file_name = (req.mask.items.target ? req.parameters["target"] : req.object);

                if (!file_manager.OpenImage(file_name))
                    ERROR("The image file '" << file_name << "' can not be read");
                else {
                    is_opened = true;
                    data_server.Reset();
                    if (!data_server.SetRequest(file_manager, req))
                        ERROR("The server can not process the request");
                    else {
                        LOG("The channel " << channel << " has been opened for the image '" << file_name << "'");

                        sock_stream << http::Response(200)
                                    << http::Header("JPIP-cnew", "cid=" + channel + ",path=jpip,transport=http")
                                    << http::Header("JPIP-tid", file_name)
                                    << http::Header::AccessControlExposeHeaders("JPIP-cnew,JPIP-tid")
                                    << (send_gzip ? head_data_gzip.str() : head_data.str())
                                    << http::Protocol::CRLF << flush;
                        send_data = true;
                    }
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
                } else if (!data_server.SetRequest(file_manager, req))
                    ERROR("The server can not process the request");
                else {
                    sock_stream << http::Response(200)
                                << (send_gzip ? head_data_gzip.str() : head_data.str())
                                << http::Protocol::CRLF << flush;
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
            sock_stream << http::Response(500)
                        << http::Header::AccessControlAllowOrigin(CORS)
                        << http::Header::StrictTransportSecurity(STS)
                        << http::Header::CacheControl(NOCACHE)
                        << http::Header::ContentLength(to_string(err_msg_len))
                        << http::Protocol::CRLF << flush;
            if (err_msg_len) {
                if (sock_stream->Send(err_msg, err_msg_len) != (ssize_t) err_msg_len) {
                    ERROR("Could not send");
                    break;
                }
            }
        } else if (send_data) {
            if (!send_gzip)
                for (bool last = false; !last;) {
                    chunk_len = buf_len;

                    if (!data_server.GenerateChunk(file_manager, buf, &chunk_len, &last)) {
                        ERROR("A new data chunk could not be generated");
                        pclose = true;
                        break;
                    }
                    if (send_chunk(sock_stream, buf, chunk_len))
                        break;
                }
            else {
                void *obj = zfilter_new();

                for (bool last = false; !last;) {
                    chunk_len = buf_len;

                    if (!data_server.GenerateChunk(file_manager, buf, &chunk_len, &last)) {
                        ERROR("A new data chunk could not be generated");
                        pclose = true;
                        break;
                    }

                    if (chunk_len > 0)
                        zfilter_write(obj, buf, chunk_len);
                }

                size_t nbytes;
                const uint8_t *out = (uint8_t *) zfilter_bytes(obj, &nbytes);

                while (nbytes > buf_len) {
                    if (send_chunk(sock_stream, out, buf_len))
                        goto zend;
                    nbytes -= buf_len;
                    out += buf_len;
                }
                if (nbytes > 0)
                    send_chunk(sock_stream, out, nbytes);

             zend:
                zfilter_del(obj);
            }

            sock_stream << "0" << http::Protocol::CRLF << http::Protocol::CRLF << flush;
        }
    }

    delete[] buf;

    sock_stream->Close();
    close(socket);
}
