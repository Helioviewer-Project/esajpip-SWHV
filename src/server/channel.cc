#include "trace.h"
#include "channel.h"
#include "http/connection.h"
#include "http/header.h"
#include "http/protocol.h"
#include "jpeg2000/file_manager.h"
#include "jpip/request.h"
#include "jpip/databin_server.h"
#include "http/response.h"

#include <zlib.h>

#include <cerrno>
#include <climits>
#include <cstring>
#include <exception>
#include <poll.h>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

using http::CRLF;
using jpeg2000::FileManager;
using jpip::DataBinServer;

const char JPIP_HANDLED_HEADER[] =
        "JPIP-handled: tid,cid,cnew=http,stream,len,handled\r\n";

static const char ZERO[] = "0\r\n\r\n";
static const char COMMON_HEADERS[] =
        "Access-Control-Allow-Origin: *\r\n"
        "Strict-Transport-Security: max-age=31536000; includeSubDomains;\r\n"
        "Cache-Control: no-cache\r\n";
static const char TARGET_ID_HEADER[] = "JPIP-tid: 0\r\n";
static const string JPIP_HEADERS =
        string(COMMON_HEADERS) +
        "Transfer-Encoding: chunked\r\n"
        "Content-Type: image/jpp-stream\r\n";
static const string JPIP_GZIP_HEADERS =
        JPIP_HEADERS + "Content-Encoding: gzip\r\n";

static string OptionalJPIPHeaders(const jpip::Request &request) {
    string headers;
    if (request.has.tid)
        headers += TARGET_ID_HEADER;
    if (request.has.handled)
        headers += JPIP_HANDLED_HEADER;
    if (request.has.tid || request.has.handled) {
        headers += "Access-Control-Expose-Headers: ";
        if (request.has.tid)
            headers += "JPIP-tid";
        if (request.has.tid && request.has.handled)
            headers += ',';
        if (request.has.handled)
            headers += "JPIP-handled";
        headers += "\r\n";
    }
    return headers;
}

static int TimeoutMilliseconds(int seconds) {
    if (seconds <= 0)
        return -1;
    if (seconds > INT_MAX / 1000)
        return INT_MAX;
    return seconds * 1000;
}

static bool SendError(http::Connection &connection, int code, const char *reason,
                     const string &message, const string &jpip_headers = "") {
    ostringstream response;
    response << http::Response(code, reason)
             << jpip_headers
             << COMMON_HEADERS
             << "Content-Type: text/plain" << CRLF
             << "Content-Length: " << message.size() << CRLF
             << "Connection: close" << CRLF << CRLF
             << message;
    return connection.Send(response.str());
}

static bool SendData(http::Connection &connection, DataBinServer &data_server,
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
            success = connection.SendChunk(buf.data(), chunk_len);
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
                if (length > 0 && !connection.SendChunk(zbuf.data(), length)) {
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

enum ServeResult {
    KEEP_CHANNEL,
    CLOSE_CHANNEL,
    FAIL_CHANNEL
};

class Channel {
private:
    const Config &cfg;
    const string id;
    const uint64_t number;
    const shared_ptr<ConnectionQueue> queue;
    const ConnectionClosed connection_closed;
    DataBinServer data_server;
    FileManager file_manager;
    vector<char> buf;

    ServeResult Serve(http::Connection &connection) {
        for (;;) {
            http::RequestHead request;
            jpip::Request req;

            if (cfg.log_requests())
                LOG("Waiting for a request ...");

            http::Connection::ReadResult read_result =
                    connection.ReadRequestHead(&request);
            if (read_result != http::Connection::REQUEST_READY) {
                if (read_result == http::Connection::INTERRUPTED)
                    return KEEP_CHANNEL;
                if (read_result == http::Connection::TIMED_OUT) {
                    LOG("The channel " << number << " timed out");
                    return FAIL_CHANNEL;
                }
                if (read_result == http::Connection::REQUEST_TOO_LARGE) {
                    LOG("HTTP request head is too large");
                    SendError(connection, 431, "Request Header Fields Too Large",
                              "HTTP request head is too large");
                    return FAIL_CHANNEL;
                }
                if (read_result == http::Connection::CONNECTION_CLOSED)
                    return file_manager.GetImage() ? KEEP_CHANNEL : FAIL_CHANNEL;
                return FAIL_CHANNEL;
            }

            if (request.unsupported_body) {
                const char message[] = "HTTP request bodies are not supported";
                LOG(message);
                SendError(connection, 400, "Bad Request", message);
                return FAIL_CHANNEL;
            }

            string request_error;
            if (!req.Parse(request.line, &request_error)) {
                LOG("Bad request: " << http::EscapeForLog(request.line));
                SendError(connection, 400, "Bad Request", request_error,
                          OptionalJPIPHeaders(req));
                return FAIL_CHANNEL;
            }

            if (cfg.log_requests())
                LOG("Request: " << http::EscapeForLog(request.line));

            const char *err_msg = nullptr;
            int error_code = 500;
            const char *error_reason = "Internal Server Error";
            string file_name;
            bool send_gzip = req.has.metareq && request.accepts_gzip;
            auto set_channel_error = [&](const char *message) {
                err_msg = message;
                error_code = 503;
                error_reason = "Service Unavailable";
            };

            if (req.has.cclose) {
                if (!file_manager.GetImage()) {
                    set_channel_error("No JPIP channel is open");
                    /* Only one channel per client supported */
                } else if (req.channel != id) {
                    set_channel_error(
                            "The close request identifies a different JPIP channel");
                } else {
                    LOG("The channel " << number << " has been closed");

                    ostringstream msg;
                    msg << http::Response(200, "OK")
                            << OptionalJPIPHeaders(req)
                            << COMMON_HEADERS
                            << "Content-Length: 0" << CRLF << CRLF;
                    (void) connection.Send(msg.str());
                    return CLOSE_CHANNEL;
                }
            } else if (req.has.cnew) {
                if (!req.accepts_http) {
                    err_msg = "The requested JPIP channel transport is not supported";
                    error_code = 501;
                    error_reason = "Not Implemented";
                } else if (file_manager.GetImage()) {
                    set_channel_error(
                            "A JPIP channel is already open on this connection");
                } else {
                    file_name = req.has.target ? req.target : req.object;

                    FileManager::OpenResult open_result =
                            file_manager.OpenImage(file_name);
                    if (open_result == FileManager::OpenResult::NOT_FOUND) {
                        err_msg = "The requested image was not found";
                        error_code = 404;
                        error_reason = "Not Found";
                    } else if (open_result == FileManager::OpenResult::INVALID_PATH) {
                        err_msg = "The requested image path is invalid";
                    } else if (open_result == FileManager::OpenResult::UNSUPPORTED) {
                        err_msg = "The requested image type is not supported";
                    } else if (open_result == FileManager::OpenResult::UNREADABLE) {
                        err_msg = "The requested image could not be read";
                    } else if (open_result == FileManager::OpenResult::INVALID) {
                        err_msg = "The requested image is not a valid supported JPEG 2000 source";
                    }
                }
            } else if (req.has.cid) {
                if (!file_manager.GetImage()) {
                    set_channel_error("No JPIP channel is open");
                } else if (req.channel != id)
                    set_channel_error(
                            "The request identifies a different JPIP channel");
            } else {
                err_msg = "The request has no JPIP channel parameter";
                error_code = 400;
                error_reason = "Bad Request";
            }

            if (err_msg) {
                LOG(err_msg);
                SendError(connection, error_code, error_reason, err_msg,
                          OptionalJPIPHeaders(req));
                return FAIL_CHANNEL;
            }

            if (!data_server.SetRequest(*file_manager.GetImage(), req,
                                        &request_error)) {
                LOG(request_error);
                SendError(connection, 400, "Bad Request", request_error,
                          OptionalJPIPHeaders(req));
                return FAIL_CHANNEL;
            }

            bool sent;
            if (req.has.cnew) {
                LOG("The channel " << number << " has been opened for the image '"
                                    << file_name << "'");

                ostringstream msg;
                msg << http::Response(200, "OK")
                        << http::Header("JPIP-cnew", "cid=" + id + ",path=jpip,transport=http")
                        << http::Header("JPIP-tid", "0")
                        << (req.has.handled ? JPIP_HANDLED_HEADER : "")
                        << (req.has.handled
                                ? "Access-Control-Expose-Headers: JPIP-cnew,JPIP-tid,JPIP-handled"
                                : "Access-Control-Expose-Headers: JPIP-cnew,JPIP-tid")
                        << CRLF
                        << (send_gzip ? JPIP_GZIP_HEADERS : JPIP_HEADERS)
                        << CRLF;
                sent = connection.Send(msg.str());
            } else {
                const string &base_headers = send_gzip ? JPIP_GZIP_HEADERS
                                                       : JPIP_HEADERS;
                string optional_headers;
                const string *headers = &base_headers;
                if (req.has.tid || req.has.handled) {
                    optional_headers = OptionalJPIPHeaders(req) + base_headers;
                    headers = &optional_headers;
                }
                sent = connection.SendOK(*headers);
            }
            if (!sent ||
                !SendData(connection, data_server, file_manager, buf, send_gzip) ||
                !connection.Send(ZERO, sizeof ZERO - 1))
                return FAIL_CHANNEL;
            file_manager.ClearFiles();
            if (request.close)
                return KEEP_CHANNEL;
        }
    }

    bool WaitForConnection(int *connection) {
        pollfd fd = {queue->GetDescriptor(), POLLIN, 0};
        int result;
        do {
            result = poll(&fd, 1, TimeoutMilliseconds(cfg.connection_timeout()));
        } while (result < 0 && errno == EINTR);
        if (result < 0) {
            LOG("Channel handoff poll failed: " << strerror(errno));
            return false;
        }
        if (result == 0) {
            LOG("The channel " << number << " timed out");
            return false;
        }
        if (!(fd.revents & POLLIN)) {
            LOG("The channel " << number << " handoff failed");
            return false;
        }
        return queue->Pop(connection);
    }

    void CloseConnection(int fd) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
        connection_closed();
        LOG("Closing connection [" << fd << "] (client finished)");
    }

public:
    Channel(const Config &_cfg, const string &_id, uint64_t _number,
            const shared_ptr<ConnectionQueue> &_queue,
            ConnectionClosed _connection_closed)
        : cfg(_cfg), id(_id), number(_number), queue(_queue),
          connection_closed(_connection_closed), buf(_cfg.max_chunk_size()) {}

    void Run() {
        if (!file_manager.Init(cfg.image_directory())) {
            ERROR("The file manager can not be initialized");
        } else {
            int fd;
            while (WaitForConnection(&fd)) {
                ServeResult result = FAIL_CHANNEL;
                bool failed_with_exception = false;
                try {
                    http::Connection connection(fd, queue->GetDescriptor(),
                                                cfg.connection_timeout());
                    if (connection.Configure())
                        result = Serve(connection);
                } catch (...) {
                    failed_with_exception = true;
                }
                CloseConnection(fd);
                if (failed_with_exception)
                    ERROR("The channel " << number << " failed with an exception");
                if (result != KEEP_CHANNEL)
                    break;
            }
        }

        int pending;
        if (queue->Close(pending))
            CloseConnection(pending);
    }
};

static void CloseQueuedConnection(const shared_ptr<ConnectionQueue> &queue,
                                  ConnectionClosed connection_closed) {
    int pending;
    if (queue->Close(pending)) {
        shutdown(pending, SHUT_RDWR);
        close(pending);
        connection_closed();
    }
}

void RunChannel(const Config &cfg, const string &channel, uint64_t channel_number,
                const shared_ptr<ConnectionQueue> &queue,
                ConnectionClosed connection_closed) {
    try {
        Channel(cfg, channel, channel_number, queue, connection_closed).Run();
        return;
    } catch (const exception &error) {
        CloseQueuedConnection(queue, connection_closed);
        ERROR("The channel " << channel_number << " failed: " << error.what());
    } catch (...) {
        CloseQueuedConnection(queue, connection_closed);
        ERROR("The channel " << channel_number << " failed with an unknown exception");
    }
}
